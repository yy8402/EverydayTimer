#!/usr/bin/env python3
"""Generate the Mandarin voice bank with macOS say and ffmpeg."""
from pathlib import Path
import struct
import subprocess
import tempfile

WORDS = """
零 一 二 三 四 五 六 七 八 九 十 百 千 万 亿
小时 分钟 分 秒 三十秒 一分钟 一分三十秒
""".split()
out = Path(__file__).resolve().parents[1] / "main" / "voice.bin"
clips = []
with tempfile.TemporaryDirectory() as temp:
    source = Path(temp) / "word.aiff"
    for word in WORDS:
        subprocess.run(["say", "-v", "Tingting", "-r", "175",
                        "-o", str(source), word], check=True)
        raw = subprocess.check_output([
            "ffmpeg", "-v", "error", "-i", str(source),
            "-af", "silenceremove=start_periods=1:start_threshold=-42dB:stop_periods=1:stop_duration=0.08:stop_threshold=-42dB",
            "-ar", "8000", "-ac", "1", "-f", "s16le", "-"
        ])
        samples = [sample for (sample,) in struct.iter_unpack("<h", raw)]
        peak = max(map(abs, samples))
        clips.append(bytes(128 + round(sample * 110 / peak) for sample in samples))
        source.unlink()
offsets = [4 * (len(clips) + 1)]
for clip in clips:
    offsets.append(offsets[-1] + len(clip))
out.write_bytes(struct.pack(f"<{len(offsets)}I", *offsets) + b"".join(clips))
print(f"{len(clips)} words, {out.stat().st_size} bytes")
