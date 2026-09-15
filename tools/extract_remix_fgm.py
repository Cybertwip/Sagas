"""Convert Smash Remix add_sound AIFC samples into Sagas SGPCM FGM overlays."""
import argparse
import re
import struct
import sys
from pathlib import Path

ORIGINAL_FGM_COUNT = 0x2B7
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "ssb-decomp-re/tools"))
from audio_codec import adpcm_decode, read_aifc  # noqa: E402


def pack_sgpcm(rate, samples):
    pcm = [max(-32768, min(32767, int(s))) for s in samples]
    return b"SGPC" + struct.pack("<III", rate, 1, len(pcm)) + struct.pack("<" + "h" * len(pcm), *pcm)


def aifc_to_pcm(path):
    info = read_aifc(str(path))
    samples = adpcm_decode(info["adpcm_bytes"], info["codebook"], info["order"], info["npredictors"])
    rate = int(round(info["sample_rate"])) or 16000
    return rate, samples


def fgm_map(source):
    text = (source / "src/FGM.asm").read_text()
    fgm_num = 0
    rows = []
    for line in text.splitlines():
        if re.match(r"\s*add_sound\(", line) or re.match(r"\s*add_sound_advanced\(", line) or re.match(r"\s*add_fgm\(", line) or re.match(r"\s*reserve_fgm\(", line):
            fgm_num += 1
            fgm_id = ORIGINAL_FGM_COUNT - 1 + fgm_num
            match = re.search(r"add_sound(?:_advanced)?\(\s*([^,\s]+)", line)
            if match and "reserve_fgm" not in line and "add_fgm" not in line:
                name = match[1].strip()
                aifc = source / "src" / (name + ".aifc")
                if not aifc.is_file():
                    aifc = next((p for p in (source / "src").rglob(Path(name).name + ".aifc")
                                 if p.stem.lower() == Path(name).name.lower()), aifc)
                rows.append((fgm_id, aifc, name))
    return rows


def extract(source, output, ids=None):
    output.mkdir(parents=True, exist_ok=True)
    written = []
    for fgm_id, aifc, name in fgm_map(source):
        if ids is not None and fgm_id not in ids:
            continue
        if not aifc.is_file():
            continue
        rate, samples = aifc_to_pcm(aifc)
        dest = output / f"{fgm_id}.sgpcm"
        dest.write_bytes(pack_sgpcm(rate, samples))
        written.append((fgm_id, dest.name, name))
    (output / "index.tsv").write_text("id\tfile\tsource\n" + "".join(f"{i}\t{f}\t{n}\n" for i, f, n in written))
    print(f"Wrote {len(written)} remix FGM samples to {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    workspace = Path(__file__).resolve().parents[2]
    parser.add_argument("--source", type=Path, default=workspace / "smashremix")
    parser.add_argument("--output", type=Path, default=workspace / "sagas/assets/remix/fgm")
    parser.add_argument("--announcers-only", action="store_true")
    args = parser.parse_args()
    ids = None
    if args.announcers_only:
        announce = workspace / "sagas/assets/fighters/remix_announce.tsv"
        ids = {int(line.split("\t")[1]) for line in announce.read_text().splitlines()[2:] if "\t" in line}
    extract(args.source, args.output, ids)
