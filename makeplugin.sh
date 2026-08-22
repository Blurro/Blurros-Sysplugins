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
#       Plugin IDs this plugin may reference, excluding itself.
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
    "MENU|ModMenu|0|"
    "blur|blurPLGbase|10|MENU"
    "coin|coinrosalina|50|blur,MENU"
    "powr|PowerPrevent|60|MENU"
    "onln|onlineblurrotemporary|999|"
)

LOADER_PLUGIN_CONFIG=(
    "coin|coinloader|50|"
)

# Tip: 3nx file data can be stacked, to make one .3nx file hold multiple plugins. Place generated .3nx files at SD:/luma/plugins/
# Stacked plugins in order of pluginA then pluginB, function the same as pluginA.1.3nx then pluginB.2.3nx
# A stack may contain any two or more output names. Configured members use the files built by this run, stacked by config priorities.
# Other non-config members are stacked by lowest-priority completed output_name.<priority>.3nx beside this script (then by filename).
# Already-stacked non-config inputs are warned and dropped. Module+ID dupes fail the stack.

STACKED_PLUGIN_CONFIG=(
    "Playcoinz|blurPLGbase,coinloader,coinrosalina"
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
# - clean-builds if marked host source or marker tooling changed
# - clean-builds if plugin IDs/order changed enough to change the linker layout
# - does NOT clean-build for output name / priority / allowed_refs-only changes
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
total_stack_count=${#STACKED_PLUGIN_CONFIG[@]}

if [[ "$total_config_count" -eq 0 && "$total_stack_count" -eq 0 ]]; then
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

declare -A MODULE_CONFIG_FP=()
declare -A MODULE_MARKER_FP=()
declare -A MODULE_CLEAN_REQUIRED=([rosalina]=false [loader]=false)
declare -A MODULE_EMIT_COUNTS=([rosalina]=0 [loader]=0)
declare -A OUTPUT_OWNERS=()
declare -A PLUGIN_OUTPUT_FILES=()
declare -A PLUGIN_OUTPUT_PRIORITIES=()
declare -A PLUGIN_OUTPUT_MODULES=()
declare -A PLUGIN_OUTPUT_NAMES=()
declare -A PLUGIN_OUTPUT_IDS=()
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
HEADER_SIZE = 0x2C
entries = []
pos = 0

if not data:
    raise SystemExit(f"ERROR: empty .3nx stack input: {path}")

while pos < len(data):
    if len(data) - pos < HEADER_SIZE:
        raise SystemExit(f"ERROR: truncated .3nx entry header at 0x{pos:X}: {path}")

    magic, _pid_word, code_size, data_size, _bss_size, reloc_size, repair_size, _abi_lo, _abi_hi, _env_lo, _env_hi = struct.unpack_from("<11I", data, pos)
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
    entry_end = (raw_end + 0xF) & ~0xF
    if raw_end < pos or entry_end <= pos or entry_end > len(data):
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

trim_final_line_endings() {
    local path="$1"
    local last_byte

    while [[ -s "$path" ]]; do
        last_byte="$(LC_ALL=C tail -c 1 -- "$path" | od -An -tu1 | tr -d '[:space:]')"
        if [[ "$last_byte" != "10" && "$last_byte" != "13" ]]; then
            break
        fi
        truncate -s -1 -- "$path"
    done
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
    trim_final_line_endings "$target_file"
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

            if [[ "$found" != true ]]; then
                echo "bad allowed_refs for $pid in ${module_name}: unknown plugin '$ref'"
                exit 1
            fi
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
        MODULE_CLEAN_REQUIRED["$module_name"]=true
        BUILD_REASONS+=("${module_name}: plugin linker layout changed")
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
    marker_fp="$(python3 "$marker_script" --fingerprint)"
    previous_marker_fp="$(read_state "$marker_state")"
    MODULE_MARKER_FP["$module_name"]="$marker_fp"

    if [[ "$marker_fp" != "$previous_marker_fp" || ! -f "$marker_ld" ]]; then
        echo "${module_name}: semantic marker state changed; preparing linker placeholders"
        (cd "$module_dir" && python3 gen_plgmarkers.py --prepare)
        MODULE_CLEAN_REQUIRED["$module_name"]=true
        BUILD_REASONS+=("${module_name}: semantic markers changed")
    else
        echo "${module_name}: semantic marker source unchanged"
    fi
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

resolve_nonconfig_stack_member() {
    local member_name="$1"
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
            printf "ERROR: stack member '%s' has out-of-range priority in filename: %s\n" \
                "$member_name" "$basename" >&2
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
        printf "ERROR: stack member '%s' is not being built and no completed %s.<priority>.3nx exists beside makeplugin.sh\n" \
            "$member_name" "$member_name" >&2
        exit 1
    fi

    if [[ "$candidate_count" -gt 1 ]]; then
        printf "ERROR: stack member '%s' matches multiple completed files; keep exactly one:\n" "$member_name" >&2
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

prepare_stacked_outputs() {
    local entry

    for entry in "${STACKED_PLUGIN_CONFIG[@]}"; do
        if [[ "$entry" != *"|"* || "${entry#*|}" == *"|"* ]]; then
            printf 'bad stacked plugin config %q: expected output_name|plugin_name,plugin_name[,...]\n' "$entry" >&2
            exit 1
        fi

        local stacked_name="${entry%%|*}"
        local members="${entry#*|}"
        stacked_name="$(trim "$stacked_name")"
        members="$(trim "$members")"

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
                kind="nonconfig"
                resolve_nonconfig_stack_member "$member_name"
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

        local final_name="${stacked_name}.${lowest_priority}.3nx"
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
        "${STACKED_OUTPUT_FILES[@]}"
    )
}

process_module_plugins "rosalina" ROSALINA_PLUGIN_CONFIG
process_module_plugins "loader" LOADER_PLUGIN_CONFIG

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
    printf 'No module plugins configured; explicitly named completed .3nx files will be used as stack members.\n\n'
elif [[ ${#BUILD_REASONS[@]} -gt 0 ]]; then
    printf 'Clean module build required:\n'
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
    local clean_required="${MODULE_CLEAN_REQUIRED[$module_name]}"

    if [[ "$clean_required" == true ]]; then
        printf 'Cleaning %s...\n' "$module_name"
        make -C "./sysmodules/${module_name}" clean
    else
        printf 'Building %s ELF...\n' "$module_name"
    fi

    make -C "./sysmodules/${module_name}" elf \
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

verify_nonconfig_stack_source() {
    local source_path="$1"
    local expected_size="$2"
    local expected_hash="$3"

    if [[ ! -f "$source_path" ]]; then
        printf 'ERROR: non-config stack member disappeared after validation: %s\n' "$source_path" >&2
        exit 1
    fi
    if [[ "$(stat -c '%s' -- "$source_path")" != "$expected_size" ||
          "$(sha256sum "$source_path" | awk '{print $1}')" != "$expected_hash" ]]; then
        printf 'ERROR: non-config stack member changed after validation: %s\n' "$source_path" >&2
        exit 1
    fi
}

for i in "${!STACKED_OUTPUT_FILES[@]}"; do
    stack_path="${OUTPUT_STAGE}/${STACKED_OUTPUT_FILES[$i]}"
    : > "$stack_path"

    member_count="${STACKED_MEMBER_COUNTS[$i]}"
    for ((member_index = 0; member_index < member_count; member_index++)); do
        member_key="${i}:${member_index}"
        source="${STACKED_MEMBER_SOURCES[$member_key]}"

        if [[ "${STACKED_MEMBER_KINDS[$member_key]}" == "staged" ]]; then
            source="${OUTPUT_STAGE}/${source}"
            if [[ ! -f "$source" ]]; then
                printf 'ERROR: generated stack member is missing from staging: %s\n' "$source" >&2
                exit 1
            fi
            inspect_stack_3nx "$source"
            if [[ "$INSPECTED_STACK_ENTRY_COUNT" -ne 1 ||
                  "$INSPECTED_STACK_IDENTITIES" != "${STACKED_MEMBER_IDENTITIES[$member_key]}" ]]; then
                printf 'ERROR: generated stack member %s does not contain exactly the expected identity %s\n' \
                    "${STACKED_MEMBER_FILES[$member_key]}" "${STACKED_MEMBER_IDENTITIES[$member_key]}" >&2
                exit 1
            fi
        else
            verify_nonconfig_stack_source \
                "$source" \
                "${STACKED_MEMBER_SIZES[$member_key]}" \
                "${STACKED_MEMBER_HASHES[$member_key]}"
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