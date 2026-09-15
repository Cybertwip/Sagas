"""Decode Remix CSS portraits and emit the selectable roster.

Unique CSS entries (new portraits, not J/E clones or polygons) are written to
remix_css.tsv. 32x32 RGBA16 portraits come from File.CHARACTER_PORTRAITS.
"""
import argparse
import csv
import re
import struct
from pathlib import Path

PORTRAIT_FILE = 2565
SKIP = {"RANDOM", "SANDBAG"}
VARIANT_PREFIX = ("J", "E", "N")

def rgba16(value):
    return (((value >> 11) & 31) * 255 // 31,
            ((value >> 6) & 31) * 255 // 31,
            ((value >> 1) & 31) * 255 // 31,
            255 if value & 1 else 0)

def constants(text, scope):
    block = text
    if scope:
        match = re.search(rf"scope {scope}\s*:\s*\{{", text)
        if not match: return {}
        start = match.end()
        depth = 1
        i = start
        while i < len(text) and depth:
            if text[i] == "{": depth += 1
            elif text[i] == "}": depth -= 1
            i += 1
        block = text[start:i]
    def number_expr(expr):
        total, sign = 0, 1
        for part in re.split(r"([+-])", expr.replace(" ", "")):
            if part == "+": sign = 1
            elif part == "-": sign = -1
            elif part:
                total += sign * int(part, 0)
        return total
    return {name: number_expr(value) for name, value in
            re.findall(r"constant\s+(\w+)\(\s*([^)]+)\)", block)}

def links(path):
    rows = {}
    for line in path.read_text().splitlines()[1:]:
        location, target, offset = line.split("\t")
        rows[int(location)] = (int(target), int(offset))
    return rows

def decode_portrait(data, relocs, sprite_offset, width=32, height=32):
    image = relocs.get(sprite_offset - 8)
    if not image: return None
    offset = image[1]
    pixels = bytearray(width * height * 4)
    for index in range(width * height):
        value = struct.unpack_from(">H", data, offset + index * 2)[0]
        r, g, b, a = rgba16(value)
        pixels[index * 4:index * 4 + 4] = bytes((r, g, b, a))
    return width, height, bytes(pixels)

def write_png(path, width, height, rgba):
    import zlib
    def chunk(tag, payload):
        header = tag + payload
        return struct.pack(">I", len(payload)) + header + struct.pack(">I", zlib.crc32(header) & 0xffffffff)
    raw = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(raw, 9)) +
        chunk(b"IEND", b""))

def unique_keys(select_source):
    keys = []
    for match in re.finditer(
            r"add_to_css\(\s*Character\.id\.(\w+)\s*,.*?portrait_offsets\.(\w+)\s*,\s*([^)]+)\)",
            select_source, re.S):
        key, portrait, override = match.group(1), match.group(2), match.group(3).strip()
        if key in SKIP: continue
        if key.startswith("N"): continue
        if key[0] in "JE" and override not in ("-1", "BOOKEND_BONUS_PORTRAIT"): continue
        keys.append((key, portrait))
    return keys

def extract(source, resources, fighters):
    select = (source / "src/CharacterSelect.asm").read_text()
    offsets = constants(select, "portrait_offsets")
    data = (resources / "reloc" / f"{PORTRAIT_FILE:04d}.bin").read_bytes()
    relocs = links(resources / "reloc" / f"{PORTRAIT_FILE:04d}.links.tsv")
    roster_lines = (fighters / "remix_roster.tsv").read_text().splitlines()
    roster = {row["key"] for row in csv.DictReader(roster_lines[1:], delimiter="\t")}
    portraits = fighters.parent / "remix" / "css" / "portraits"
    portraits.mkdir(parents=True, exist_ok=True)
    rows = []
    for key, portrait_name in unique_keys(select):
        if key not in roster: continue
        offset = offsets.get(portrait_name)
        if offset is None: continue
        decoded = decode_portrait(data, relocs, offset)
        if not decoded: continue
        width, height, rgba = decoded
        relative = f"css/portraits/{key}.png"
        write_png(portraits / f"{key}.png", width, height, rgba)
        rows.append([key, relative])
    out = fighters / "remix_css.tsv"
    with out.open("w", newline="") as f:
        f.write("SAGAS-DATA\t1\n")
        writer = csv.writer(f, delimiter="\t", lineterminator="\n")
        writer.writerow(["key", "portrait"])
        writer.writerows(rows)
    print(f"Wrote {len(rows)} CSS portraits to {out}")
    return rows

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    workspace = Path(__file__).resolve().parents[2]
    parser.add_argument("--source", type=Path, default=workspace / "smashremix")
    parser.add_argument("--resources", type=Path, default=workspace / "sagas/assets/remix")
    parser.add_argument("--fighters", type=Path, default=workspace / "sagas/assets/fighters")
    args = parser.parse_args()
    extract(args.source, args.resources, args.fighters)
