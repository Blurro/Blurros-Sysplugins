#!/bin/bash
set -euo pipefail
if [[ -n "${DEVKITARM:-}" && -d "${DEVKITARM}/bin" ]]; then
    export PATH="${DEVKITARM}/bin:${PATH}"
elif [[ -n "${DEVKITPRO:-}" && -d "${DEVKITPRO}/devkitARM/bin" ]]; then
    export PATH="${DEVKITPRO}/devkitARM/bin:${PATH}"
elif [[ -d "/opt/devkitpro/devkitARM/bin" ]]; then
    export PATH="/opt/devkitpro/devkitARM/bin:${PATH}"
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

if (( BASH_VERSINFO[0] < 4 )); then
    printf 'ERROR: makeplugin.sh requires Bash 4 or newer.\n' >&2
    exit 1
fi

# =========================
# ==== START OF CONFIG ====
# =========================
#
# Each plugin entry is:
#   "id|output_name|priority|allowed_refs"
#
# Fields:
#   id
#       Exactly 4 chars: A-Z, a-z, 0-9 or _ to fit into 4 bytes
#       Example: blur, 1337, coin, COIN
#       These are unique per module, both loader and rosalina can have a plugin with the same id
#
#   output_name
#       Base filename for the generated .3nx
#       Final filename becomes:
#           output_name.priority.3nx
#       Completed files are written beside makeplugin.sh in the Nexus root,
#       which is also where a normal top-level build writes boot.firm.
#       Example:
#           blurPLGbase.50.3nx
#           playcoinmod.100.3nx
#
#   priority
#       Lower filename priority sorts first; ties use filename, then stacked-entry order. Final sorted order is Main() order.
#       As ties are resolved, sorted priority between plugins cannot end up as equal.
#       Example:
#           blur priority 0 has main() called before coin priority 5.
#
#   allowed_refs
#       Plugin IDs this plugin may reference, excluding itself. A provider does not need to be built in the same run:
#       undefined cross-plugin symbols named PLUGIN_<4-char ID>_* are resolved at runtime when that ID is listed here.
#       The consumer header/source must give each imported symbol its correct ELF type (function/object).
#       A referenced plugin is a dependency only when its sorted priority is lower, if higher then its up to the
#       author to manage the potential risks, as that plugin's main() may return false and unload from memory.
#       Higher = reference, lower = dependency.
#       If a plugin's reference is unavailable before main()s run, its runtime pointer slots to it are NULL.
#       If a plugin is early-rejected or main() returns false, the runtime rejects not-yet-run plugins that depend on it.
#       Example config:
#           blur references only itself, so this can be empty:
#               "blur|blurPLGbase|10|"
#           if coin were to reference blur and ref2 it would be
#               "coin|coinmod|50|blur,ref2"
#

ROSALINA_PLUGIN_CONFIG=(
    "blur|blurPLGbase|10|MENU"
    "coin|coinrosalina|50|blur,MENU"
    "powr|PowerPrevent|60|MENU"
    "helo|HelloWorld|70|MENU"
)

LOADER_PLUGIN_CONFIG=(
    "CFGN|IgnoreCfgNor-v101|0|"
    "coin|coinloader|50|"
)

# Optional metadata is appended to one single-entry .3nx after its normal 16-byte body padding.
# Each entry is:
#   "plugin_name|file1|file2|..."
# plugin_name is the configured output_name, or the base name of exactly one completed
# plugin_name.<priority>.3nx beside this script. Metadata files must also be beside this script.
# Files are concatenated in listed order with no gaps, then the combined metadata is padded to 16 bytes.
# Re-running replaces that plugin's previous metadata. Already-stacked .3nx targets are warned and dropped.
METADATA_CONFIG=(
    "blurPLGbase|version101.bin"
    "coinrosalina|version104.bin|coinasset_icn.lz|coinasset_achv.lz|easytop.binv|mediumtop.binv|hardtop.binv|extremtop.binv"
    "PowerPrevent|version100.bin"
    "HelloWorld|version100.bin"
    "coinloader|version104.bin"
)

# Tip: 3nx file data can be stacked, to make one .3nx file hold multiple plugins. Place generated .3nx files at SD:/luma/plugins/
# Stacked plugins in order of pluginA then pluginB, function the same as pluginA.1.3nx then pluginB.2.3nx
# Each stack entry is:
#   "output_name|priority|plugin_name,plugin_name[,...]"
# Leave priority empty to keep the existing behavior of using the lowest member priority.
# A supplied priority controls only the created stack filename; member ordering still follows member priorities.
# Configured members use the files built by this run, stacked by config priorities.
# Other non-config members use the single completed output_name.<priority>.3nx beside this script.
# Already-stacked inputs are warned and dropped; module+ID duplicates fail the stack.

STACKED_PLUGIN_CONFIG=(
    "Playcoinz|10|blurPLGbase,coinloader,coinrosalina"
)

# USEFUL MARKER AND COMPILER INFORMATION!!!
# PLG_MARKER names the complete GCC semantic construct immediately before the comment.
# GCC supplies the parsed and normalized construct; DWARF supplies the linked address, saved into the 3nr.
#
# Good:
#   existing_code(); // PLG_MARKER(coin_marker_5)
#   existing_code(); //  PLG_MARKER( coin_marker_5 ) note
#   existing_code(); /*   PLG_MARKER (coin_marker_5  )  */ other_code();
#   if (condition) /* PLG_MARKER(coin_marker_5) */ {
#       existing_code();
#   }
# Bad:
#   // PLG_MARKER(marker)              # marker goes AFTER the target
#   existing_code(); /* note PLG_MARKER(marker) */
#   existing_code(); /* PLG_MARKER(marker) note */
#   foo(bar() /* PLG_MARKER(marker) */);   # nested subexpression is not a complete anchor
#   } else /* PLG_MARKER(marker) */ {      # bare else has no semantic construct
#   } // PLG_MARKER(marker)                # closing brace has no semantic construct
#   ; // PLG_MARKER(marker)                # empty statement has no semantic construct
#
# Formatting and line movement are safe; adding an identical construct earlier in the same function can change occurrence identity.
# Do not use PLG_MARKER comments in plugin-owned code. Markers are only for unnamed host executable sites.
#
# Plugin code can then use 'extern u32 coin_marker_5;' then use it in a table, like:
# PLUGIN_DATA(coin) void* pluginTable_coin[] = {
#    (void*)&coin_marker_5
# }
# #define COIN_HOST__coin_marker_5      ((u32)pluginTable_coin[0])
# ...then use COIN_HOST__coin_marker_5 anywhere in the 'coin' plugin code.
# Access host targets through the plugin table; do not use the host symbol directly at runtime.
# This 'define' trick forces non-relative references, which is VERY IMPORTANT for plugin code to be relocatable
#
# Run ./makeplugin.sh for every plugin build. It automatically:
# - runs a normal incremental make for plugin-code-only changes
# - prepares marker placeholders, builds, then resolves semantic marker keys
# - incrementally recompiles changed marked host sources while preserving unrelated .o files
# - relinks without recompiling source when plugin IDs/order only change the linker layout
# - recompiles all C/C++ sources only if the semantic GCC marker plugin itself changes
# - does NOT relink for output name / priority / allowed_refs-only changes
# - runs create3nx.py automatically after the build

# =========================
# ==== END OF CONFIG ====
# =========================

MAX_ALLOWED_REFS=31



LD_START_MARKER="/* pluginstart */"
LD_END_MARKER="/* pluginend */"

PY_START_MARKER="#makepluginstart#"
PY_END_MARKER="#makepluginend#"

