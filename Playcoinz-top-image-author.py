#!/usr/bin/env python3
"""Optional authoring tool for regenerating committed Playcoinz JPEG assets.

This is NOT part of the normal Nexus/sysplugin build. pre_makeplugin.sh and
makeplugin.sh use the committed JPEGs and require only the Python standard
library. Run this only when one of the editable PNG masters is changed.
"""
import argparse
from io import BytesIO
from pathlib import Path

try:
    from PIL import Image, ImageFile, JpegImagePlugin
except ImportError as exc:
    raise SystemExit(
        "ERROR: Pillow is required only to regenerate Playcoinz JPEG assets from "
        "the editable PNG masters. The normal pre_makeplugin.sh / makeplugin.sh "
        "build does not require Pillow."
    ) from exc

MAX_IMAGE_SIZE = 0x10000
JPEG_SUBSAMPLING_444 = 0

PACKS = (
    ("easy", 5),
    ("medium", 5),
    ("hard", 5),
    ("extreme", 3),
)


def encode_jpeg_444_under_limit(source: Path) -> tuple[bytes, int, tuple[int, int]]:
    if not source.is_file():
        raise FileNotFoundError(f"missing required image: {source}")

    with Image.open(source) as opened:
        image = opened.convert("RGB")
        size = image.size

    ImageFile.MAXBLOCK = max(ImageFile.MAXBLOCK, image.width * image.height * 8)

    for quality in range(100, 0, -1):
        encoded = BytesIO()
        image.save(
            encoded,
            format="JPEG",
            quality=quality,
            subsampling=JPEG_SUBSAMPLING_444,
            optimize=True,
            progressive=False,
        )
        data = encoded.getvalue()
        if len(data) > MAX_IMAGE_SIZE:
            continue

        with Image.open(BytesIO(data)) as check:
            sampling = JpegImagePlugin.get_sampling(check)
            if sampling != JPEG_SUBSAMPLING_444:
                raise RuntimeError(
                    f"{source.name}: JPEG encoder produced subsampling={sampling}, expected 4:4:4"
                )
            if check.size != size:
                raise RuntimeError(
                    f"{source.name}: JPEG dimensions changed from {size} to {check.size}"
                )

        return data, quality, size

    raise RuntimeError(
        f"{source.name}: could not fit a 4:4:4 JPEG within {MAX_IMAGE_SIZE} bytes "
        "even at JPEG quality 1"
    )


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description=(
            "Regenerate committed Playcoinz 4:4:4 JPEG assets from editable PNG "
            "masters. Optional authoring step; not used by normal builds."
        )
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=script_dir / "achvimages-src",
        help="directory containing easy1.png..extreme3.png",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=script_dir / "achvimages",
        help="directory for committed easy1.jpg..extreme3.jpg assets",
    )
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    for prefix, count in PACKS:
        for index in range(1, count + 1):
            png_path = args.input_dir / f"{prefix}{index}.png"
            jpeg_path = args.output_dir / f"{prefix}{index}.jpg"
            data, quality, dimensions = encode_jpeg_444_under_limit(png_path)
            jpeg_path.write_bytes(data)
            print(
                f"{png_path.name} -> {jpeg_path.name}: "
                f"{dimensions[0]}x{dimensions[1]}, 4:4:4, quality {quality}, {len(data)} bytes"
            )


if __name__ == "__main__":
    main()
