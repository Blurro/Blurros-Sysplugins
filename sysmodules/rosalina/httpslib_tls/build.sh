#!/bin/sh
set -eu

OUT="$1"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK="$(dirname -- "$OUT")/.httpslib_tls"
MB="$ROOT/mbedtls"
PATCH="$ROOT/patched"
CONFIG="$ROOT/config"
CC=${CC:-arm-none-eabi-gcc}
LD=${LD_RAW:-arm-none-eabi-ld}
OBJCOPY=${OBJCOPY_RAW:-arm-none-eabi-objcopy}
NM=${NM_RAW:-arm-none-eabi-nm}

rm -rf "$WORK"
mkdir -p "$WORK"

CFLAGS='-Os -std=gnu11 -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-math-errno -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-builtin -D__3DS__ -DBLUR_TLS_NO_CERT_VERIFY -DBLUR_TLS_MIN_CERT -DMBEDTLS_CONFIG_FILE="blur_tls_config.h"'
INCLUDES="-I$CONFIG -I$MB/include -I$MB/library -include $CONFIG/blur_tls_compat.h"

for src in "$MB"/library/*.c; do
    name=$(basename "$src")
    [ "$name" = pk_wrap.c ] && continue
    case "$name" in
        pkparse.c|rsa.c|ssl_tls.c|ssl_cli.c|cipher_wrap.c) src="$PATCH/$name" ;;
    esac
    obj="$WORK/${name%.c}.o"
    $CC $CFLAGS $INCLUDES -c "$src" -o "$obj"
done

$CC $CFLAGS $INCLUDES -c "$PATCH/pk_wrap_client.c" -o "$WORK/pk_wrap_client.o"
$CC $CFLAGS $INCLUDES -c "$ROOT/blur_tls_client.c" -o "$WORK/blur_tls_client.o"

LIBGCC=$($CC -march=armv6k -mfloat-abi=hard -print-libgcc-file-name)
$LD -r --gc-sections -T "$ROOT/bundle.ld" \
    -u PLUGIN_htps_TlsConnect \
    -u PLUGIN_htps_TlsWrite \
    -u PLUGIN_htps_TlsRead \
    -u PLUGIN_htps_TlsClose \
    "$WORK"/*.o "$LIBGCC" -o "$WORK/bundle.o"

$NM -g --defined-only "$WORK/bundle.o" | awk '{print $3}' | \
    grep -v '^PLUGIN_htps_TlsConnect$' | \
    grep -v '^PLUGIN_htps_TlsWrite$' | \
    grep -v '^PLUGIN_htps_TlsRead$' | \
    grep -v '^PLUGIN_htps_TlsClose$' > "$WORK/localize.txt"

$OBJCOPY \
    --redefine-sym memcpy=PLUGIN_htps_TlsMemcpy \
    --redefine-sym memset=PLUGIN_htps_TlsMemset \
    --strip-symbol=snprintf \
    --localize-symbols="$WORK/localize.txt" \
    "$WORK/bundle.o" "$OUT"