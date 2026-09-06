#!/usr/bin/env python3
"""Bake an SGM2 sequence to interleaved PCM so boot never synthesizes a song."""

import argparse
import array
import math
import pathlib
import struct
import sys


def aiff(path: pathlib.Path) -> tuple[int, list[int]]:
    data = path.read_bytes()
    if data[:4] != b"FORM" or data[8:12] not in (b"AIFF", b"AIFC"):
        raise RuntimeError(f"not an AIFF PCM asset: {path}")
    channels = bits = rate = frames = 0
    sound = b""
    at = 12
    while at + 8 <= len(data):
        size = struct.unpack_from(">I", data, at + 4)[0]
        body = at + 8
        if body + size > len(data):
            break
        if data[at:at + 4] == b"COMM" and size >= 18:
            channels, frames, bits = struct.unpack_from(">hIh", data, body)
            exponent = struct.unpack_from(">H", data, body + 8)[0]
            mantissa = int.from_bytes(data[body + 10:body + 18], "big")
            rate = round(math.ldexp(float(mantissa), (exponent & 0x7FFF) - 16383 - 63))
        elif data[at:at + 4] == b"SSND" and size >= 8:
            offset = struct.unpack_from(">I", data, body)[0]
            sound = data[body + 8 + offset:body + size]
        at = body + size + (size & 1)
    if channels < 1 or bits != 16 or rate <= 0 or not sound:
        raise RuntimeError(f"unsupported AIFF layout: {path}")
    raw = struct.unpack(f">{min(frames * channels, len(sound) // 2)}h", sound[:frames * channels * 2])
    mono = [sum(raw[i:i + channels]) // channels for i in range(0, len(raw), channels)]
    return rate, mono


def load_sgm(path: pathlib.Path, looping=False):
    data = path.read_bytes()
    if data[:4] != b"SGM2":
        raise RuntimeError("music input is not SGM2")
    division, initial_tempo, sound_count, track_count = struct.unpack_from("<IIII", data, 4)
    at = 20
    sounds = []
    for _ in range(sound_count):
        sounds.append(struct.unpack_from("<20i", data, at))
        at += 80
    events = []
    tracks = []
    for _ in range(track_count):
        _, loop_start, loop_end, count = struct.unpack_from("<IIII", data, at)
        track_events = []
        at += 16
        for _ in range(count):
            tick, kind, channel, a, b = struct.unpack_from("<IBBBB", data, at)
            track_events.append([tick, 0, kind, channel, a, b])
            at += 8
        tracks.append((loop_start, loop_end, track_events))
    loop_ticks = None
    if looping:
        lengths = {end-start for start,end,_ in tracks if end>start}
        if len(lengths)!=1:
            raise RuntimeError("PCM looping requires a common track loop period")
        period = lengths.pop()
        begin = max(end for _,end,_ in tracks)
        loop_ticks = (begin, begin+period)
    for start,end,track_events in tracks:
        events.extend(track_events)
        if loop_ticks and end>start:
            for event in track_events:
                if start<=event[0]<end:
                    tick=event[0]+end-start
                    while tick<loop_ticks[1]:
                        events.append([tick,*event[1:]])
                        tick+=end-start
    if loop_ticks:
        events.extend([[tick,0,255,0,0,0] for tick in loop_ticks])
    events.sort(key=lambda event: event[0])
    frame = 0.0
    previous = 0
    tempo = initial_tempo
    index = 0
    while index < len(events):
        tick = events[index][0]
        frame += (tick - previous) * tempo * 32000.0 / (division * 1_000_000.0)
        end = index
        while end < len(events) and events[end][0] == tick:
            events[end][1] = round(frame)
            if events[end][2] == 5:
                tempo = (events[end][3] << 16) | (events[end][4] << 8) | events[end][5]
            end += 1
        previous = tick
        index = end
    loop_frames = tuple(event[1] for event in events if event[2]==255)
    return sounds, events, loop_frames


def render(sgm: pathlib.Path, wave_root: pathlib.Path, gain: float, looping=False):
    sounds, events, loop_frames = load_sgm(sgm, looping)
    channels = [{"program": 0, "volume": 127, "pan": 64, "bend": 8192,
                 "bend_range": 200, "sustain": False} for _ in range(16)]
    base = next((sound for sound in sounds if sound[0] == 0), None)
    if base:
        for channel in channels:
            channel["pan"], channel["bend_range"] = base[10], base[12]
    waves: dict[int, tuple[int, list[int]]] = {}
    voices = []
    output = array.array("h")
    final_frame = (events[-1][1] if events else 0) + 160000
    event_index = 0
    master_volume = 127

    for frame in range(final_frame):
        while event_index < len(events) and events[event_index][1] <= frame:
            _, _, kind, channel_index, a, b = events[event_index]
            event_index += 1
            channel_index &= 15
            channel = channels[channel_index]
            if kind == 2:
                channel["program"] = a
                selected = next((sound for sound in sounds if sound[0] == a), None)
                if selected:
                    channel["bend_range"] = selected[12]
            elif kind == 3:
                if a == 7:
                    channel["volume"] = b
                elif a == 10:
                    channel["pan"] = b
                elif a == 20:
                    channel["bend_range"] = 1200 if b >= 121 else b * 10
                elif a == 21:
                    master_volume = b
                elif a == 64:
                    channel["sustain"] = b > 63
                    if not channel["sustain"]:
                        for voice in voices:
                            if voice["channel"] == channel_index and voice["sustained"]:
                                voice["sustained"] = False
                                voice["released"] = True
                                voice["release_age"] = 0
            elif kind == 4:
                channel["bend"] = a | (b << 7)
            elif kind == 1 or (kind == 0 and b == 0):
                voice = next((voice for voice in voices if voice["channel"] == channel_index and
                              voice["note"] == a and not voice["released"] and not voice["sustained"]), None)
                if voice:
                    if channel["sustain"]:
                        voice["sustained"] = True
                    else:
                        voice["released"] = True
                        voice["release_age"] = 0
            elif kind == 0:
                sound = next((item for item in sounds if item[0] == channel["program"] and
                              item[3] <= a <= item[4] and item[1] <= b <= item[2]), None)
                if sound:
                    wave_id = sound[7]
                    if wave_id not in waves:
                        waves[wave_id] = aiff(wave_root / f"wave_{wave_id:03}.aiff")
                    wave_rate, samples = waves[wave_id]
                    cents = (a - sound[5]) * 100.0 + sound[6]
                    voices.append({"sound": sound, "samples": samples, "channel": channel_index,
                                   "note": a, "velocity": b, "position": 0.0,
                                   "step": 2.0 ** (cents / 1200.0) * wave_rate / 32000.0,
                                   "age": 0, "release_age": 0, "released": False, "sustained": False})

        left = right = 0.0
        for voice in voices:
            samples = voice["samples"]
            position = voice["position"]
            if not samples or position >= len(samples):
                voice["samples"] = None
                continue
            sound = voice["sound"]
            channel = channels[voice["channel"]]
            age_us = voice["age"] * (1_000_000.0 / 32000.0)
            envelope = sound[17] / 127.0
            if sound[13] > 0 and age_us < sound[13]:
                envelope = age_us / sound[13] * sound[16] / 127.0
            elif sound[14] > 0 and age_us < sound[13] + sound[14]:
                mix = (age_us - sound[13]) / sound[14]
                envelope = (sound[16] + (sound[17] - sound[16]) * mix) / 127.0
            if voice["released"]:
                release_us = voice["release_age"] * (1_000_000.0 / 32000.0)
                voice["release_age"] += 1
                envelope *= max(0.0, 1.0 - release_us / sound[15]) if sound[15] > 0 else 0.0
                if envelope <= 0:
                    voice["samples"] = None
                    continue
            index = int(position)
            following = min(index + 1, len(samples) - 1)
            fraction = position - index
            sample = samples[index] * (1.0 - fraction) + samples[following] * fraction
            level = (gain * voice["velocity"] / 127.0 * channel["volume"] / 127.0 *
                     master_volume / 127.0 * sound[8] / 127.0 * sound[9] / 127.0 * envelope)
            pan = min(127, max(0, channel["pan"] - 64 + sound[11]))
            angle = pan * (math.pi / 2.0 / 127.0)
            left += sample * level * math.cos(angle)
            right += sample * level * math.sin(angle)
            bend_cents = (channel["bend"] - 8192) * (channel["bend_range"] / 8192.0)
            voice["position"] += voice["step"] * 2.0 ** (bend_cents / 1200.0)
            voice["age"] += 1
            if not voice["released"] and sound[19] > sound[18] and voice["position"] >= sound[19]:
                voice["position"] = sound[18] + (voice["position"] - sound[18]) % (sound[19] - sound[18])
        if frame % 4096 == 0:
            voices = [voice for voice in voices if voice["samples"] is not None]
        output.append(round(min(32767, max(-32768, left))))
        output.append(round(min(32767, max(-32768, right))))
    return output, loop_frames


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--wave-root", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--gain", type=float, default=1.0)
    parser.add_argument("--loop", action="store_true")
    args = parser.parse_args()
    samples, loop_frames = render(args.input, args.wave_root, args.gain, args.loop)
    if sys.byteorder != "little":
        samples.byteswap()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(struct.pack("<4sIII", b"SGPC", 32000, 2, len(samples)) + samples.tobytes())
    if loop_frames:
        with args.output.open("ab") as stream:
            stream.write(struct.pack("<4sII",b"LOOP",*loop_frames))
    print(f"Baked PCM: {len(samples) // 2} stereo frames")


if __name__ == "__main__":
    main()
