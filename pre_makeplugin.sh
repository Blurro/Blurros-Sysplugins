#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

# Independent installed-asset versions. Bump only the asset that changed.
ICN_VERSION=1
ACHV_VERSION=1
EASYTOP_VERSION=1
MEDIUMTOP_VERSION=1
HARDTOP_VERSION=1
EXTREMTOP_VERSION=1

ACHV_BUILDER="$SCRIPT_DIR/Playcoinz-achv-bin-builder.py"
TOP_IMAGE_BUILDER="$SCRIPT_DIR/Playcoinz-top-image-pack-builder.py"
ACHV_IMAGE_DIR="$SCRIPT_DIR/achvimages"

inputs=(
    "$SCRIPT_DIR/icn.bin"
    "$ACHV_BUILDER"
    "$TOP_IMAGE_BUILDER"
    "$ACHV_IMAGE_DIR/easy1.jpg"
    "$ACHV_IMAGE_DIR/easy2.jpg"
    "$ACHV_IMAGE_DIR/easy3.jpg"
    "$ACHV_IMAGE_DIR/easy4.jpg"
    "$ACHV_IMAGE_DIR/easy5.jpg"
    "$ACHV_IMAGE_DIR/medium1.jpg"
    "$ACHV_IMAGE_DIR/medium2.jpg"
    "$ACHV_IMAGE_DIR/medium3.jpg"
    "$ACHV_IMAGE_DIR/medium4.jpg"
    "$ACHV_IMAGE_DIR/medium5.jpg"
    "$ACHV_IMAGE_DIR/hard1.jpg"
    "$ACHV_IMAGE_DIR/hard2.jpg"
    "$ACHV_IMAGE_DIR/hard3.jpg"
    "$ACHV_IMAGE_DIR/hard4.jpg"
    "$ACHV_IMAGE_DIR/hard5.jpg"
    "$ACHV_IMAGE_DIR/extreme1.jpg"
    "$ACHV_IMAGE_DIR/extreme2.jpg"
    "$ACHV_IMAGE_DIR/extreme3.jpg"
)
for path in "${inputs[@]}"; do
    if [[ ! -e "$path" ]]; then
        printf 'ERROR: required pre-build input is missing: %s\n' "$path" >&2
        exit 1
    fi
done

printf 'Generating achv.bin...\n'
python3 "$ACHV_BUILDER" --output "$SCRIPT_DIR/achv.bin"

printf '\nValidating 4:4:4 achievement JPEGs and building top-image packs...\n'
python3 "$TOP_IMAGE_BUILDER" \
    --input-dir "$ACHV_IMAGE_DIR" \
    --output-dir "$SCRIPT_DIR"

required=(
    "$SCRIPT_DIR/icn.bin"
    "$SCRIPT_DIR/achv.bin"
    "$SCRIPT_DIR/easytop.bin"
    "$SCRIPT_DIR/mediumtop.bin"
    "$SCRIPT_DIR/hardtop.bin"
    "$SCRIPT_DIR/extremtop.bin"
)
for path in "${required[@]}"; do
    if [[ ! -f "$path" ]]; then
        printf 'ERROR: required pre-build input is missing: %s\n' "$path" >&2
        exit 1
    fi
done

python3 - \
    "$ICN_VERSION" "$ACHV_VERSION" "$EASYTOP_VERSION" \
    "$MEDIUMTOP_VERSION" "$HARDTOP_VERSION" "$EXTREMTOP_VERSION" <<'PY'
from collections import defaultdict, deque
from pathlib import Path
import struct
import sys

versions = list(map(int, sys.argv[1:]))
MAGIC = b"3NXV"
LZ10_TYPE = 0x10
WINDOW = 4096
MIN_MATCH = 3
MAX_MATCH = 18


def validate_raw(name, data, expected_count):
    if name == "icn.bin":
        if len(data) != 0x4800:
            raise SystemExit(f"ERROR: icn.bin must be exactly 0x4800 bytes, got 0x{len(data):X}")
        return

    if name == "achv.bin":
        if len(data) < 12:
            raise SystemExit("ERROR: achv.bin is truncated")
        magic, count, entry_size = struct.unpack_from("<4sII", data, 0)
        if magic != b"ACHV" or count != 18 or entry_size != 1092:
            raise SystemExit("ERROR: achv.bin must be ACHV / 18 entries / 1092-byte entries")
        if len(data) != 12 + count * entry_size:
            raise SystemExit("ERROR: achv.bin size does not match its header")
        return

    if len(data) < 4:
        raise SystemExit(f"ERROR: {name} is truncated")
    count = struct.unpack_from("<I", data, 0)[0]
    if count != expected_count:
        raise SystemExit(f"ERROR: {name} must contain {expected_count} images, got {count}")
    table_end = 4 + 4 * count
    if len(data) < table_end:
        raise SystemExit(f"ERROR: {name} size table is truncated")
    sizes = struct.unpack_from(f"<{count}I", data, 4)
    if any(size == 0 or size > 0x10000 for size in sizes):
        raise SystemExit(f"ERROR: {name} contains an invalid image size")
    if table_end + sum(sizes) != len(data):
        raise SystemExit(f"ERROR: {name} payload sizes do not match file size")