total_config_count=$((${#ROSALINA_PLUGIN_CONFIG[@]} + ${#LOADER_PLUGIN_CONFIG[@]}))
total_metadata_count=${#METADATA_CONFIG[@]}
total_stack_count=${#STACKED_PLUGIN_CONFIG[@]}

if [[ "$total_config_count" -eq 0 && "$total_metadata_count" -eq 0 && "$total_stack_count" -eq 0 ]]; then
    printf '\n'
    printf '========================================\n'
    printf '========= No plugins to build! =========\n'
    printf '========================================\n'
    printf 'Open this script for plugin configuration and info.\n\n'
    exit 0
fi

printf '\n'
printf '=========================================\n'
printf '========= Building all plugins! =========\n'
printf '=========================================\n'
printf 'Plugin configuration and marker information is documented in this script.\n\n'

if ! command -v python3 >/dev/null 2>&1; then
    printf 'ERROR: Python 3 is required to build plugins.\n' >&2
    exit 1
fi

# Helper funcs
trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf "%s" "$s"
}


STATE_DIR=".plgbuild"
mkdir -p "$STATE_DIR"

ROOT_DIR="$(pwd -P)"

# If this header has not been generated yet, create it before building plugins.
# Header generation may create temporary K11 build files; remove files created
# by this invocation afterward and keep the generated header for later builds.
ensure_sysplugin_entry_header() {
    local entry_header="${ROOT_DIR}/sysplugin/include/SysPluginLoaderEntryGenerated.h"
    local build_pair_tool="${ROOT_DIR}/sysplugin/build_pair.py"
    local k11_build_dir="${ROOT_DIR}/k11_extension/build"
    local k11_elf="${ROOT_DIR}/k11_extension/k11_extension.elf"
    local k11_build_existed=0
    local k11_elf_existed=0

    [[ -f "$entry_header" ]] && return 0
    if [[ ! -f "$build_pair_tool" ]]; then
        printf 'ERROR: missing sysplugin/build_pair.py: %s\n' "$build_pair_tool" >&2
        return 1
    fi

    [[ -e "$k11_build_dir" ]] && k11_build_existed=1
    [[ -e "$k11_elf" ]] && k11_elf_existed=1

    printf 'Generating SysPluginLoaderEntryGenerated.h for first plugin build...\n'
    if ! python3 - "$ROOT_DIR" <<'PY_ENTRY_HEADER'
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
sys.path.insert(0, str(root / "sysplugin"))
import build_pair
build_pair.bootstrap_entry_header()
PY_ENTRY_HEADER
    then
        [[ "$k11_build_existed" -eq 1 ]] || rm -rf -- "$k11_build_dir"
        [[ "$k11_elf_existed" -eq 1 ]] || rm -f -- "$k11_elf"
        return 1
    fi

    [[ "$k11_build_existed" -eq 1 ]] || rm -rf -- "$k11_build_dir"
    [[ "$k11_elf_existed" -eq 1 ]] || rm -f -- "$k11_elf"

    if [[ ! -f "$entry_header" ]]; then
        printf 'ERROR: failed to generate %s\n' "$entry_header" >&2
        return 1
    fi
}

ensure_sysplugin_entry_header

declare -A MODULE_CONFIG_FP=()
declare -A MODULE_MARKER_FP=()
declare -A MODULE_MARKER_SOURCE_STATE=()
declare -A MODULE_MARKER_COMPILER_FP=()
declare -A MODULE_MARKER_COMPILER_CHANGED=([rosalina]=false [loader]=false)
declare -A MODULE_RELINK_REQUIRED=([rosalina]=false [loader]=false)
declare -A MODULE_EMIT_COUNTS=([rosalina]=0 [loader]=0)
declare -A OUTPUT_OWNERS=()
declare -A PLUGIN_OUTPUT_FILES=()
declare -A PLUGIN_OUTPUT_PRIORITIES=()
declare -A PLUGIN_OUTPUT_MODULES=()
declare -A PLUGIN_OUTPUT_NAMES=()
declare -A PLUGIN_OUTPUT_IDS=()
declare -A METADATA_TARGET_OUTPUTS=()
declare -A METADATA_TARGET_KINDS=()
declare -A METADATA_TARGET_SOURCES=()
declare -A METADATA_TARGET_SIZES=()
declare -A METADATA_TARGET_HASHES=()
declare -A METADATA_TARGET_IDENTITIES=()
declare -A METADATA_TARGET_FILES=()
METADATA_TARGET_KEYS=()
METADATA_NONCONFIG_OUTPUTS=()
declare -A STACKED_MEMBER_OUTPUTS=()
declare -A STACKED_MEMBER_COUNTS=()
declare -A STACKED_MEMBER_SOURCES=()
declare -A STACKED_MEMBER_KINDS=()
declare -A STACKED_MEMBER_SIZES=()
declare -A STACKED_MEMBER_HASHES=()
declare -A STACKED_MEMBER_IDENTITIES=()
declare -A STACKED_MEMBER_FILES=()
GENERATED_OUTPUTS=()
PUBLISHED_INDIVIDUAL_OUTPUTS=()
PUBLISHED_OUTPUTS=()
STACKED_OUTPUT_FILES=()
STACKED_DESCRIPTIONS=()
BUILD_REASONS=()
BUILD_NOTES=()

inspect_stack_3nx() {
    local path="$1"
    local result

    if ! result="$(python3 - "$path" <<'PY'
import pathlib
import re
import struct
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
module_names = {0x24584E33: "rosalina", 0x25584E33: "loader"}
HEADER_SIZE = 0x30
entries = []
pos = 0

if not data:
    raise SystemExit(f"ERROR: empty .3nx stack input: {path}")

while pos < len(data):
    if len(data) - pos < HEADER_SIZE:
        raise SystemExit(f"ERROR: truncated .3nx entry header at 0x{pos:X}: {path}")

    magic, _pid_word, code_size, data_size, _bss_size, reloc_size, repair_size, _abi_lo, _abi_hi, _env_lo, _env_hi, metadata_size = struct.unpack_from("<12I", data, pos)
    if magic not in module_names or code_size == 0:
        raise SystemExit(f"ERROR: invalid .3nx header at 0x{pos:X}: {path}")

    raw_id = data[pos + 4:pos + 8]
    try:
        plugin_id = raw_id.decode("ascii")
    except UnicodeDecodeError:
        raise SystemExit(f"ERROR: invalid .3nx plugin ID at 0x{pos + 4:X}: {path}")
    if re.fullmatch(r"[A-Za-z0-9_]{4}", plugin_id) is None:
        raise SystemExit(f"ERROR: invalid .3nx plugin ID '{plugin_id}' at 0x{pos + 4:X}: {path}")

    raw_end = pos + HEADER_SIZE + reloc_size + code_size + data_size + repair_size
    body_end = (raw_end + 0xF) & ~0xF
    entry_end = body_end + metadata_size
    if metadata_size & 0xF:
        raise SystemExit(f"ERROR: metadata size is not 16-byte aligned at 0x{pos:X}: {path}")
    if raw_end < pos or body_end < raw_end or entry_end < body_end or entry_end <= pos or entry_end > len(data):
        raise SystemExit(f"ERROR: truncated/overflowed .3nx entry at 0x{pos:X}: {path}")

    q = pos + HEADER_SIZE
    reloc_end = q + reloc_size
    while q < reloc_end:
        if reloc_end - q < 8:
            raise SystemExit(f"ERROR: malformed .3nx relocation group at 0x{q:X}: {path}")
        _provider, count = struct.unpack_from("<II", data, q)
        q += 8
        group_bytes = count * 8
        if q + group_bytes < q or q + group_bytes > reloc_end:
            raise SystemExit(f"ERROR: malformed .3nx relocation count at 0x{q:X}: {path}")
        q += group_bytes
    if q != reloc_end:
        raise SystemExit(f"ERROR: malformed .3nx relocation table at 0x{pos:X}: {path}")

    entries.append(f"{module_names[magic]}:{plugin_id}")
    pos = entry_end

if pos != len(data):
    raise SystemExit(f"ERROR: malformed .3nx file length: {path}")

print(len(entries), ",".join(entries), sep="\t")
PY
)"; then
        exit 1
    fi

    IFS=$'\t' read -r INSPECTED_STACK_ENTRY_COUNT INSPECTED_STACK_IDENTITIES <<< "$result"
}

sha256_file_or_missing() {
    local path="$1"
    if [[ -f "$path" ]]; then
        sha256sum "$path" | awk '{print $1}'
    else
        printf 'MISSING'
    fi
}

module_config_fingerprint() {
    local module_name="$1"
    local config_array_name="$2"
    local -n cfg="$config_array_name"
    {
        printf 'module=%s\n' "$module_name"
        printf '%s\n' "${cfg[@]}"
    } | sha256sum | awk '{print $1}'
}

read_state() {
    local path="$1"
    [[ -f "$path" ]] && cat "$path" || true
}

write_state() {
    local path="$1"
    local value="$2"
    printf '%s' "$value" > "$path"
}

source_object_name() {
    local rel="$1"
    local base="${rel##*/}"
    case "$base" in
        *.cpp) printf '%s.o' "${base%.cpp}" ;;
        *.c)   printf '%s.o' "${base%.c}" ;;
        *)     return 1 ;;
    esac
}

