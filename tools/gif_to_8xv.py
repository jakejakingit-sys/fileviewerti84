#!/usr/bin/env python3
"""Pack an animated GIF into a FILEVIEW FVGF AppVar (.8xv). Requires Pillow."""
import argparse
import re
from pathlib import Path

from PIL import Image, ImageSequence

MAX_W, MAX_H, MAX_FRAMES = 64, 48, 20


def appvar_8xv(name: str, payload: bytes) -> bytes:
    name = re.sub(r"[^A-Z0-9]", "", name.upper())[:8]
    if not name:
        name = "FVANIM"
    entry_len = len(payload) + 2
    # Archived AppVar entry: length, type 0x15, name, version, archived flag,
    # repeated data length, then the AppVar's own length word and payload.
    entry = bytearray()
    entry += (0x000D).to_bytes(2, "little")
    entry += entry_len.to_bytes(2, "little")
    entry.append(0x15)
    entry += name.encode("ascii").ljust(8, b"\0")
    entry += bytes((0, 0x80))
    entry += entry_len.to_bytes(2, "little")
    entry += len(payload).to_bytes(2, "little")
    entry += payload
    comment = b"FILEVIEW GIF FRAME PACK".ljust(42, b"\0")
    header = b"**TI83F*" + bytes((0x1A, 0x0A, 0)) + comment
    body = bytes(entry)
    return header + len(body).to_bytes(2, "little") + body + (sum(body) & 0xFFFF).to_bytes(2, "little")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gif", type=Path)
    parser.add_argument("--name", help="calculator AppVar name (1–8 letters/numbers)")
    parser.add_argument("--delay", type=int, default=5, help="playback delay in 1/10-second units (1–25)")
    args = parser.parse_args()
    delay = max(1, min(25, args.delay))
    name = args.name or args.gif.stem

    frames = []
    with Image.open(args.gif) as gif:
        total = getattr(gif, "n_frames", 1)
        picks = sorted(set(round(i * (total - 1) / min(total, MAX_FRAMES - 1)) for i in range(min(total, MAX_FRAMES)))) if total > 1 else [0]
        for index in picks[:MAX_FRAMES]:
            gif.seek(index)
            frame = gif.convert("RGB")
            frame.thumbnail((MAX_W, MAX_H), Image.Resampling.LANCZOS)
            canvas = Image.new("RGB", (MAX_W, MAX_H), (0, 0, 0))
            canvas.paste(frame, ((MAX_W - frame.width) // 2, (MAX_H - frame.height) // 2))
            packed = bytearray()
            for red, green, blue in canvas.getdata():
                packed.append(((red >> 5) << 5) | ((green >> 5) << 2) | (blue >> 6))
            frames.append(bytes(packed))

    # FVGF + width + height + frame count + delay + 2 reserved bytes.
    payload = b"FVGF" + bytes((MAX_W, MAX_H, len(frames), delay, 0, 0)) + b"".join(frames)
    if len(payload) + 19 > 65535:
        raise SystemExit("Frame pack exceeds the TI AppVar size limit")
    out = args.gif.with_name(name.upper()[:8] + ".8xv")
    out.write_bytes(appvar_8xv(name, payload))
    print(f"Created {out} ({len(frames)} frames, {len(payload)} AppVar bytes)")


if __name__ == "__main__":
    main()
