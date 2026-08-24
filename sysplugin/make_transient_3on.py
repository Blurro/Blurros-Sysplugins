from pathlib import Path
import hashlib
import struct
import sys

TRANSIENT_MAGIC = 0x26584E33
ROSALINA_MAGIC = 0x24584E33
OLD_HEADER_SIZE = 0x2C
NEW_HEADER_SIZE = 0x30


def plugin_id_u32(value):
    return struct.unpack('<I', value.encode('ascii'))[0]


def parse_source(data, expected_pid):
    if len(data) >= NEW_HEADER_SIZE:
        fields = struct.unpack_from('<12I', data, 0)
        magic, pid, code_size, data_size, _bss_size, reloc_size, repair_size = fields[:7]
        metadata_size = fields[11]
        raw_end = NEW_HEADER_SIZE + reloc_size + code_size + data_size + repair_size
        body_end = (raw_end + 0xF) & ~0xF
        entry_end = body_end + metadata_size
        if magic == ROSALINA_MAGIC and pid == expected_pid and code_size and not (metadata_size & 0xF) and entry_end == len(data):
            header = bytearray(data[:OLD_HEADER_SIZE])
            payload_end = NEW_HEADER_SIZE + reloc_size + code_size + data_size + repair_size
            payload = data[NEW_HEADER_SIZE:payload_end]
            out = header + payload
            out += b'\0' * (((len(out) + 0xF) & ~0xF) - len(out))
            return out

    if len(data) < OLD_HEADER_SIZE:
        raise SystemExit('truncated header')

    magic, pid, code_size, data_size, _bss_size, reloc_size, repair_size, _abi, env = struct.unpack_from('<7IQQ', data, 0)
    raw_end = OLD_HEADER_SIZE + reloc_size + code_size + data_size + repair_size
    entry_end = (raw_end + 0xF) & ~0xF
    if magic != ROSALINA_MAGIC or pid != expected_pid or not code_size or env != 0 or entry_end != len(data):
        raise SystemExit('unexpected source header')
    return bytearray(data)


def convert(source, destination, expected_id):
    src = Path(source)
    dst = Path(destination)
    expected_pid = plugin_id_u32(expected_id)

    try:
        data = parse_source(bytearray(src.read_bytes()), expected_pid)
    except SystemExit as exc:
        raise SystemExit(f'{src}: {exc}')

    magic, pid, code_size, data_size, _bss_size, reloc_size, repair_size, _abi, env = struct.unpack_from('<7IQQ', data, 0)
    if magic != ROSALINA_MAGIC or pid != expected_pid or not code_size or env != 0:
        raise SystemExit(f'{src}: unexpected normalized header')

    pos = OLD_HEADER_SIZE
    reloc_end = pos + reloc_size
    while pos < reloc_end:
        if reloc_end - pos < 8:
            raise SystemExit(f'{src}: malformed relocation group')
        provider, count = struct.unpack_from('<II', data, pos)
        pos += 8
        if provider != expected_pid or count > (reloc_end - pos) // 8:
            raise SystemExit(f'{src}: transient entry has non-self relocation')
        pos += count * 8
    if pos != reloc_end:
        raise SystemExit(f'{src}: malformed relocation table')

    repair_off = OLD_HEADER_SIZE + reloc_size + code_size + data_size
    if repair_size < 4:
        raise SystemExit(f'{src}: malformed repair table')
    export_count = struct.unpack_from('<I', data, repair_off)[0]
    export_bytes = export_count * 12
    if 4 + export_bytes != repair_size:
        raise SystemExit(f'{src}: transient entry has HOST/plugin repair groups')
    export_blob = bytes(data[repair_off + 4:repair_off + 4 + export_bytes])

    abi_payload = b'NEXUS_3NX_ABI\0' + struct.pack('<II', TRANSIENT_MAGIC, expected_pid) + export_blob
    own_abi = int.from_bytes(hashlib.sha256(abi_payload).digest()[:8], 'little') & 0xFFFFFFFFFFFFFFFE
    struct.pack_into('<I', data, 0, TRANSIENT_MAGIC)
    struct.pack_into('<Q', data, 0x1C, own_abi)
    dst.write_bytes(data)


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: make_transient_3on.py SOURCE DESTINATION ID')
    convert(sys.argv[1], sys.argv[2], sys.argv[3])