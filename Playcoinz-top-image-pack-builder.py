#!/usr/bin/env python3
import argparse
import hashlib
import struct
from pathlib import Path

MAX_IMAGE_SIZE = 0x10000

PACKS = (
    ("easy", 5, "easytop.bin"),
    ("medium", 5, "mediumtop.bin"),
    ("hard", 5, "hardtop.bin"),
    ("extreme", 3, "extremtop.bin"),
)

# Start-of-frame markers that carry dimensions/component sampling data.
SOF_MARKERS = {
    0xC0, 0xC1, 0xC2, 0xC3,
    0xC5, 0xC6, 0xC7,
    0xC9, 0xCA, 0xCB,
    0xCD, 0xCE, 0xCF,
}

# Markers with no length field.
STANDALONE_MARKERS = {0x01, *range(0xD0, 0xD9)}


def jpeg_info(data: bytes, source: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    """Return (width, height, [(component_id, h_sampling, v_sampling), ...])."""
    if len(data) < 4 or data[:2] != b"\xFF\xD8":
        raise RuntimeError(f"{source.name}: not a JPEG (missing SOI marker)")

    pos = 2
    while pos < len(data):
        if data[pos] != 0xFF:
            # Entropy-coded scan data is irrelevant here. All metadata we need must
            # appear before SOS, so unexpected raw data before finding SOF is invalid.
            raise RuntimeError(f"{source.name}: malformed JPEG marker stream")

        while pos < len(data) and data[pos] == 0xFF:
            pos += 1
        if pos >= len(data):
            break

        marker = data[pos]
        pos += 1

        if marker == 0x00:
            continue
        if marker in STANDALONE_MARKERS:
            if marker == 0xD9:
                break
            continue
        if marker == 0xDA:  # SOS: SOF must have appeared earlier.
            break

        if pos + 2 > len(data):
            raise RuntimeError(f"{source.name}: truncated JPEG segment length")
        seg_len = struct.unpack_from(">H", data, pos)[0]
        if seg_len < 2 or pos + seg_len > len(data):
            raise RuntimeError(f"{source.name}: invalid JPEG segment length")

        payload = data[pos + 2: pos + seg_len]
        if marker in SOF_MARKERS:
            if len(payload) < 6:
                raise RuntimeError(f"{source.name}: truncated SOF segment")
            height = struct.unpack_from(">H", payload, 1)[0]
            width = struct.unpack_from(">H", payload, 3)[0]
            component_count = payload[5]
            expected = 6 + component_count * 3
            if component_count == 0 or len(payload) < expected:
                raise RuntimeError(f"{source.name}: malformed SOF component table")

            components = []
            off = 6
            for _ in range(component_count):
                component_id = payload[off]
                sampling = payload[off + 1]
                h_sampling = sampling >> 4
                v_sampling = sampling & 0x0F
                components.append((component_id, h_sampling, v_sampling))
                off += 3
            return width, height, components

        pos += seg_len

    raise RuntimeError(f"{source.name}: JPEG has no SOF marker before scan data")


def validate_jpeg(source: Path) -> tuple[bytes, tuple[int, int]]:
    if not source.is_file():
        raise FileNotFoundError(f"missing required image: {source}")

    data = source.read_bytes()
    if len(data) > MAX_IMAGE_SIZE:
        raise RuntimeError(
            f"{source.name}: {len(data)} bytes exceeds the 64 KiB image buffer"
        )

    width, height, components = jpeg_info(data, source)
    if len(components) != 3:
        raise RuntimeError(
            f"{source.name}: expected 3 JPEG colour components, got {len(components)}"
        )

    # 4:4:4 means every component has 1x1 sampling. This is readable directly from
    # the JPEG SOF header, so normal builds need no image-decoding library.
    bad = [(cid, h, v) for cid, h, v in components if (h, v) != (1, 1)]
    if bad:
        sampling_text = ", ".join(f"component {cid}={h}x{v}" for cid, h, v in components)
        raise RuntimeError(
            f"{source.name}: expected JPEG 4:4:4 sampling; found {sampling_text}"
        )

    return data, (width, height)


def build_pack(images: list[bytes]) -> bytes:
    count = len(images)
    # Current 3NX format:
    #   u32 imageCount
    #   u32 imageSizes[imageCount]
    #   u8  jpegData[]
    header = struct.pack("<I", count)
    header += struct.pack(f"<{count}I", *(len(image) for image in images))
    return header + b"".join(images)


def build_all(input_dir: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    for prefix, count, output_name in PACKS:
        images: list[bytes] = []
        for index in range(1, count + 1):
            jpeg_path = input_dir / f"{prefix}{index}.jpg"
            data, dimensions = validate_jpeg(jpeg_path)
            images.append(data)
            print(
                f"{jpeg_path.name}: {dimensions[0]}x{dimensions[1]}, "
                f"4:4:4, {len(data)} bytes"
            )

        blob = build_pack(images)
        output_path = output_dir / output_name
        output_path.write_bytes(blob)
        print(f"Wrote {output_path} ({len(blob)} bytes, {count} images)")
        print(f"SHA-256: {hashlib.sha256(blob).hexdigest()}")


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description=(
            "Validate committed Playcoinz 4:4:4 JPEG assets and build the four "
            "top-screen image packs. Uses only the Python standard library."
        )
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=script_dir / "achvimages",
        help="directory containing easy1.jpg..extreme3.jpg",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=script_dir,
        help="directory for the four .bin packs; default: beside this script",
    )
    args = parser.parse_args()
    build_all(args.input_dir, args.output_dir)


if __name__ == "__main__":
    main()
