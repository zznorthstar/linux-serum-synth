"""Compare private Serum/ZYG renders without phase or loudness matching.

Usage: python tools/compare_renders.py serum.wav zyg.wav [--output report.json]
Requires the project's `oracle` extra. Reports measurements, not compatibility.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import soundfile as sf


def measure(path):
    info = sf.info(path)
    audio, rate = sf.read(path, always_2d=True, dtype="float64")
    if not audio.size or not np.isfinite(audio).all():
        raise ValueError(f"Empty or non-finite audio: {path}")
    rms = float(np.sqrt(np.mean(audio * audio)))
    width = max(1, round(rate * 0.01))
    envelope = [float(np.sqrt(np.mean(audio[i:i + width] ** 2)))
                for i in range(0, len(audio), width)]
    mono = audio.mean(axis=1)
    spectrum = np.abs(np.fft.rfft(mono * np.hanning(len(mono)))) ** 2
    frequencies = np.fft.rfftfreq(len(mono), 1 / rate)
    total = float(spectrum.sum())
    return {
        "sample_rate": rate, "channels": audio.shape[1], "frames": len(audio),
        "encoding": info.subtype, "samples_over_full_scale": int(np.count_nonzero(np.abs(audio) > 1.0)),
        "seconds": len(audio) / rate, "peak": float(np.max(np.abs(audio))),
        "rms": rms, "rms_dbfs": 20 * np.log10(max(rms, 1e-12)),
        "spectral_centroid_hz": float(np.dot(frequencies, spectrum) / total) if total else 0.0,
        "rms_10ms": envelope,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    reference, candidate = measure(args.reference), measure(args.candidate)
    if reference["sample_rate"] != candidate["sample_rate"]:
        raise ValueError("Render both instruments at the same sample rate")
    report = {
        "method": "unaligned, no level matching; fixed MIDI/tempo/settings must be verified separately",
        "reference": reference, "candidate": candidate,
        "rms_difference_db": candidate["rms_dbfs"] - reference["rms_dbfs"],
        "duration_difference_seconds": candidate["seconds"] - reference["seconds"],
    }
    result = json.dumps(report, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