invalidate_module_object() {
    local module_name="$1"
    local rel="$2"
    local obj
    if ! obj="$(source_object_name "$rel")"; then
        return 0
    fi
    local build_dir="./sysmodules/${module_name}/build"
    rm -f -- "${build_dir}/${obj}" "${build_dir}/${obj%.o}.d"
}

invalidate_all_marker_compiled_objects() {
    local module_name="$1"
    local module_dir="./sysmodules/${module_name}"
    local src
    while IFS= read -r -d '' src; do
        local rel="${src#${module_dir}/}"
        invalidate_module_object "$module_name" "$rel"
    done < <(find "$module_dir/source" -type f \( -name '*.c' -o -name '*.cpp' \) -print0 2>/dev/null)
}

changed_marker_sources() {
    local previous_state_file="$1"
    local current_state_file="$2"
    python3 - "$previous_state_file" "$current_state_file" <<'PY_STATE'
from pathlib import Path
import sys

def load(path):
    p = Path(path)
    out = {}
    if not p.is_file():
        return out
    for raw in p.read_text(encoding='utf-8').splitlines():
        if not raw:
            continue
        digest, rel = raw.split('\t', 1)
        out[rel] = digest
    return out

old = load(sys.argv[1])
new = load(sys.argv[2])
for rel in sorted(set(old) | set(new), key=str.casefold):
    if old.get(rel) != new.get(rel):
        print(rel)
PY_STATE
}


replace_between_markers() {
    local target_file="$1"
    local insert_file="$2"
    local start_marker="$3"
    local end_marker="$4"
    local tmp_file

    tmp_file="$(mktemp)"

    if ! awk -v start_marker="$start_marker" \
        -v end_marker="$end_marker" \
        -v insert_file="$insert_file" '
        BEGIN {
            while ((getline insert_line < insert_file) > 0) {
                insert_text = insert_text insert_line "\n"
            }

            close(insert_file)

            inside = 0
            start_count = 0
            end_count = 0
            bad_order = 0
        }

        index($0, start_marker) {
            start_count++
            if (start_count != 1 || end_count != 0 || inside)
                bad_order = 1
            inside = 1
            print
            printf "%s", insert_text
            next
        }

        index($0, end_marker) {
            end_count++
            if (start_count != 1 || end_count != 1 || !inside)
                bad_order = 1
            inside = 0
            print
            next
        }

        !inside {
            print
        }

        END {
            if (start_count != 1 || end_count != 1 || inside || bad_order) {
                printf "expected exactly one ordered marker pair: %s ... %s\n", start_marker, end_marker > "/dev/stderr"
                exit 1
            }
        }
    ' "$target_file" > "$tmp_file"; then
        rm -f -- "$tmp_file"
        return 1
    fi

    chmod --reference="$target_file" "$tmp_file"
    mv "$tmp_file" "$target_file"
}

