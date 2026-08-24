from os import environ, replace
from os.path import exists
from pathlib import Path
from tempfile import TemporaryDirectory
import hashlib
import json
import re
import struct
import subprocess

from binutils_elf import find_tool, read_sections, read_symbols, read_relocations
from stable_abi import SYMBOL_TYPE_TO_KIND, symbol_key

HEADER_SIZE = 0x30
EXPORT_SIZE = 0x0C
MAX_ALLOWED_REFS = 31
R_ARM_ABS32 = 2
R_ARM_REL32 = 3
R_ARM_CALL = 28
R_ARM_JUMP24 = 29
SAFE_SAME_PLUGIN_RELOCS = {R_ARM_REL32, R_ARM_CALL, R_ARM_JUMP24}


def plugin_id_u32(value):
    return struct.unpack("<I", value.encode("ascii"))[0]


def read_u32(buf, off):
    if off < 0 or off + 4 > len(buf):
        raise ValueError(f"read outside plugin blob at {off:#x}")
    return struct.unpack_from("<I", buf, off)[0]


def write_u32(buf, off, value):
    if off < 0 or off + 4 > len(buf):
        raise ValueError(f"write outside plugin blob at {off:#x}")
    struct.pack_into("<I", buf, off, value & 0xFFFFFFFF)


def load_symbols(path):
    sections = read_sections(path)
    sections_by_name = {sec.name: sec for sec in sections}
    syms = read_symbols(path)
    values = {}
    for sym in syms:
        if sym.name:
            values.setdefault(sym.name, set()).add(int(sym["st_value"]))
    unique = {name: next(iter(vals)) for name, vals in values.items() if len(vals) == 1}
    return sections, sections_by_name, syms, unique


def load_marker_keys(path):
    path = Path(path)
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SystemExit(f"invalid {path}: {exc}")
    if not isinstance(data, dict):
        raise SystemExit(f"invalid {path}: expected object")
    out = {}
    for name, rec in data.items():
        if not isinstance(name, str) or not isinstance(rec, dict):
            raise SystemExit(f"invalid {path}: bad marker record")
        key = rec.get("key")
        if not isinstance(key, int) or not 0 <= key <= 0xFFFFFFFFFFFFFFFF:
            raise SystemExit(f"invalid {path}: bad key for {name}")
        out[name] = key
    return out


def validate_addend(stype, size, sym_addr, word, label):
    if word < sym_addr:
        raise ValueError(f"{label} has negative/wrapped addend")
    addend = word - sym_addr
    if stype in ("STT_FUNC", "STT_NOTYPE"):
        if addend:
            raise ValueError(f"{label} code/notype reference has nonzero addend {addend:#x}")
    elif stype == "STT_OBJECT":
        if addend and (not size or addend >= size):
            raise ValueError(f"{label} object addend {addend:#x} outside size {size:#x}")
    else:
        raise ValueError(f"{label} has unsupported symbol type {stype}")
    return addend


