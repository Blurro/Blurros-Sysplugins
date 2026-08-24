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
    printf 'ERROR: makeblurroextras.sh requires Bash 4 or newer.\n' >&2
    exit 1
fi

MAIN_BUILDER="${SCRIPT_DIR}/makeplugin.sh"
TRANSIENT_TOOL="${SCRIPT_DIR}/sysplugin/make_transient_3on.py"
HTTPSLIB_SOURCE="${SCRIPT_DIR}/sysmodules/rosalina/source/httpslib.c"
HTTPSLIB_BUILD="${SCRIPT_DIR}/sysmodules/rosalina/httpslib_tls/build.sh"
ONLINE_SOURCE="${SCRIPT_DIR}/sysmodules/rosalina/source/blurro_online.c"
ROSALINA_DIR="${SCRIPT_DIR}/sysmodules/rosalina"

for required in "$MAIN_BUILDER" "$TRANSIENT_TOOL" "$HTTPSLIB_SOURCE" "$HTTPSLIB_BUILD" "$ONLINE_SOURCE"; do
    if [[ ! -f "$required" ]]; then
        printf 'ERROR: required Blurro extras file is missing: %s\n' "$required" >&2
        exit 1
    fi
done

if ! grep -q 'PLUGIN_MAIN(htps)' "$HTTPSLIB_SOURCE"; then
    printf 'ERROR: httpslib.c does not contain PLUGIN_MAIN(htps).\n' >&2
    exit 1
fi
if ! grep -q 'PLUGIN_MAIN(onln)' "$ONLINE_SOURCE"; then
    printf 'ERROR: blurro_online.c does not contain PLUGIN_MAIN(onln).\n' >&2
    exit 1
fi

# This helper deliberately leaves the public makeplugin.sh stock. It makes a private,
# temporary clone configured only for the two transient plugins, builds them using the
# exact same dev-kit logic, converts them to 3NX&, then restores every generated source
# file the stock builder touched. A separate state directory keeps normal plugin build
# fingerprints/output history completely independent.
TEMP_BUILDER="$(mktemp "${SCRIPT_DIR}/.makeblurroextras.makeplugin.XXXXXX")"
BACKUP_DIR="$(mktemp -d "${SCRIPT_DIR}/.makeblurroextras.backup.XXXXXX")"
TEMP_HTTPS_OUT="${SCRIPT_DIR}/.httpslib.3on.new.$$"
TEMP_ONLINE_OUT="${SCRIPT_DIR}/.onlineblurrotemporary.3on.new.$$"
EXTRA_STATE_DIR="${SCRIPT_DIR}/.plgbuild-blurroextras"

GENERATED_FILES=(
    "sysmodules/rosalina/3dsx.ld"
    "sysmodules/rosalina/create3nx.py"
    "sysmodules/rosalina/plgmarkers.ld"
)

: > "${BACKUP_DIR}/existing.list"
for rel in "${GENERATED_FILES[@]}"; do
    if [[ -e "${SCRIPT_DIR}/${rel}" || -L "${SCRIPT_DIR}/${rel}" ]]; then
        mkdir -p "${BACKUP_DIR}/$(dirname -- "$rel")"
        cp -a -- "${SCRIPT_DIR}/${rel}" "${BACKUP_DIR}/${rel}"
        printf '%s\n' "$rel" >> "${BACKUP_DIR}/existing.list"
    fi
done

cleanup() {
    local status=$?
    trap - EXIT
    set +e

    rm -f -- "$TEMP_BUILDER" "$TEMP_HTTPS_OUT" "$TEMP_ONLINE_OUT"
    rm -f -- "${SCRIPT_DIR}/httpslib.998.3nx" "${SCRIPT_DIR}/onlineblurrotemporary.999.3nx"
    rm -rf -- "$EXTRA_STATE_DIR"

    # Do not leave an ELF/build directory whose linker layout was the private htps/onln layout.
    # The next normal ./makeplugin.sh invocation will rebuild from its untouched stock config.
    make -C "$ROSALINA_DIR" clean >/dev/null 2>&1 || true

    for rel in "${GENERATED_FILES[@]}"; do
        rm -f -- "${SCRIPT_DIR}/${rel}"
    done
    while IFS= read -r rel; do
        [[ -n "$rel" ]] || continue
        mkdir -p "${SCRIPT_DIR}/$(dirname -- "$rel")"
        cp -a -- "${BACKUP_DIR}/${rel}" "${SCRIPT_DIR}/${rel}"
    done < "${BACKUP_DIR}/existing.list"

    rm -rf -- "$BACKUP_DIR"
    exit "$status"
}
trap cleanup EXIT INT TERM HUP