process_module_plugins() {
    local module_name="$1"
    local config_array_name="$2"

    local -n module_plugin_config="$config_array_name"

    if [[ ${#module_plugin_config[@]} -eq 0 ]]; then
        echo "skipping sysmodules/${module_name}: no plugins configured"
        return 0
    fi

    local module_dir="./sysmodules/${module_name}"
    local ld_file="${module_dir}/3dsx.ld"
    local specs_file="${module_dir}/3dsx.specs"
    local makefile="${module_dir}/Makefile"
    local create3nx_file="${module_dir}/create3nx.py"
    local marker_script="${module_dir}/gen_plgmarkers.py"
    local marker_ld="${module_dir}/plgmarkers.ld"
    local config_state="${STATE_DIR}/${module_name}.config.sha256"
    local marker_state="${STATE_DIR}/${module_name}.markers.sha256"
    local marker_source_state="${STATE_DIR}/${module_name}.marker-sources.tsv"
    local marker_compiler_state="${STATE_DIR}/${module_name}.marker-compiler.sha256"
    local config_fp
    local previous_config_fp
    local old_ld_hash
    local old_py_hash

    config_fp="$(module_config_fingerprint "$module_name" "$config_array_name")"
    previous_config_fp="$(read_state "$config_state")"
    old_ld_hash="$(sha256_file_or_missing "$ld_file")"
    old_py_hash="$(sha256_file_or_missing "$create3nx_file")"
    MODULE_CONFIG_FP["$module_name"]="$config_fp"

    if [[ "$config_fp" != "$previous_config_fp" ]]; then
        BUILD_NOTES+=("${module_name}: plugin packaging configuration changed")
    fi

    if [[ ! -f "$ld_file" ]]; then
        echo "missing linker script: $ld_file"
        exit 1
    fi

    if [[ ! -f "$specs_file" ]]; then
        echo "missing linker specs: $specs_file"
        exit 1
    fi

    if [[ ! -f "$makefile" ]] || ! grep -Eq '^[[:space:]]*elf[[:space:]]*:' "$makefile"; then
        echo "missing sysplugin Makefile support: $makefile"
        exit 1
    fi

    if [[ ! -f "$create3nx_file" ]]; then
        echo "missing create3nx script: $create3nx_file"
        exit 1
    fi

    # Validate config
    local plugin_ids=()
    local plugin_output_names=()
    local plugin_priorities=()
    local plugin_allowed_refs=()
    local emit_count=0

    local entry
    for entry in "${module_plugin_config[@]}"; do
        local pid
        local out_name
        local priority
        local allowed_refs

        IFS='|' read -r pid out_name priority allowed_refs <<< "$entry"

        pid="$(trim "$pid")"
        out_name="$(trim "$out_name")"
        priority="$(trim "$priority")"
        allowed_refs="$(trim "$allowed_refs")"

        if [[ ! "$pid" =~ ^[A-Za-z0-9_]{4}$ ]]; then
            echo "bad plugin id '$pid' in ${module_name}: must be exactly 4 ASCII chars using A-Z, a-z, 0-9, _"
            exit 1
        fi

        if [[ ! "$out_name" =~ ^[A-Za-z0-9_.-]+$ ]]; then
            echo "bad output_name '$out_name' for $pid in ${module_name}: use only A-Z, a-z, 0-9, _, ., -"
            exit 1
        fi

        if [[ ! "$priority" =~ ^[0-9]+$ ]]; then
            echo "bad priority '$priority' for $pid in ${module_name}: must be a non-negative integer"
            exit 1
        fi

        local priority_normalized="$priority"
        while [[ ${#priority_normalized} -gt 1 && "${priority_normalized:0:1}" == "0" ]]; do
            priority_normalized="${priority_normalized:1}"
        done

        if [[ ${#priority_normalized} -gt 10 ||
              ( ${#priority_normalized} -eq 10 && "$priority_normalized" > "4294967295" ) ]]; then
            echo "bad priority '$priority' for $pid in ${module_name}: maximum is 4294967295"
            exit 1
        fi

        # Python 3 rejects decimal literals such as 010. Normalize once and
        # use the same value in metadata and the final filename.
        priority="$priority_normalized"

        local final_name="${out_name}.${priority}.3nx"
        if [[ ${#final_name} -ge 256 ]]; then
            echo "bad output filename '$final_name' for $pid in ${module_name}: must be shorter than 256 ASCII bytes"
            exit 1
        fi

        # Plugin output names are unique across both modules, regardless of case or priority.
        local output_key="${out_name,,}"
        if [[ -n "${OUTPUT_OWNERS[$output_key]+present}" ]]; then
            echo "bad output_name '$out_name' for $pid in ${module_name}: collides with ${OUTPUT_OWNERS[$output_key]}"
            exit 1
        fi
        OUTPUT_OWNERS["$output_key"]="${module_name}:${pid}"
        PLUGIN_OUTPUT_FILES["$output_key"]="$final_name"
        PLUGIN_OUTPUT_PRIORITIES["$output_key"]="$priority"
        PLUGIN_OUTPUT_MODULES["$output_key"]="$module_name"
        PLUGIN_OUTPUT_NAMES["$output_key"]="$out_name"
        PLUGIN_OUTPUT_IDS["$output_key"]="$pid"

        GENERATED_OUTPUTS+=("$final_name")
        emit_count=$((emit_count + 1))

        local existing
        for existing in "${plugin_ids[@]}"; do
            if [[ "$existing" == "$pid" ]]; then
                echo "bad ${module_name} plugin config: duplicate plugin id '$pid'"
                exit 1
            fi
        done

        plugin_ids+=("$pid")
        plugin_output_names+=("$out_name")
        plugin_priorities+=("$priority")
        plugin_allowed_refs+=("$allowed_refs")
    done

    MODULE_EMIT_COUNTS["$module_name"]="$emit_count"

    local i
    for i in "${!plugin_ids[@]}"; do
        local pid="${plugin_ids[$i]}"
        local refs
        local ref_count=1
        local -A seen_refs=()

        IFS=',' read -ra refs <<< "${plugin_allowed_refs[$i]}"

        local ref
        for ref in "${refs[@]}"; do
            ref="$(trim "$ref")"

            if [[ -z "$ref" ]]; then
                continue
            fi

            if [[ -n "${seen_refs[$ref]+present}" ]]; then
                echo "bad allowed_refs for $pid in ${module_name}: duplicate plugin '$ref'"
                exit 1
            fi
            seen_refs["$ref"]=1

            if [[ "$ref" != "$pid" ]]; then
                ref_count=$((ref_count + 1))
            fi

            local found=false
            local known

            for known in "${plugin_ids[@]}"; do
                if [[ "$known" == "$ref" ]]; then
                    found=true
                    break
                fi
            done

        done

        if [[ "$ref_count" -gt "$MAX_ALLOWED_REFS" ]]; then
            echo "bad allowed_refs for $pid in ${module_name}: ${ref_count} refs including itself, max is ${MAX_ALLOWED_REFS}"
            exit 1
        fi

    done

    local ld_tmp
    local py_tmp

    ld_tmp="$(mktemp)"
    py_tmp="$(mktemp)"

    # Generate LD block
    {
        echo "	/* generated by makeplugin.sh */"
        echo ""

        local plugin
        for plugin in "${plugin_ids[@]}"; do
            echo "	. = ALIGN(0x1000);"
            echo "	.plugin_${plugin} :"
            echo "	{"
            echo "		__plugin_${plugin}_start = .;"
            echo "		KEEP(*(.plugin_${plugin}_entry))"
            echo "		KEEP(*(.plugin_${plugin}))"
            echo "		KEEP(*(.plugin_${plugin}.*))"
            echo "		__plugin_${plugin}_end = .;"
            echo "	} : plugin"
            echo ""

            echo "	__plugin_${plugin}_pad_start = .;"
            echo "	. = ALIGN(0x1000);"
            echo "	__plugin_${plugin}_pad_end = .;"
            echo ""

            echo "	.pluginrodata_${plugin} :"
            echo "	{"
            echo "		__pluginrodata_${plugin}_start = .;"
            echo "		KEEP(*(.pluginrodata_${plugin}))"
            echo "		KEEP(*(.pluginrodata_${plugin}.*))"
            echo "		__pluginrodata_${plugin}_end = .;"
            echo "	} : plugin"
            echo ""

            echo "	.plugindata_${plugin} :"
            echo "	{"
            echo "		__plugindata_${plugin}_start = .;"
            echo "		KEEP(*(.plugindata_${plugin}))"
            echo "		KEEP(*(.plugindata_${plugin}.*))"
            echo "		__plugindata_${plugin}_end = .;"
            echo "	} : plugin"
            echo ""

            echo "	.pluginbss_${plugin} (NOLOAD) :"
            echo "	{"
            echo "		__pluginbss_${plugin}_start = .;"
            echo "		KEEP(*(.pluginbss_${plugin}))"
            echo "		KEEP(*(.pluginbss_${plugin}.*))"
            echo "		__pluginbss_${plugin}_end = .;"
            echo "	} : plugin"
            echo ""
            echo ""
        done
    } > "$ld_tmp"

    replace_between_markers "$ld_file" "$ld_tmp" "$LD_START_MARKER" "$LD_END_MARKER"

    echo "updated $ld_file from ${module_name} plugin config"

    # Generate Python plugin_defs block
    {
        echo "# generated by makeplugin.sh"
        echo "plugin_defs = ["

        for i in "${!plugin_ids[@]}"; do
            local pid="${plugin_ids[$i]}"
            local out_name="${plugin_output_names[$i]}"
            local priority="${plugin_priorities[$i]}"
            local allowed_refs="${plugin_allowed_refs[$i]}"
            local refs_py="\"$pid\""
            local refs

            IFS=',' read -ra refs <<< "$allowed_refs"

            local ref
            for ref in "${refs[@]}"; do
                ref="$(trim "$ref")"

                if [[ -z "$ref" ]]; then
                    continue
                fi

                if [[ "$ref" == "$pid" ]]; then
                    continue
                fi

                refs_py+=", \"$ref\""
            done

            echo "    (\"$pid\", \"$out_name\", $priority, [$refs_py]),"
        done

        echo "]"
    } > "$py_tmp"

    replace_between_markers "$create3nx_file" "$py_tmp" "$PY_START_MARKER" "$PY_END_MARKER"

    echo "updated $create3nx_file from ${module_name} plugin config"

    rm -f "$ld_tmp" "$py_tmp"

    if [[ "$(sha256_file_or_missing "$ld_file")" != "$old_ld_hash" ]]; then
        MODULE_RELINK_REQUIRED["$module_name"]=true
        BUILD_REASONS+=("${module_name}: plugin linker layout changed; relink only")
    fi

    if [[ "$(sha256_file_or_missing "$create3nx_file")" != "$old_py_hash" ]]; then
        BUILD_NOTES+=("${module_name}: .3nx packaging metadata changed")
    fi

    if [[ ! -f "$marker_script" ]]; then
        echo "missing marker generator: $marker_script"
        exit 1
    fi

    local marker_fp
    local previous_marker_fp
    local marker_source_fp_text
    local marker_compiler_fp
    local previous_marker_compiler_fp
    local marker_source_tmp
    marker_fp="$(python3 "$marker_script" --fingerprint)"
    previous_marker_fp="$(read_state "$marker_state")"
    marker_source_fp_text="$(python3 "$marker_script" --source-fingerprints)"
    marker_compiler_fp="$(python3 "$marker_script" --compiler-plugin-fingerprint)"
    previous_marker_compiler_fp="$(read_state "$marker_compiler_state")"
    MODULE_MARKER_FP["$module_name"]="$marker_fp"
    MODULE_MARKER_SOURCE_STATE["$module_name"]="$marker_source_fp_text"
    MODULE_MARKER_COMPILER_FP["$module_name"]="$marker_compiler_fp"

    marker_source_tmp="$(mktemp)"
    printf '%s' "$marker_source_fp_text" > "$marker_source_tmp"

    if [[ "$marker_fp" != "$previous_marker_fp" || ! -f "$marker_ld" ]]; then
        echo "${module_name}: semantic marker state changed; preparing linker placeholders"
        (cd "$module_dir" && python3 gen_plgmarkers.py --prepare)
        MODULE_RELINK_REQUIRED["$module_name"]=true
        BUILD_REASONS+=("${module_name}: semantic markers changed; selective recompile + relink")

        if [[ "$marker_compiler_fp" != "$previous_marker_compiler_fp" ]]; then
            echo "${module_name}: semantic GCC marker plugin changed; rebuilding compiler plugin"
            make -C "${ROOT_DIR}/sysplugin" tools
            MODULE_MARKER_COMPILER_CHANGED["$module_name"]=true
            echo "${module_name}: invalidating C/C++ objects; preserving assembly/data objects"
            invalidate_all_marker_compiled_objects "$module_name"
            BUILD_NOTES+=("${module_name}: semantic GCC marker plugin changed; C/C++ sources will recompile, assembly/data objects preserved")
        else
            local changed_marker_source
            while IFS= read -r changed_marker_source; do
                [[ -z "$changed_marker_source" ]] && continue
                echo "${module_name}: invalidating marked source object: ${changed_marker_source}"
                invalidate_module_object "$module_name" "$changed_marker_source"
            done < <(changed_marker_sources "$marker_source_state" "$marker_source_tmp")
        fi
    else
        echo "${module_name}: semantic marker source unchanged"
    fi

    rm -f -- "$marker_source_tmp"
}

normalize_stack_priority() {
    local raw="$1"
    local normalized="$raw"

    while [[ ${#normalized} -gt 1 && "${normalized:0:1}" == "0" ]]; do
        normalized="${normalized:1}"
    done

    if [[ ${#normalized} -gt 10 ||
          ( ${#normalized} -eq 10 && "$normalized" > "4294967295" ) ]]; then
        return 1
    fi

    printf '%s' "$normalized"
}

resolve_nonconfig_plugin_file() {
    local member_name="$1"
    local purpose="$2"
    local candidate
    local candidate_count=0
    local selected_path=""
    local selected_priority=""
    local selected_size=""
    local selected_hash=""
    local candidate_files=()

    for candidate in "${ROOT_DIR}/${member_name}."*.3nx; do
        [[ -f "$candidate" ]] || continue

        local basename="${candidate##*/}"
        local priority_part="${basename#"${member_name}."}"
        priority_part="${priority_part%.3nx}"
        [[ "$priority_part" =~ ^[0-9]+$ ]] || continue

        local priority
        if ! priority="$(normalize_stack_priority "$priority_part")"; then
            printf "ERROR: %s '%s' has out-of-range priority in filename: %s\n" \
                "$purpose" "$member_name" "$basename" >&2
            exit 1
        fi

        candidate_count=$((candidate_count + 1))
        candidate_files+=("$basename")

        if [[ "$candidate_count" -eq 1 ]]; then
            selected_path="$candidate"
            selected_priority="$priority"
            selected_size="$(stat -c '%s' -- "$candidate")"
            selected_hash="$(sha256sum "$candidate" | awk '{print $1}')"
        fi
    done

    if [[ "$candidate_count" -eq 0 ]]; then
        printf "ERROR: %s '%s' is not being built and no completed %s.<priority>.3nx exists beside makeplugin.sh\n" \
            "$purpose" "$member_name" "$member_name" >&2
        exit 1
    fi

    if [[ "$candidate_count" -gt 1 ]]; then
        printf "ERROR: %s '%s' matches multiple completed files; keep exactly one:\n" "$purpose" "$member_name" >&2
        local candidate_file
        for candidate_file in "${candidate_files[@]}"; do
            printf "  %s\n" "$candidate_file" >&2
        done
        exit 1
    fi

    inspect_stack_3nx "$selected_path"

    NONCONFIG_STACK_SOURCE="$selected_path"
    NONCONFIG_STACK_PRIORITY="$selected_priority"
    NONCONFIG_STACK_SIZE="$selected_size"
    NONCONFIG_STACK_HASH="$selected_hash"
    NONCONFIG_STACK_ENTRY_COUNT="$INSPECTED_STACK_ENTRY_COUNT"
    NONCONFIG_STACK_IDENTITIES="$INSPECTED_STACK_IDENTITIES"
}

prepare_metadata_outputs() {
    local entry

    for entry in "${METADATA_CONFIG[@]}"; do
        local fields=()
        IFS='|' read -ra fields <<< "$entry"
        if [[ ${#fields[@]} -lt 2 ]]; then
            printf 'bad metadata config %q: expected plugin_name|file1[|file2|...]\n' "$entry" >&2
            exit 1
        fi

        local plugin_name
        plugin_name="$(trim "${fields[0]}")"
        if [[ ! "$plugin_name" =~ ^[A-Za-z0-9_.-]+$ ]]; then
            printf "bad metadata plugin_name '%s': use only A-Z, a-z, 0-9, _, ., -\n" "$plugin_name" >&2
            exit 1
        fi

        local plugin_key="${plugin_name,,}"
        if [[ -n "${METADATA_TARGET_OUTPUTS[$plugin_key]+present}" ]]; then
            printf "bad metadata config: duplicate plugin_name '%s'\n" "$plugin_name" >&2
            exit 1
        fi

        local metadata_paths=()
        local field_index
        for ((field_index = 1; field_index < ${#fields[@]}; field_index++)); do
            local metadata_name
            metadata_name="$(trim "${fields[$field_index]}")"
            if [[ -z "$metadata_name" || "$metadata_name" == */* || "$metadata_name" == "." || "$metadata_name" == ".." ]]; then
                printf "bad metadata file '%s' for '%s': file must be directly beside makeplugin.sh\n" \
                    "$metadata_name" "$plugin_name" >&2
                exit 1
            fi

            local metadata_path="${ROOT_DIR}/${metadata_name}"
            if [[ ! -f "$metadata_path" ]]; then
                printf "ERROR: metadata file '%s' for '%s' is missing beside makeplugin.sh\n" \
                    "$metadata_name" "$plugin_name" >&2
                exit 1
            fi
            metadata_paths+=("$metadata_path")
        done

        if [[ ${#metadata_paths[@]} -eq 0 ]]; then
            printf "bad metadata config for '%s': at least one metadata file is required\n" "$plugin_name" >&2
            exit 1
        fi

        local kind
        local output_file
        local source
        local source_size="-"
        local source_hash="-"
        local identity

        if [[ -n "${PLUGIN_OUTPUT_FILES[$plugin_key]+present}" ]]; then
            kind="generated"
            output_file="${PLUGIN_OUTPUT_FILES[$plugin_key]}"
            source="$output_file"
            identity="${PLUGIN_OUTPUT_MODULES[$plugin_key]}:${PLUGIN_OUTPUT_IDS[$plugin_key]}"
        else
            resolve_nonconfig_plugin_file "$plugin_name" "metadata target"
            if [[ "$NONCONFIG_STACK_ENTRY_COUNT" -ne 1 ]]; then
                printf "WARNING: metadata target '%s' dropped: %s already contains %s .3nx entries\n" \
                    "$plugin_name" "${NONCONFIG_STACK_SOURCE##*/}" "$NONCONFIG_STACK_ENTRY_COUNT" >&2
                continue
            fi

            kind="nonconfig"
            output_file="${NONCONFIG_STACK_SOURCE##*/}"
            source="$NONCONFIG_STACK_SOURCE"
            source_size="$NONCONFIG_STACK_SIZE"
            source_hash="$NONCONFIG_STACK_HASH"
            identity="$NONCONFIG_STACK_IDENTITIES"
            METADATA_NONCONFIG_OUTPUTS+=("$output_file")
        fi

        local joined_metadata=""
        local metadata_path
        for metadata_path in "${metadata_paths[@]}"; do
            if [[ -z "$joined_metadata" ]]; then
                joined_metadata="$metadata_path"
            else
                joined_metadata+=$'\034'"$metadata_path"
            fi
        done

        METADATA_TARGET_KEYS+=("$plugin_key")
        METADATA_TARGET_OUTPUTS["$plugin_key"]="$output_file"
        METADATA_TARGET_KINDS["$plugin_key"]="$kind"
        METADATA_TARGET_SOURCES["$plugin_key"]="$source"
        METADATA_TARGET_SIZES["$plugin_key"]="$source_size"
        METADATA_TARGET_HASHES["$plugin_key"]="$source_hash"
        METADATA_TARGET_IDENTITIES["$plugin_key"]="$identity"
        METADATA_TARGET_FILES["$plugin_key"]="$joined_metadata"
    done
}

rewrite_3nx_metadata() {
    local path="$1"
    shift

    python3 - "$path" "$@" <<'PYMETA'
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
metadata_files = [pathlib.Path(arg) for arg in sys.argv[2:]]
data = bytearray(path.read_bytes())
HEADER_SIZE = 0x30
METADATA_SIZE_OFFSET = 0x2C

if len(data) < HEADER_SIZE:
    raise SystemExit(f"ERROR: truncated .3nx metadata target: {path}")

header = struct.unpack_from("<12I", data, 0)
magic, _pid, code_size, data_size, _bss_size, reloc_size, repair_size = header[:7]
old_metadata_size = header[11]
if magic not in (0x24584E33, 0x25584E33) or code_size == 0:
    raise SystemExit(f"ERROR: invalid .3nx metadata target: {path}")

raw_end = HEADER_SIZE + reloc_size + code_size + data_size + repair_size
body_end = (raw_end + 0xF) & ~0xF
old_end = body_end + old_metadata_size
if old_metadata_size & 0xF or raw_end > body_end or old_end != len(data):
    raise SystemExit(f"ERROR: metadata target is malformed or stacked: {path}")

metadata = bytearray()
for metadata_file in metadata_files:
    metadata += metadata_file.read_bytes()

metadata_size = (len(metadata) + 0xF) & ~0xF
if metadata_size > 0xFFFFFFFF:
    raise SystemExit(f"ERROR: metadata is too large for .3nx header: {metadata_size} bytes")

out = data[:body_end]
struct.pack_into("<I", out, METADATA_SIZE_OFFSET, metadata_size)
out += metadata
out += b"\0" * (metadata_size - len(metadata))
path.write_bytes(out)
print(metadata_size)
PYMETA
}

prepare_stacked_outputs() {
    local entry

    for entry in "${STACKED_PLUGIN_CONFIG[@]}"; do
        local stacked_name
        local stack_priority
        local members
        local first_rest

        if [[ "$entry" != *"|"* ]]; then
            printf 'bad stacked plugin config %q: expected output_name|priority|plugin_name,plugin_name[,...]\n' "$entry" >&2
            exit 1
        fi

        stacked_name="${entry%%|*}"
        first_rest="${entry#*|}"
        if [[ "$first_rest" == *"|"* ]]; then
            stack_priority="${first_rest%%|*}"
            members="${first_rest#*|}"
            if [[ "$members" == *"|"* ]]; then
                printf 'bad stacked plugin config %q: expected output_name|priority|plugin_name,plugin_name[,...]\n' "$entry" >&2
                exit 1
            fi
        else
            # Backward-compatible old form: output_name|members means empty priority.
            stack_priority=""
            members="$first_rest"
        fi

        stacked_name="$(trim "$stacked_name")"
        stack_priority="$(trim "$stack_priority")"
        members="$(trim "$members")"

        if [[ -n "$stack_priority" ]]; then
            if [[ ! "$stack_priority" =~ ^[0-9]+$ ]]; then
                printf "bad stacked priority '%s' for '%s': must be a non-negative integer or empty\n" \
                    "$stack_priority" "$stacked_name" >&2
                exit 1
            fi
            local raw_stack_priority="$stack_priority"
            if ! stack_priority="$(normalize_stack_priority "$raw_stack_priority")"; then
                printf "bad stacked priority '%s' for '%s': maximum is 4294967295\n" \
                    "$raw_stack_priority" "$stacked_name" >&2
                exit 1
            fi
        fi

        if [[ ! "$stacked_name" =~ ^[A-Za-z0-9_.-]+$ ]]; then
            printf "bad stacked output_name '%s': use only A-Z, a-z, 0-9, _, ., -\n" "$stacked_name" >&2
            exit 1
        fi

        local stacked_key="${stacked_name,,}"
        if [[ -n "${OUTPUT_OWNERS[$stacked_key]+present}" ]]; then
            printf "bad stacked output_name '%s': collides with %s\n" "$stacked_name" "${OUTPUT_OWNERS[$stacked_key]}" >&2
            exit 1
        fi
        OUTPUT_OWNERS["$stacked_key"]="stacked output"

        local member_names=()
        IFS=',' read -ra member_names <<< "$members"
        if [[ ${#member_names[@]} -lt 2 ]]; then
            printf "bad stacked plugin config for '%s': at least two plugin names are required\n" "$stacked_name" >&2
            exit 1
        fi

        local -A seen_members=()
        local -A identity_counts=()
        local -A identity_files=()
        local records=()
        local original_index=0
        local member_name

        for member_name in "${member_names[@]}"; do
            member_name="$(trim "$member_name")"
            if [[ ! "$member_name" =~ ^[A-Za-z0-9_.-]+$ ]]; then
                printf "bad stack member '%s' for '%s': use only A-Z, a-z, 0-9, _, ., -\n" \
                    "$member_name" "$stacked_name" >&2
                exit 1
            fi

            local member_key="${member_name,,}"
            if [[ -n "${seen_members[$member_key]+present}" ]]; then
                printf "bad stacked plugin config for '%s': duplicate plugin name '%s'\n" \
                    "$stacked_name" "$member_name" >&2
                exit 1
            fi
            seen_members["$member_key"]=1

            local source
            local priority
            local size="-"
            local hash="-"
            local kind
            local generated_output="-"
            local identity
            local display_file
            local configured=false

            if [[ -n "${PLUGIN_OUTPUT_FILES[$member_key]+present}" ]]; then
                configured=true
            fi

            if [[ "$configured" == true ]]; then
                kind="staged"
                source="${PLUGIN_OUTPUT_FILES[$member_key]}"
                priority="${PLUGIN_OUTPUT_PRIORITIES[$member_key]}"
                generated_output="$source"
                identity="${PLUGIN_OUTPUT_MODULES[$member_key]}:${PLUGIN_OUTPUT_IDS[$member_key]}"
                display_file="$source"
            else
                resolve_nonconfig_plugin_file "$member_name" "stack member"
                source="$NONCONFIG_STACK_SOURCE"
                priority="$NONCONFIG_STACK_PRIORITY"
                size="$NONCONFIG_STACK_SIZE"
                hash="$NONCONFIG_STACK_HASH"
                display_file="${source##*/}"

                if [[ "$NONCONFIG_STACK_ENTRY_COUNT" -ne 1 ]]; then
                    printf "WARNING: stack member '%s' dropped: %s already contains %s .3nx entries\n" \
                        "$member_name" "$display_file" "$NONCONFIG_STACK_ENTRY_COUNT" >&2
                    continue
                fi

                identity="$NONCONFIG_STACK_IDENTITIES"
                if [[ -n "${METADATA_TARGET_OUTPUTS[$member_key]+present}" && \
                      "${METADATA_TARGET_KINDS[$member_key]}" == "nonconfig" ]]; then
                    kind="metadata"
                    source="${METADATA_TARGET_OUTPUTS[$member_key]}"
                    size="-"
                    hash="-"
                else
                    kind="nonconfig"
                fi
            fi

            identity_counts["$identity"]=$(( ${identity_counts[$identity]:-0} + 1 ))
            if [[ -z "${identity_files[$identity]:-}" ]]; then
                identity_files["$identity"]="$display_file"
            else
                identity_files["$identity"]+=$'\034'"$display_file"
            fi

            records+=("${priority}"$'\t'"${original_index}"$'\t'"${kind}"$'\t'"${source}"$'\t'"${size}"$'\t'"${hash}"$'\t'"${member_name}"$'\t'"${generated_output}"$'\t'"${identity}"$'\t'"${display_file}")
            original_index=$((original_index + 1))
        done

        local duplicate_identities=()
        local identity_key
        for identity_key in "${!identity_counts[@]}"; do
            if [[ "${identity_counts[$identity_key]}" -gt 1 ]]; then
                duplicate_identities+=("$identity_key")
            fi
        done

        if [[ ${#duplicate_identities[@]} -gt 0 ]]; then
            mapfile -t duplicate_identities < <(printf '%s\n' "${duplicate_identities[@]}" | LC_ALL=C sort)
            printf "ERROR: stacking '%s' failed: duplicate module+ID entries:\n" "$stacked_name" >&2
            for identity_key in "${duplicate_identities[@]}"; do
                local duplicate_files=()
                IFS=$'\034' read -ra duplicate_files <<< "${identity_files[$identity_key]}"
                printf '  %s: ' "$identity_key" >&2
                local duplicate_index
                for duplicate_index in "${!duplicate_files[@]}"; do
                    if [[ "$duplicate_index" -gt 0 ]]; then
                        printf ', ' >&2
                    fi
                    printf '%s' "${duplicate_files[$duplicate_index]}" >&2
                done
                printf '\n' >&2
            done
            exit 1
        fi

        if [[ ${#records[@]} -lt 2 ]]; then
            printf "ERROR: stacking '%s' failed: fewer than two usable members remain after dropping already-stacked inputs\n" \
                "$stacked_name" >&2
            exit 1
        fi

        local sorted_records=()
        mapfile -t sorted_records < <(printf '%s\n' "${records[@]}" | LC_ALL=C sort -t$'\t' -k1,1n -k2,2n)

        local stack_index="${#STACKED_OUTPUT_FILES[@]}"
        local lowest_priority=""
        local description=""
        local member_index=0
        local record

        for record in "${sorted_records[@]}"; do
            local rec_priority
            local rec_original_index
            local rec_kind
            local rec_source
            local rec_size
            local rec_hash
            local rec_name
            local rec_generated
            local rec_identity
            local rec_file
            IFS=$'\t' read -r rec_priority rec_original_index rec_kind rec_source rec_size rec_hash rec_name rec_generated rec_identity rec_file <<< "$record"

            if [[ -z "$lowest_priority" ]]; then
                lowest_priority="$rec_priority"
            fi

            STACKED_MEMBER_SOURCES["${stack_index}:${member_index}"]="$rec_source"
            STACKED_MEMBER_KINDS["${stack_index}:${member_index}"]="$rec_kind"
            STACKED_MEMBER_SIZES["${stack_index}:${member_index}"]="$rec_size"
            STACKED_MEMBER_HASHES["${stack_index}:${member_index}"]="$rec_hash"
            STACKED_MEMBER_IDENTITIES["${stack_index}:${member_index}"]="$rec_identity"
            STACKED_MEMBER_FILES["${stack_index}:${member_index}"]="$rec_file"

            if [[ "$rec_kind" == "staged" ]]; then
                STACKED_MEMBER_OUTPUTS["$rec_generated"]=1
            fi

            if [[ -z "$description" ]]; then
                description="$rec_name"
            else
                description+=" + ${rec_name}"
            fi

            member_index=$((member_index + 1))
        done

        STACKED_MEMBER_COUNTS["$stack_index"]="$member_index"

        local final_priority="$lowest_priority"
        if [[ -n "$stack_priority" ]]; then
            final_priority="$stack_priority"
        fi
        local final_name="${stacked_name}.${final_priority}.3nx"
        local metadata_output
        for metadata_output in "${METADATA_NONCONFIG_OUTPUTS[@]}"; do
            if [[ "$final_name" == "$metadata_output" ]]; then
                printf "bad stacked output filename '%s': collides with metadata-updated plugin output\n" "$final_name" >&2
                exit 1
            fi
        done
        if [[ ${#final_name} -ge 256 ]]; then
            printf "bad stacked output filename '%s': must be shorter than 256 ASCII bytes\n" "$final_name" >&2
            exit 1
        fi

        STACKED_OUTPUT_FILES+=("$final_name")
        STACKED_DESCRIPTIONS+=("$description")
    done

    local output_name
    for output_name in "${GENERATED_OUTPUTS[@]}"; do
        if [[ -z "${STACKED_MEMBER_OUTPUTS[$output_name]+present}" ]]; then
            PUBLISHED_INDIVIDUAL_OUTPUTS+=("$output_name")
        fi
    done

    PUBLISHED_OUTPUTS=(
        "${PUBLISHED_INDIVIDUAL_OUTPUTS[@]}"
        "${METADATA_NONCONFIG_OUTPUTS[@]}"
        "${STACKED_OUTPUT_FILES[@]}"
    )
}

process_module_plugins "rosalina" ROSALINA_PLUGIN_CONFIG
process_module_plugins "loader" LOADER_PLUGIN_CONFIG

prepare_metadata_outputs
prepare_stacked_outputs

printf '\n'
for note in "${BUILD_NOTES[@]}"; do
    printf '%s\n' "$note"
done
if [[ ${#BUILD_NOTES[@]} -gt 0 ]]; then
    printf '\n'
fi

total_emit_count=$((MODULE_EMIT_COUNTS[rosalina] + MODULE_EMIT_COUNTS[loader]))
if [[ "$total_emit_count" -eq 0 && "$total_config_count" -eq 0 ]]; then
    printf 'No module plugins configured; completed .3nx files will be used for configured metadata/stack operations.\n\n'
elif [[ ${#BUILD_REASONS[@]} -gt 0 ]]; then
    printf 'Selective rebuild/relink required:\n'
    for reason in "${BUILD_REASONS[@]}"; do
        printf '  - %s\n' "$reason"
    done
    printf '\n'
else
    printf 'Plugin layout and host markers unchanged; building modules.\n\n'
fi

# A normal top-level Nexus build passes these version values down through
# sysmodules/Makefile.  Read the same values here so an ELF-only plugin build uses
# exactly the same compile-time version defines as boot.firm.
read_version_var() {
    local name="$1"
    local value
    value="$(sed -n -E "s/^${name}[[:space:]]*:=[[:space:]]*(.*)$/\1/p" version.mk | head -n 1)"
    if [[ -z "$value" ]]; then
        printf 'ERROR: could not read %s from version.mk\n' "$name" >&2
        exit 1
    fi
    printf '%s' "$value"
}

NEXUS_VERSION_MAJOR="$(read_version_var NEXUS_VERSION_MAJOR)"
NEXUS_VERSION_MINOR="$(read_version_var NEXUS_VERSION_MINOR)"
NEXUS_VERSION_BUILD="$(read_version_var NEXUS_VERSION_BUILD)"
LUMA_VERSION_MAJOR="$(read_version_var LUMA_VERSION_MAJOR)"
LUMA_VERSION_MINOR="$(read_version_var LUMA_VERSION_MINOR)"
LUMA_VERSION_BUILD="$(read_version_var LUMA_VERSION_BUILD)"

build_module_elf() {
    local module_name="$1"
    local relink_required="${MODULE_RELINK_REQUIRED[$module_name]}"

    if [[ "$relink_required" == true ]]; then
        printf 'Relinking %s while preserving reusable objects...\n' "$module_name"
        rm -f -- "./sysmodules/${module_name}/${module_name}.elf"
    else
        printf 'Building %s ELF...\n' "$module_name"
    fi

    local marker_make_override=()
    if [[ "${MODULE_MARKER_COMPILER_CHANGED[$module_name]}" == true ]]; then
        local marker_plugin_path="${ROOT_DIR}/sysplugin/build/semantic_marker_plugin.so"
        # The recursive module Makefile currently lists the semantic GCC plugin as a
        # prerequisite of every source object, including assembly.  We already rebuilt
        # the plugin above and explicitly removed every C/C++ object whose semantic
        # manifest depends on it.  Treat the plugin as an old prerequisite in the
        # recursive make so unrelated assembly objects are not rebuilt solely because
        # the .so acquired a newer timestamp.
        marker_make_override+=("MAKE=make --old-file=${marker_plugin_path}")
    fi

    make -C "./sysmodules/${module_name}" elf "${marker_make_override[@]}" \
        NEXUS_VERSION_MAJOR="$NEXUS_VERSION_MAJOR" \
        NEXUS_VERSION_MINOR="$NEXUS_VERSION_MINOR" \
        NEXUS_VERSION_BUILD="$NEXUS_VERSION_BUILD" \
        LUMA_VERSION_MAJOR="$LUMA_VERSION_MAJOR" \
        LUMA_VERSION_MINOR="$LUMA_VERSION_MINOR" \
        LUMA_VERSION_BUILD="$LUMA_VERSION_BUILD"
}

OUTPUT_STAGE="$(mktemp -d "${ROOT_DIR}/.3nx-stage.XXXXXX")"
cleanup_output_stage() {
    if [[ -d "$OUTPUT_STAGE" ]]; then
        # OUTPUT_STAGE is an exact mktemp directory owned by this invocation.
        # Remove both completed files and create3nx's private temporary folder.
        find "$OUTPUT_STAGE" -mindepth 1 -depth -delete
        rmdir -- "$OUTPUT_STAGE" 2>/dev/null || true
    fi
}
trap cleanup_output_stage EXIT

for module_name in rosalina loader; do
    emit_count="${MODULE_EMIT_COUNTS[$module_name]:-0}"
    if [[ "$emit_count" -gt 0 ]]; then
        build_module_elf "$module_name"
    fi
done

for module_name in rosalina loader; do
    emit_count="${MODULE_EMIT_COUNTS[$module_name]:-0}"
    if [[ "$emit_count" -gt 0 ]]; then
        printf '\nResolving %s semantic markers...\n' "$module_name"
        (cd "./sysmodules/${module_name}" && python3 gen_plgmarkers.py --resolve)
    fi
done

for module_name in rosalina loader; do
    emit_count="${MODULE_EMIT_COUNTS[$module_name]:-0}"
    if [[ "$emit_count" -gt 0 ]]; then
        printf '\nGenerating %s .3nx files...\n' "$module_name"
        (
            cd "./sysmodules/${module_name}"
            NEXUS_3NX_OUTPUT_DIR="$OUTPUT_STAGE" python3 create3nx.py
        )
    fi
done

for output_name in "${GENERATED_OUTPUTS[@]}"; do
    if [[ ! -f "${OUTPUT_STAGE}/${output_name}" ]]; then
        printf 'ERROR: create3nx.py did not produce expected output %s\n' "$output_name" >&2
        exit 1
    fi
done

verify_nonconfig_source() {
    local source_path="$1"
    local expected_size="$2"
    local expected_hash="$3"
    local purpose="$4"

    if [[ ! -f "$source_path" ]]; then
        printf 'ERROR: %s disappeared after validation: %s\n' "$purpose" "$source_path" >&2
        exit 1
    fi
    if [[ "$(stat -c '%s' -- "$source_path")" != "$expected_size" ||
          "$(sha256sum "$source_path" | awk '{print $1}')" != "$expected_hash" ]]; then
        printf 'ERROR: %s changed after validation: %s\n' "$purpose" "$source_path" >&2
        exit 1
    fi
}

for plugin_key in "${METADATA_TARGET_KEYS[@]}"; do
    metadata_output="${METADATA_TARGET_OUTPUTS[$plugin_key]}"
    metadata_kind="${METADATA_TARGET_KINDS[$plugin_key]}"
    metadata_identity="${METADATA_TARGET_IDENTITIES[$plugin_key]}"
    metadata_stage="${OUTPUT_STAGE}/${metadata_output}"

    if [[ "$metadata_kind" == "generated" ]]; then
        if [[ ! -f "$metadata_stage" ]]; then
            printf 'ERROR: generated metadata target is missing from staging: %s\n' "$metadata_output" >&2
            exit 1
        fi
    else
        metadata_source="${METADATA_TARGET_SOURCES[$plugin_key]}"
        verify_nonconfig_source \
            "$metadata_source" \
            "${METADATA_TARGET_SIZES[$plugin_key]}" \
            "${METADATA_TARGET_HASHES[$plugin_key]}" \
            "metadata target"
        cp -- "$metadata_source" "$metadata_stage"
    fi

    inspect_stack_3nx "$metadata_stage"
    if [[ "$INSPECTED_STACK_ENTRY_COUNT" -ne 1 ||
          "$INSPECTED_STACK_IDENTITIES" != "$metadata_identity" ]]; then
        printf 'ERROR: metadata target %s is not exactly the expected single plugin %s\n' \
            "$metadata_output" "$metadata_identity" >&2
        exit 1
    fi

    metadata_files=()
    IFS=$'\034' read -ra metadata_files <<< "${METADATA_TARGET_FILES[$plugin_key]}"
    metadata_size="$(rewrite_3nx_metadata "$metadata_stage" "${metadata_files[@]}")"

    inspect_stack_3nx "$metadata_stage"
    if [[ "$INSPECTED_STACK_ENTRY_COUNT" -ne 1 ||
          "$INSPECTED_STACK_IDENTITIES" != "$metadata_identity" ]]; then
        printf 'ERROR: metadata rewrite changed plugin identity for %s\n' "$metadata_output" >&2
        exit 1
    fi
    printf 'Packed %s bytes of metadata into %s\n' "$metadata_size" "$metadata_output"
done

for i in "${!STACKED_OUTPUT_FILES[@]}"; do
    stack_path="${OUTPUT_STAGE}/${STACKED_OUTPUT_FILES[$i]}"
    : > "$stack_path"

    member_count="${STACKED_MEMBER_COUNTS[$i]}"
    for ((member_index = 0; member_index < member_count; member_index++)); do
        member_key="${i}:${member_index}"
        source="${STACKED_MEMBER_SOURCES[$member_key]}"

        if [[ "${STACKED_MEMBER_KINDS[$member_key]}" == "staged" ||
              "${STACKED_MEMBER_KINDS[$member_key]}" == "metadata" ]]; then
            source="${OUTPUT_STAGE}/${source}"
            if [[ ! -f "$source" ]]; then
                printf 'ERROR: staged stack member is missing: %s\n' "$source" >&2
                exit 1
            fi
            inspect_stack_3nx "$source"
            if [[ "$INSPECTED_STACK_ENTRY_COUNT" -ne 1 ||
                  "$INSPECTED_STACK_IDENTITIES" != "${STACKED_MEMBER_IDENTITIES[$member_key]}" ]]; then
                printf 'ERROR: staged stack member %s does not contain exactly the expected identity %s\n' \
                    "${STACKED_MEMBER_FILES[$member_key]}" "${STACKED_MEMBER_IDENTITIES[$member_key]}" >&2
                exit 1
            fi
        else
            verify_nonconfig_source \
                "$source" \
                "${STACKED_MEMBER_SIZES[$member_key]}" \
                "${STACKED_MEMBER_HASHES[$member_key]}" \
                "non-config stack member"
        fi

        cat -- "$source" >> "$stack_path"
    done
done

for output_name in "${!STACKED_MEMBER_OUTPUTS[@]}"; do
    if [[ -f "${ROOT_DIR}/${output_name}" ]]; then
        rm -f -- "${ROOT_DIR}/${output_name}"
    fi
done

OUTPUT_MANIFEST="${STATE_DIR}/outputs.list"
if [[ -f "$OUTPUT_MANIFEST" ]]; then
    while IFS= read -r old_output; do
        [[ -z "$old_output" ]] && continue

        still_generated=false
        for output_name in "${PUBLISHED_OUTPUTS[@]}"; do
            if [[ "$old_output" == "$output_name" ]]; then
                still_generated=true
                break
            fi
        done

        if [[ "$still_generated" == false && -f "${ROOT_DIR}/${old_output}" ]]; then
            printf 'stale output remains: %s\n' "${old_output}" >&2
        fi
    done < "$OUTPUT_MANIFEST"
fi
printf '\n'
for output_name in "${PUBLISHED_INDIVIDUAL_OUTPUTS[@]}"; do
    mv -f -- "${OUTPUT_STAGE}/${output_name}" "${ROOT_DIR}/${output_name}"
    printf 'Wrote %s\n' "$output_name"
done

for output_name in "${METADATA_NONCONFIG_OUTPUTS[@]}"; do
    mv -f -- "${OUTPUT_STAGE}/${output_name}" "${ROOT_DIR}/${output_name}"
    printf 'Wrote metadata-updated %s\n' "$output_name"
done

for i in "${!STACKED_OUTPUT_FILES[@]}"; do
    output_name="${STACKED_OUTPUT_FILES[$i]}"
    mv -f -- "${OUTPUT_STAGE}/${output_name}" "${ROOT_DIR}/${output_name}"
    printf 'Wrote %s > %s\n' "${STACKED_DESCRIPTIONS[$i]}" "$output_name"
done

cleanup_output_stage
trap - EXIT

for module_name in "${!MODULE_CONFIG_FP[@]}"; do
    write_state "${STATE_DIR}/${module_name}.config.sha256" "${MODULE_CONFIG_FP[$module_name]}"
    write_state "${STATE_DIR}/${module_name}.markers.sha256" "${MODULE_MARKER_FP[$module_name]}"
    write_state "${STATE_DIR}/${module_name}.marker-sources.tsv" "${MODULE_MARKER_SOURCE_STATE[$module_name]}"
    write_state "${STATE_DIR}/${module_name}.marker-compiler.sha256" "${MODULE_MARKER_COMPILER_FP[$module_name]}"
done

if [[ ${#PUBLISHED_OUTPUTS[@]} -eq 0 ]]; then
    : > "${STATE_DIR}/outputs.list"
else
    : > "${STATE_DIR}/outputs.list"
    first_output=true
    for output_name in "${PUBLISHED_OUTPUTS[@]}"; do
        if [[ "$first_output" == true ]]; then
            first_output=false
        else
            printf '\n' >> "${STATE_DIR}/outputs.list"
        fi
        printf '%s' "$output_name" >> "${STATE_DIR}/outputs.list"
    done
fi

printf '\nDone. Re-run ./makeplugin.sh for every plugin build.\n\n'