def build_module(module_name, plugin_magic, plugin_defs):
    new_elf = environ.get("NEXUS_3NX_INPUT_ELF", f"{module_name}.elf")
    marker_keys_path = "plgmarkers.keys"
    output_dir = Path(environ.get("NEXUS_3NX_OUTPUT_DIR", ".")).resolve()
    if not output_dir.is_dir():
        raise SystemExit(f"output directory does not exist: {output_dir}")
    if not plugin_defs:
        raise SystemExit("No plugins defined. Add plugin definitions to makeplugin.sh and run it first")
    plugin_info = {}
    output_names = {}
    for entry in plugin_defs:
        if not isinstance(entry, (list, tuple)) or len(entry) != 4:
            raise SystemExit(f"bad plugin entry {entry!r}: expected (id, output_name, priority, allowed_refs)")
        pid, name, priority, allowed_refs = entry
        if not isinstance(pid, str) or re.fullmatch(r"[A-Za-z0-9_]{4}", pid) is None:
            raise SystemExit(f"bad plugin id {pid!r}: expected exactly 4 ASCII letters, digits, or underscores")
        if not isinstance(name, str) or re.fullmatch(r"[A-Za-z0-9_.-]+", name) is None:
            raise SystemExit(f"bad output name {name!r} for {pid}")
        if not isinstance(priority, int) or isinstance(priority, bool) or not 0 <= priority <= 0xFFFFFFFF:
            raise SystemExit(f"bad priority for {pid}: {priority!r}")
        if not isinstance(allowed_refs, list) or not all(isinstance(ref, str) for ref in allowed_refs):
            raise SystemExit(f"bad allowed_refs for {pid}")
        if len(set(allowed_refs)) != len(allowed_refs) or pid not in allowed_refs:
            raise SystemExit(f"bad allowed_refs for {pid}: must include self exactly once")
        if len(allowed_refs) > MAX_ALLOWED_REFS:
            raise SystemExit(f"bad allowed_refs for {pid}: {len(allowed_refs)} > {MAX_ALLOWED_REFS}")
        filename = f"{name}.{priority}.3nx"
        if len(filename.encode("ascii")) >= 256:
            raise SystemExit(f"bad filename for {pid}: too long")
        if pid in plugin_info:
            raise SystemExit(f"duplicate plugin id {pid}")
        key = filename.casefold()
        if key in output_names:
            raise SystemExit(f"duplicate output filename {filename}")
        output_names[key] = pid
        plugin_info[pid] = {"filename": filename, "allowed_refs": set(allowed_refs)}

    plugin_ids = list(plugin_info)
    for pid, info in plugin_info.items():
        for ref in info["allowed_refs"]:
            if ref not in plugin_info:
                raise SystemExit(f"bad allowed_refs for {pid}: unknown plugin {ref!r}")

    seg_names = []
    for pid in plugin_ids:
        seg_names.extend([
            f".plugin_{pid}",
            f".pluginrodata_{pid}",
            f".plugindata_{pid}",
            f".pluginbss_{pid}",
        ])

    if not exists(new_elf):
        raise SystemExit(f"missing plugin elf: {new_elf}")

    marker_keys = load_marker_keys(marker_keys_path)
    sections, sections_by_name, syms, symvals = load_symbols(new_elf)

    def address_is_mapped(value):
        masked = value & ~1
        for sec in sections:
            if sec["sh_size"] and sec["sh_addr"] <= masked < sec["sh_addr"] + sec["sh_size"]:
                return True
        return False
    relocation_groups = read_relocations(new_elf, seg_names)

    plugin_section_prefixes = (".plugin_", ".pluginrodata_", ".plugindata_", ".pluginbss_")
    for section in sections:
        if section.name.startswith(plugin_section_prefixes) and section.name not in seg_names:
            raise SystemExit(f"unexpected plugin section {section.name!r}")
        if section.name.startswith(".rela.") and section.name[5:] in seg_names:
            raise SystemExit(f"unsupported RELA relocation section {section.name!r}")

    relocation_groups_by_name = {group.name: group for group in relocation_groups}
    if len(relocation_groups_by_name) != len(relocation_groups):
        raise SystemExit("duplicate relocation section")
    for section in sections:
        if not section.name.startswith(".rel.") or section.name[4:] not in seg_names:
            continue
        if section["sh_size"] % 8:
            raise SystemExit(f"malformed ARM REL section {section.name!r}")
        parsed = relocation_groups_by_name.get(section.name)
        if parsed is None or len(parsed.relocations) != section["sh_size"] // 8:
            raise SystemExit(f"failed to parse every relocation in {section.name!r}")

    symbol_details = {}
    for sym in syms:
        if sym.name:
            key = (sym.name, int(sym["st_value"]))
            old = symbol_details.get(key)
            if old is None or sym["st_info"]["type"] in SYMBOL_TYPE_TO_KIND:
                symbol_details[key] = sym

    def get_sym(name):
        if name not in symvals:
            raise SystemExit(f"missing symbol {name}")
        return symvals[name]

    plugin_start_by_id = {pid: get_sym(f"__plugin_{pid}_start") for pid in plugin_ids}
    plugin_runtime_span_by_id = {}
    for pid in plugin_ids:
        runtime_end = max(
            get_sym(f"__plugin_{pid}_end"),
            get_sym(f"__pluginrodata_{pid}_end"),
            get_sym(f"__plugindata_{pid}_end"),
            get_sym(f"__pluginbss_{pid}_end"),
        )
        span = runtime_end - plugin_start_by_id[pid]
        if span <= 0 or span > 0xFFFFFFFF:
            raise SystemExit(f"bad runtime span for {pid}: {span}")
        plugin_runtime_span_by_id[pid] = span

    def owner_plugin_from_addr(addr):
        masked = addr & ~1
        for pid in plugin_ids:
            for prefix in (".plugin_", ".pluginrodata_", ".plugindata_", ".pluginbss_"):
                sec = sections_by_name.get(prefix + pid)
                if sec and sec["sh_size"] and sec["sh_addr"] <= masked < sec["sh_addr"] + sec["sh_size"]:
                    return pid
        return None

    provider_exports = {pid: {} for pid in plugin_ids}
    provider_export_identity = {pid: {} for pid in plugin_ids}
    for sym in syms:
        if not sym.name or sym["st_shndx"] == "SHN_UNDEF" or sym["st_info"]["bind"] not in ("STB_GLOBAL", "STB_WEAK"):
            continue
        stype = sym["st_info"]["type"]
        if stype not in SYMBOL_TYPE_TO_KIND:
            continue
        owner = owner_plugin_from_addr(int(sym["st_value"]))
        if owner is None:
            continue
        value = (int(sym["st_value"]) - plugin_start_by_id[owner]) & 0xFFFFFFFF
        if (value & ~1) >= plugin_runtime_span_by_id[owner]:
            raise SystemExit(f"export {sym.name!r} lies outside {owner} runtime image")
        size = int(sym["st_size"])
        key = symbol_key(stype, sym.name)
        identity = (stype, sym.name)
        rec = (value, size, identity)
        old = provider_exports[owner].get(key)
        if old is not None and old != rec:
            raise SystemExit(f"u64 export collision in {owner}: 0x{key:016X}")
        provider_exports[owner][key] = rec
        old = provider_export_identity[owner].get(identity)
        if old is not None and old[:2] != rec[:2]:
            raise SystemExit(f"ambiguous external symbol {sym.name!r} in {owner}")
        provider_export_identity[owner][identity] = rec

    temporary_directory = TemporaryDirectory(prefix=".create3nx-", dir=output_dir)
    temporary_root = Path(temporary_directory.name)

    for current_plugin in plugin_ids:
        temporary_plugin = temporary_root / f"{current_plugin}.3nx"
        subprocess.run([
            find_tool("arm-none-eabi-objcopy"), "-O", "binary",
            f"--only-section=.plugin_{current_plugin}",
            f"--only-section=.pluginrodata_{current_plugin}",
            f"--only-section=.plugindata_{current_plugin}",
            "--gap-fill=0x00", new_elf, str(temporary_plugin),
        ], check=True)
        if not temporary_plugin.exists() or temporary_plugin.stat().st_size == 0:
            raise SystemExit(f"plugin {current_plugin} has no code or data")
        blob = bytearray(temporary_plugin.read_bytes())

        plugin_start = plugin_start_by_id[current_plugin]
        plugin_end = get_sym(f"__plugin_{current_plugin}_end")
        plugin_pad_end = get_sym(f"__plugin_{current_plugin}_pad_end")
        code_size = plugin_end - plugin_start
        if code_size <= 0 or code_size > 0xFFFFFFFF:
            raise SystemExit(f"bad code size for {current_plugin}")

        segs = {}
        for name in seg_names:
            sec = sections_by_name.get(name)
            segs[name] = {"addr": int(sec["sh_addr"]) if sec else 0, "size": int(sec["sh_size"]) if sec else 0, "present": bool(sec)}

        def find_seg(addr):
            masked = addr & ~1
            for name, rec in segs.items():
                if rec["present"] and rec["size"] and rec["addr"] <= masked < rec["addr"] + rec["size"]:
                    return name
            return None

        main_addresses = {
            int(sym["st_value"]) for sym in syms
            if sym["st_info"]["type"] == "STT_FUNC" and sym.name == f"PLUGIN_{current_plugin}_Main"
            and find_seg(int(sym["st_value"])) == f".plugin_{current_plugin}"
        }
        if len(main_addresses) != 1:
            raise SystemExit(f"expected exactly one PLUGIN_{current_plugin}_Main")
        main_offset = next(iter(main_addresses)) - plugin_start
        if main_offset != 0:
            raise SystemExit(
                f"PLUGIN_{current_plugin}_Main is +0x{main_offset:X}, expected +0x0; "
                f"put Main in __attribute__((section(\".plugin_{current_plugin}_entry\"), used))"
            )

        self_refs = []
        external_refs = {}
        host_repairs = []
        seen_patches = set()

        for relsec in relocation_groups:
            if not relsec.name.endswith(f"_{current_plugin}") or relsec.target_name not in seg_names or not segs[relsec.target_name]["present"]:
                continue
            for rel in relsec.relocations:
                offset = int(rel["r_offset"])
                patch_off = offset - plugin_start
                rel_type = int(rel["r_info_type"])
                raw_sym = rel.symbol
                sym = symbol_details.get((raw_sym.name, int(raw_sym["st_value"])), raw_sym)
                sym_addr = int(sym["st_value"])
                sym_seg = find_seg(sym_addr)
                if patch_off < 0 or patch_off + 4 > len(blob) or patch_off in seen_patches:
                    raise SystemExit(f"bad/duplicate relocation patch {patch_off:#x} in {current_plugin}")
                seen_patches.add(patch_off)
                word = read_u32(blob, patch_off)

                if sym_seg is None:
                    if rel_type != R_ARM_ABS32 or (patch_off & 3) or not sym.name:
                        raise SystemExit(f"unsupported host relocation for {sym.name!r} in {current_plugin}")
                    if sym.name in marker_keys:
                        key = marker_keys[sym.name]
                        if sym_addr != 0:
                            raise SystemExit(f"semantic marker {sym.name!r} linked at {sym_addr:#x}, expected placeholder 0")
                        addend = validate_addend("STT_NOTYPE", 0, sym_addr, word, f"marker {sym.name}")
                    else:
                        stype = sym["st_info"]["type"]
                        if sym["st_shndx"] == "SHN_UNDEF" or stype not in SYMBOL_TYPE_TO_KIND or not address_is_mapped(sym_addr):
                            raise SystemExit(f"host symbol {sym.name!r} ({stype}) is not a defined mapped symbol in the linked {module_name} ELF")
                        key = symbol_key(stype, sym.name)
                        addend = validate_addend(stype, int(sym["st_size"]), sym_addr, word, f"host symbol {sym.name}")
                    write_u32(blob, patch_off, addend)
                    host_repairs.append((patch_off, key, addend))
                    continue

                target_plugin = owner_plugin_from_addr(sym_addr)
                if target_plugin is None:
                    continue
                if target_plugin not in plugin_info[current_plugin]["allowed_refs"]:
                    raise SystemExit(f"forbidden plugin reference {current_plugin} -> {target_plugin} ({sym.name})")
                if rel_type != R_ARM_ABS32:
                    if target_plugin == current_plugin and rel_type in SAFE_SAME_PLUGIN_RELOCS:
                        continue
                    raise SystemExit(f"unsupported cross-provider relocation type {rel_type}: {current_plugin} -> {target_plugin} ({sym.name})")
                if patch_off & 3:
                    raise SystemExit(f"unaligned runtime relocation patch {patch_off:#x}")

                if target_plugin == current_plugin:
                    target_off = word - plugin_start
                    if target_off < 0 or (target_off & ~1) >= plugin_runtime_span_by_id[current_plugin]:
                        raise SystemExit(f"self target outside {current_plugin} image")
                    self_refs.append((patch_off, target_off))
                    continue

                if not sym.name:
                    raise SystemExit(f"unnamed cross-plugin target {current_plugin} -> {target_plugin}")
                stype = sym["st_info"]["type"]
                exported = provider_export_identity[target_plugin].get((stype, sym.name))
                if exported is None:
                    raise SystemExit(f"cross-plugin symbol {sym.name!r} ({stype}) is not exported by {target_plugin}")
                export_value, export_size, _ = exported
                addend = validate_addend(stype, export_size, sym_addr, word, f"{target_plugin}:{sym.name}")
                target_off = export_value + addend
                if (target_off & ~1) >= plugin_runtime_span_by_id[target_plugin]:
                    raise SystemExit(f"target outside {target_plugin} image")
                external_refs.setdefault(plugin_id_u32(target_plugin), []).append((patch_off, target_off, symbol_key(stype, sym.name), addend))

        fast = bytearray()
        plugin_repairs = {}
        if self_refs:
            self_refs.sort()
            fast += struct.pack("<II", plugin_id_u32(current_plugin), len(self_refs))
            for patch, target in self_refs:
                fast += struct.pack("<II", patch, target)
        for provider_id in sorted(external_refs):
            refs = sorted(external_refs[provider_id], key=lambda item: (item[0], item[1], item[2], item[3]))
            fast += struct.pack("<II", provider_id, len(refs))
            for patch, target, key, addend in refs:
                target_field = HEADER_SIZE + len(fast) + 4
                fast += struct.pack("<II", patch, target)
                plugin_repairs.setdefault(provider_id, []).append((key, target_field, addend))

        plugin_data_start = plugin_pad_end
        plugin_data_end = plugin_data_start
        for name in (f".pluginrodata_{current_plugin}", f".plugindata_{current_plugin}"):
            rec = segs[name]
            if rec["present"] and rec["size"]:
                if rec["addr"] < plugin_data_start:
                    raise SystemExit(f"bad data layout in {current_plugin}")
                plugin_data_end = max(plugin_data_end, rec["addr"] + rec["size"])
        data_size = plugin_data_end - plugin_data_start
        data_off = plugin_data_start - plugin_start
        if data_off < 0 or (data_size and data_off + data_size > len(blob)):
            raise SystemExit(f"plugin data outside extracted blob for {current_plugin}")
        data_blob = bytes(blob[data_off:data_off + data_size])

        bss = segs[f".pluginbss_{current_plugin}"]
        if bss["present"] and bss["size"]:
            if bss["addr"] < plugin_data_end:
                raise SystemExit(f"bad BSS layout in {current_plugin}")
            runtime_data_end = bss["addr"] + bss["size"]
            bss_size = runtime_data_end - plugin_data_start - data_size
        else:
            bss_size = 0
        if not 0 <= bss_size <= 0xFFFFFFFF:
            raise SystemExit(f"bad BSS span for {current_plugin}")

        repair = bytearray()
        export_records = sorted(provider_exports[current_plugin].items())
        export_blob = b"".join(struct.pack("<QI", key, value) for key, (value, _size, _) in export_records)
        repair += struct.pack("<I", len(export_records))
        repair += export_blob

        runtime_data_start_rel = plugin_data_start - plugin_start
        host_group = []
        for patch_off, key, addend in sorted(host_repairs, key=lambda item: (item[1], item[0], item[2])):
            if patch_off < code_size:
                cache_offset = HEADER_SIZE + len(fast) + patch_off
            elif runtime_data_start_rel <= patch_off < runtime_data_start_rel + data_size:
                cache_offset = HEADER_SIZE + len(fast) + code_size + (patch_off - runtime_data_start_rel)
            else:
                raise SystemExit(f"host cache patch {patch_off:#x} is not stored in code/data for {current_plugin}")
            host_group.append((key, cache_offset, addend))
        if host_group:
            repair += struct.pack("<II", 0, len(host_group))
            for rec in host_group:
                repair += struct.pack("<QII", *rec)
        for provider_id in sorted(plugin_repairs):
            records = sorted(plugin_repairs[provider_id], key=lambda item: (item[0], item[1], item[2]))
            repair += struct.pack("<II", provider_id, len(records))
            for rec in records:
                repair += struct.pack("<QII", *rec)

        abi_payload = b"NEXUS_3NX_ABI\0" + struct.pack("<II", plugin_magic, plugin_id_u32(current_plugin)) + export_blob
        own_abi = int.from_bytes(hashlib.sha256(abi_payload).digest()[:8], "little") & 0xFFFFFFFFFFFFFFFE

        out = bytearray(struct.pack(
            "<IIIIIIIQQI",
            plugin_magic,
            plugin_id_u32(current_plugin),
            code_size,
            data_size,
            bss_size,
            len(fast),
            len(repair),
            own_abi,
            0,
            0,
        ))
        if len(out) != HEADER_SIZE:
            raise SystemExit("internal 3nx header size mismatch")
        out += fast
        out += blob[:code_size]
        out += data_blob
        out += repair
        out += b"\x00" * ((-len(out)) & 0xF)

        temporary_plugin.write_bytes(out)
        output_path = output_dir / plugin_info[current_plugin]["filename"]
        replace(temporary_plugin, output_path)
        print(
            f"{current_plugin}: {len(self_refs)} self relocation(s), "
            f"{sum(len(v) for v in external_refs.values())} plugin relocation(s), "
            f"{len(host_repairs)} HOST repair(s), {len(export_records)} export(s) -> {output_path.name}"
        )