cp -- "$MAIN_BUILDER" "$TEMP_BUILDER"
chmod +x "$TEMP_BUILDER"

python3 - "$TEMP_BUILDER" <<'PY'
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()

# makeblurroextras.sh is intended to sit next to the stock dev-kit makeplugin.sh.
# Refuse the old combined variant so the transient path cannot accidentally be run twice.
if 'TRANSIENT_3ON_TOOL=' in text or 'Wrote httpslib.3on (3NX& transient)' in text:
    raise SystemExit('ERROR: makeplugin.sh still contains Blurro transient-build customisations; use the stock dev-kit script.')

def replace_array(name, body):
    global text
    pattern = re.compile(rf'(?ms)^{re.escape(name)}=\(\n.*?^\)\n')
    replacement = name + '=(\n' + ''.join(f'    {line}\n' for line in body) + ')\n'
    text, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise SystemExit(f'ERROR: could not locate {name} in makeplugin.sh')

replace_array('ROSALINA_PLUGIN_CONFIG', [
    '"htps|httpslib|998|"',
    '"onln|onlineblurrotemporary|999|"',
])
replace_array('LOADER_PLUGIN_CONFIG', [])
replace_array('METADATA_CONFIG', [])
replace_array('STACKED_PLUGIN_CONFIG', [])

text, count = re.subn(r'(?m)^STATE_DIR="\.plgbuild"$', 'STATE_DIR=".plgbuild-blurroextras"', text, count=1)
if count != 1:
    raise SystemExit('ERROR: could not isolate makeplugin.sh build state for Blurro extras')

path.write_text(text)
PY

printf '\n'
printf '=========================================\n'
printf '======= Building Blurro extras! =========\n'
printf '=========================================\n\n'

bash "$TEMP_BUILDER"

HTTPS_NX="${SCRIPT_DIR}/httpslib.998.3nx"
ONLINE_NX="${SCRIPT_DIR}/onlineblurrotemporary.999.3nx"
if [[ ! -f "$HTTPS_NX" || ! -f "$ONLINE_NX" ]]; then
    printf 'ERROR: private plugin build did not produce both transient source .3nx files.\n' >&2
    exit 1
fi

python3 "$TRANSIENT_TOOL" "$HTTPS_NX" "$TEMP_HTTPS_OUT" htps
python3 "$TRANSIENT_TOOL" "$ONLINE_NX" "$TEMP_ONLINE_OUT" onln

python3 - "$TEMP_HTTPS_OUT" htps "$TEMP_ONLINE_OUT" onln <<'PY'
import pathlib
import struct
import sys

TRANSIENT_MAGIC = 0x26584E33
for i in range(1, len(sys.argv), 2):
    path = pathlib.Path(sys.argv[i])
    expected_id = sys.argv[i + 1].encode('ascii')
    data = path.read_bytes()
    if len(data) < 0x2C:
        raise SystemExit(f'ERROR: transient output is truncated: {path}')
    magic = struct.unpack_from('<I', data, 0)[0]
    plugin_id = data[4:8]
    if magic != TRANSIENT_MAGIC or plugin_id != expected_id:
        raise SystemExit(f'ERROR: transient output validation failed: {path}')
PY

mv -f -- "$TEMP_HTTPS_OUT" "${SCRIPT_DIR}/httpslib.3on"
mv -f -- "$TEMP_ONLINE_OUT" "${SCRIPT_DIR}/onlineblurrotemporary.3on"
rm -f -- "$HTTPS_NX" "$ONLINE_NX"

printf '\nWrote httpslib.3on (%s bytes, 3NX& transient)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/httpslib.3on")"
printf 'Wrote onlineblurrotemporary.3on (%s bytes, 3NX& transient)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/onlineblurrotemporary.3on")"
printf '\nDone. Normal plugin configuration remains in stock makeplugin.sh.\n\n'