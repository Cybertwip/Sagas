#!/usr/bin/env python3
"""Compile extracted CSEQ + ALBank JSON into Sagas' external music format."""

import argparse
import importlib.util
import json
import struct
from pathlib import Path


def module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--parser", required=True, type=Path)
    parser.add_argument("--bank", required=True, type=Path)
    parser.add_argument("--sequence", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    generator = module(args.generator, "sagas_music_generator")
    cseq = generator.load_cseq_parser(args.parser)
    division, tempo, tracks, programs = generator.parse_sequence(args.sequence, cseq)
    bank_json = json.loads(args.bank.read_text())
    structs = {entry["offset"]: entry for entry in bank_json["structs"]}
    bank = next(entry for entry in structs.values() if entry["kind"] == "ALBank")
    sounds = generator.collect_sounds(programs, bank, structs)

    output = bytearray(b"SGM1")
    output += struct.pack("<IIII", division, tempo, len(sounds), len(tracks))
    for sound in sounds:
        output += struct.pack("<17i", *sound)
    for track_id, loop_start, loop_end, events in tracks:
        output += struct.pack("<IIII", track_id, loop_start, loop_end, len(events))
        for tick, kind, channel, a, b in events:
            output += struct.pack("<IBBBB", tick, kind, channel, a, b)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"Packed opening CSEQ: {len(tracks)} tracks, {len(sounds)} sounds, {len(output)} bytes")


if __name__ == "__main__":
    main()

