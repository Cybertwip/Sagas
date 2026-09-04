#!/usr/bin/env python3
"""Compile the editable scene resource table into a bounded runtime binary."""

import argparse
import csv
import pathlib
import struct


KINDS = {"model": 0, "display": 1, "fighter": 2, "transition": 3}
LAYOUTS = {"direct": 0, "links": 1, "pairs": 2}
WRAPPERS = {"none": 0, "transn": 1, "xrotn": 2}


def mask(value: str | None, default: int) -> int:
    if value is None or not value.strip() or value == "-":
        return default
    return int(value, 0)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--sequence", required=True, type=pathlib.Path)
    parser.add_argument("--cues", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    with args.input.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if not rows:
        raise SystemExit("scene table has no resources")
    with args.sequence.open(newline="", encoding="utf-8") as stream:
        segments = list(csv.DictReader(stream, delimiter="\t"))
    with args.cues.open(newline="", encoding="utf-8") as stream:
        cues = list(csv.DictReader(stream, delimiter="\t"))

    bundles: dict[str, list[dict[str, str]]] = {}
    keys: set[str] = set()
    for row in rows:
        if row["key"] in keys:
            raise SystemExit(f"duplicate resource key: {row['key']}")
        keys.add(row["key"])
        bundles.setdefault(row["bundle"], []).append(row)
        if row["kind"] not in KINDS or row["layout"] not in LAYOUTS or row["wrapper"] not in WRAPPERS:
            raise SystemExit(f"invalid enum in resource {row['key']}")
        if row["lighting"] not in ("lit", "unlit", "spot"):
            raise SystemExit(f"invalid lighting mode in resource {row['key']}")

    strings = bytearray(b"\0")
    offsets: dict[str, int] = {"": 0, "-": 0}

    def string_offset(value: str) -> int:
        if value not in offsets:
            offsets[value] = len(strings)
            strings.extend(value.encode("utf-8") + b"\0")
        return offsets[value]

    bundle_records = bytearray()
    resource_records = bytearray()
    segment_records = bytearray()
    cue_records = bytearray()
    first = 0
    for bundle, resources in bundles.items():
        bundle_records.extend(struct.pack("<III", string_offset(bundle), first, len(resources)))
        first += len(resources)
        for row in resources:
            flags = 0
            if row["lighting"] in ("unlit", "spot"):
                flags |= 1
            if row["lighting"] == "spot":
                flags |= 2
            resource_records.extend(struct.pack(
                "<IIIIIIBBBB Iff fff II",
                string_offset(row["key"]), string_offset(row["descriptor"]),
                string_offset(row["animation"]), string_offset(row["materials"]),
                string_offset(row["material_animation"]), string_offset(row["dependency"]),
                KINDS[row["kind"]], LAYOUTS[row["layout"]], WRAPPERS[row["wrapper"]], flags,
                int(row["animation_file"]), float(row["transition_frame"]), float(row["material_start"]),
                float(row["position_x"]), float(row["position_y"]), float(row["position_z"]),
                mask(row.get("setup_flags0"), 0xffffffff),
                mask(row.get("setup_flags1"), 0xffffffff)))

    segment_names = {row["name"] for row in segments}
    for row in segments:
        color = (int(row["red"]) | (int(row["green"]) << 8) |
                 (int(row["blue"]) << 16) | (int(row["alpha"]) << 24))
        segment_records.extend(struct.pack(
            "<IIIIIIffI", string_offset(row["name"]), string_offset(row["renderer"]),
            string_offset(row["argument"]), string_offset(row["bundle"]), int(row["duration"]),
            int(row["preload_lead"]), float(row["scale_x"]), float(row["scale_y"]), color))
    for row in cues:
        if row["segment"] not in segment_names:
            raise SystemExit(f"render cue references unknown segment: {row['segment']}")
        color = (int(row["red"]) | (int(row["green"]) << 8) |
                 (int(row["blue"]) << 16) | (int(row["alpha"]) << 24))
        cue_records.extend(struct.pack(
            "<IIIII", string_offset(row["segment"]), string_offset(row["resource"]),
            string_offset(row["camera"]), int(row["order"]), color))

    header = struct.pack("<4sHHIIIII", b"SGSC", 3, 0, len(bundles), len(rows),
                         len(segments), len(cues), len(strings))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + bundle_records + resource_records + segment_records + cue_records + strings)


if __name__ == "__main__":
    main()
