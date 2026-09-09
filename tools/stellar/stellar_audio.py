"""WAV processing used by Stellar export and desktop previews."""

from __future__ import annotations

import array
import math
from pathlib import Path
import sys
import wave
from typing import Any, Iterable


def _pcm_to_float(samples: array.array, width: int) -> list[float]:
    if width == 1:
        return [(value - 128) / 128.0 for value in samples]
    scale = float(1 << (width * 8 - 1))
    return [value / scale for value in samples]


def read_wav_mono(path: Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        width = source.getsampwidth()
        rate = source.getframerate()
        frames = source.getnframes()
        raw = source.readframes(frames)
    if width not in (1, 2, 4):
        raise ValueError(f"Unsupported WAV sample width: {width * 8} bits")
    values = array.array({1: "B", 2: "h", 4: "i"}[width])
    values.frombytes(raw)
    if sys.byteorder != "little" and width > 1:
        values.byteswap()
    samples = _pcm_to_float(values, width)
    if channels > 1:
        samples = [sum(samples[index:index + channels]) / channels
                   for index in range(0, len(samples), channels)]
    return rate, samples


def write_wav_mono(path: Path, rate: int, samples: Iterable[float]) -> None:
    values = array.array("h", (
        max(-32768, min(32767, int(value * 32767.0))) for value in samples
    ))
    if sys.byteorder != "little":
        values.byteswap()
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as target:
        target.setnchannels(1)
        target.setsampwidth(2)
        target.setframerate(rate)
        target.writeframes(values.tobytes())


def process_sound(source: Path, target: Path, asset: Any,
                  global_pitch_semitones: float = 0.0) -> dict[str, Any]:
    rate, samples = read_wav_mono(source)
    start = max(0, int(asset.trim_start * rate))
    end = len(samples) - max(0, int(asset.trim_end * rate))
    samples = samples[start:max(start, end)]
    gain = 10.0 ** (asset.gain_db / 20.0)
    samples = [value * gain for value in samples]
    effective_pitch = (0.0 if asset.kind.casefold() == "announcer" else
                       asset.pitch_semitones + global_pitch_semitones)
    ratio = 2.0 ** (effective_pitch / 12.0)
    if abs(ratio - 1.0) > 1e-7 and samples:
        output_len = max(1, int(len(samples) / ratio))
        resampled: list[float] = []
        for index in range(output_len):
            position = index * ratio
            left = min(len(samples) - 1, int(position))
            right = min(len(samples) - 1, left + 1)
            fraction = position - left
            resampled.append(samples[left] * (1.0 - fraction) + samples[right] * fraction)
        samples = resampled
    write_wav_mono(target, rate, samples)
    pitch = estimate_pitch(samples, rate)
    return {
        "sample_rate": rate, "samples": len(samples), "duration": len(samples) / rate,
        "pitch_hz": pitch, "pitch_note": pitch_note(pitch),
        "peak": max(map(abs, samples), default=0.0),
        "pitch_semitones": effective_pitch,
    }


def estimate_pitch(samples: list[float], rate: int) -> float:
    if not samples:
        return 0.0
    window_len = min(len(samples), max(512, int(rate * 0.08)))
    hop = max(1, window_len // 4)
    best_start = max(range(0, max(1, len(samples) - window_len + 1), hop),
                     key=lambda index: sum(value * value for value in samples[index:index + window_len]))
    window = samples[best_start:best_start + window_len]
    mean = sum(window) / len(window)
    window = [value - mean for value in window]
    min_lag = max(1, rate // 500)
    max_lag = min(len(window) - 2, rate // 55)
    if max_lag <= min_lag:
        return 0.0
    if sum(value * value for value in window) < 1e-8:
        return 0.0
    correlations: list[tuple[int, float]] = []
    for lag in range(min_lag, max_lag + 1):
        numerator = sum(window[index] * window[index + lag]
                        for index in range(len(window) - lag))
        denominator = math.sqrt(
            sum(window[index] ** 2 for index in range(len(window) - lag)) *
            sum(window[index + lag] ** 2 for index in range(len(window) - lag)))
        correlations.append((lag, numerator / denominator if denominator else 0.0))
    best_corr = max((correlation for _lag, correlation in correlations), default=0.0)
    threshold = max(0.2, best_corr * 0.96)
    best_lag = next((lag for lag, correlation in correlations if correlation >= threshold), 0)
    return rate / best_lag if best_lag and best_corr >= 0.2 else 0.0


def pitch_note(frequency: float) -> str:
    if frequency <= 0:
        return "—"
    midi = 69 + 12 * math.log2(frequency / 440.0)
    nearest = round(midi)
    names = ("C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B")
    cents = round((midi - nearest) * 100)
    return f"{names[nearest % 12]}{nearest // 12 - 1} {cents:+d}¢"