def compress_lz10(data):
    if len(data) > 0xFFFFFF:
        raise SystemExit("ERROR: LZ10 payload exceeds 24-bit size field")

    positions = defaultdict(deque)
    stream = bytearray()
    index = 0

    while index < len(data):
        flag_offset = len(stream)
        stream.append(0)
        flags = 0

        for slot in range(8):
            if index >= len(data):
                break

            best_length = 0
            best_distance = 0
            if index + MIN_MATCH <= len(data):
                key = data[index:index + MIN_MATCH]
                candidates = positions[key]
                while candidates and index - candidates[0] > WINDOW:
                    candidates.popleft()

                for position in reversed(list(candidates)[-64:]):
                    distance = index - position
                    maximum = min(MAX_MATCH, len(data) - index)
                    length = MIN_MATCH
                    while length < maximum and data[position + length] == data[index + length]:
                        length += 1
                    if length > best_length:
                        best_length = length
                        best_distance = distance
                        if length == maximum:
                            break

            if best_length >= MIN_MATCH:
                flags |= 0x80 >> slot
                displacement = best_distance - 1
                stream.append(((best_length - MIN_MATCH) << 4) | ((displacement >> 8) & 0x0F))
                stream.append(displacement & 0xFF)
                consumed = best_length
            else:
                stream.append(data[index])
                consumed = 1

            for step in range(consumed):
                position = index + step
                if position + MIN_MATCH <= len(data):
                    key = data[position:position + MIN_MATCH]
                    candidates = positions[key]
                    candidates.append(position)
                    while candidates and position - candidates[0] > WINDOW:
                        candidates.popleft()
            index += consumed

        stream[flag_offset] = flags

    size = len(data)
    return bytes((LZ10_TYPE, size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF)) + bytes(stream)


def decompress_lz10(blob):
    if len(blob) < 4 or blob[0] != LZ10_TYPE:
        raise SystemExit("ERROR: bad LZ10 header")
    expected_size = blob[1] | (blob[2] << 8) | (blob[3] << 16)
    cursor = 4
    output = bytearray()

    while len(output) < expected_size:
        if cursor >= len(blob):
            raise SystemExit("ERROR: compressed stream ended early")
        flags = blob[cursor]
        cursor += 1

        for slot in range(8):
            if len(output) >= expected_size:
                break
            if flags & (0x80 >> slot):
                if cursor + 2 > len(blob):
                    raise SystemExit("ERROR: truncated LZ10 match token")
                first = blob[cursor]
                second = blob[cursor + 1]
                cursor += 2
                length = (first >> 4) + MIN_MATCH
                distance = (((first & 0x0F) << 8) | second) + 1
                if distance > len(output):
                    raise SystemExit("ERROR: invalid LZ10 displacement")
                for _ in range(length):
                    if len(output) >= expected_size:
                        break
                    output.append(output[-distance])
            else:
                if cursor >= len(blob):
                    raise SystemExit("ERROR: truncated LZ10 literal")
                output.append(blob[cursor])
                cursor += 1

    if cursor != len(blob):
        raise SystemExit("ERROR: compressed LZ10 stream has trailing data")
    return bytes(output)


# Normal plugin metadata versions consumed directly by makeplugin.sh.
for version in (100, 101, 102, 103):
    Path(f"version{version}.bin").write_bytes(MAGIC + struct.pack("<I", version))

compressed_items = [
    ("icn.bin", "coinasset_icn.lz", versions[0], 4),
    ("achv.bin", "coinasset_achv.lz", versions[1], None),
]
for source_name, output_name, version, expected_count in compressed_items:
    raw = Path(source_name).read_bytes()
    validate_raw(source_name, raw, expected_count)
    installed = MAGIC + struct.pack("<I", version) + raw
    compressed = compress_lz10(installed)
    if decompress_lz10(compressed) != installed:
        raise SystemExit(f"ERROR: LZ10 round-trip failed for {source_name}")
    wrapper = MAGIC + struct.pack("<II", version, len(compressed)) + compressed
    Path(output_name).write_bytes(wrapper)
    print(f"Packed {source_name}: v{version}, {len(raw)} -> {len(compressed)} bytes LZ10")

raw_items = [
    ("easytop.bin", "easytop.binv", versions[2], 5),
    ("mediumtop.bin", "mediumtop.binv", versions[3], 5),
    ("hardtop.bin", "hardtop.binv", versions[4], 5),
    ("extremtop.bin", "extremtop.binv", versions[5], 3),
]
for source_name, output_name, version, expected_count in raw_items:
    raw = Path(source_name).read_bytes()
    validate_raw(source_name, raw, expected_count)
    packed = MAGIC + struct.pack("<I", version) + raw
    Path(output_name).write_bytes(packed)
    if Path(output_name).read_bytes() != packed:
        raise SystemExit(f"ERROR: raw asset verification failed for {output_name}")
    print(f"Wrapped {source_name}: v{version}, {len(raw)} -> {len(packed)} bytes raw")
PY

printf '\nPre-build complete. Run ./makeplugin.sh next.\n'
