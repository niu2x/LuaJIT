#!/bin/bash
# macOS: ./build_android.sh [all|arm64-v8a|armeabi-v7a] [--install /path/to/animal-core]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")" && pwd)
if [[ "${LUAJIT_REQUIRE_ALLOC2:-false}" == true ]]; then
    if ! grep -q 'define LUA_GCDIFF' "$ROOT/src/lua.h" || ! grep -q 'luaL_gcdebugoutput' "$ROOT/src/lauxlib.h"; then
        echo "Local Android Lua debugging requires debug/alloc2 with the gcdebugoutput adapter." >&2
        exit 1
    fi
fi
ABI=${1:-all}
if [[ $# -gt 0 ]]; then shift; fi
INSTALL_ROOT=
if [[ $# -gt 0 ]]; then
    [[ $# == 2 && "$1" == --install ]] || { echo "Usage: $0 [all|arm64-v8a|armeabi-v7a] [--install /path/to/animal-core]" >&2; exit 1; }
    INSTALL_ROOT=$(cd "$2" && pwd)
    [[ -d "$INSTALL_ROOT/engine/he.core/lua_support/luajit/android" ]] || { echo "Not an animal-core checkout" >&2; exit 1; }
fi
case "$ABI" in all|arm64-v8a|armeabi-v7a) ;; *) echo "Unknown ABI: $ABI" >&2; exit 1 ;; esac
[[ $(uname -s) == Darwin ]] || { echo "This script targets macOS; use the Linux scripts on Linux." >&2; exit 1; }
NDK=${ANDROID_NDK_HOME:-/opt/android-sdk/ndk/21.4.7075529}
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
[[ -x "$TOOLCHAIN/bin/aarch64-linux-android21-clang" ]] || { echo "NDK r21 toolchain not found: $TOOLCHAIN" >&2; exit 1; }
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
OUT="$ROOT/build/android"
mkdir -p "$OUT"
WORK=$(mktemp -d /private/tmp/luajit-android.XXXXXX)
CONTAINER=
cleanup() {
    if [[ -n "$CONTAINER" ]]; then docker rm -f "$CONTAINER" >/dev/null 2>&1 || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT
if [[ "$ABI" == all || "$ABI" == armeabi-v7a ]]; then
    docker info >/dev/null
    HOST_IMAGE=${LUAJIT_HOST32_IMAGE:-luajit-android-host32:local}
    docker build --platform linux/amd64 -t "$HOST_IMAGE" - < "$ROOT/Dockerfile.android-host32"
    CONTAINER=$(docker run -d --rm --platform linux/amd64 --network none -v "$WORK:$WORK" "$HOST_IMAGE" sleep infinity)
fi
build_one() {
    local abi=$1 triple api source hostcc hostlua buildvm
    case "$abi" in
        arm64-v8a) triple=aarch64-linux-android; api=${ANDROID_API64:-21} ;;
        armeabi-v7a) triple=armv7a-linux-androideabi; api=${ANDROID_API32:-16} ;;
    esac
    source="$WORK/$abi"
    mkdir -p "$source"
    # Include local edits, but exclude Git metadata and previous build outputs.
    tar -C "$ROOT" --exclude=./.git --exclude=./build --exclude='./install*' -cf - . | tar -C "$source" -xf -
    hostcc=clang
    hostlua=
    buildvm=host/buildvm
    if [[ "$abi" == armeabi-v7a ]]; then
        clang -O2 "$source/src/host/minilua.c" -lm -o "$WORK/minilua"
        hostlua="$WORK/minilua"
        cat > "$WORK/host-cc" <<WRAPPER
#!/bin/bash
exec docker exec -w "\$PWD" "$CONTAINER" gcc -m32 "\$@"
WRAPPER
        cat > "$WORK/buildvm" <<WRAPPER
#!/bin/bash
exec docker exec -w "\$PWD" "$CONTAINER" qemu-i386 ./host/buildvm "\$@"
WRAPPER
        chmod +x "$WORK/host-cc" "$WORK/buildvm"
        hostcc="$WORK/host-cc"
        buildvm="$WORK/buildvm"
    fi
    local args=(HOST_SYS=Darwin TARGET_SYS=Linux BUILDMODE=static
        "HOST_CC=$hostcc" "BUILDVM_X=$buildvm"
        "STATIC_CC=$TOOLCHAIN/bin/$triple$api-clang"
        "DYNAMIC_CC=$TOOLCHAIN/bin/$triple$api-clang -fPIC"
        "TARGET_LD=$TOOLCHAIN/bin/$triple$api-clang"
        "TARGET_AR=$TOOLCHAIN/bin/llvm-ar rcus"
        "TARGET_STRIP=$TOOLCHAIN/bin/llvm-strip")
    if [[ -n "$hostlua" ]]; then args+=("HOST_LUA=$hostlua"); fi
    make -C "$source" "${args[@]}" clean
    make -C "$source" -j"$JOBS" "${args[@]}"
    mkdir -p "$OUT/$abi/include"
    cp "$source/src/libluajit.a" "$OUT/$abi/libluajit.a"
    cp "$source"/src/*.h "$source/src/lua.hpp" "$OUT/$abi/include/"
    "$TOOLCHAIN/bin/llvm-readelf" -h "$source/src/lj_state.o" | sed -n '/Class:/p; /Machine:/p'
    echo "Built: $OUT/$abi/libluajit.a"
    if [[ -n "$INSTALL_ROOT" ]]; then
        cp "$OUT/$abi/libluajit.a" "$INSTALL_ROOT/engine/he.core/lua_support/luajit/android/$abi/libluajit.a"
    fi
}
if [[ "$ABI" == all ]]; then
    build_one arm64-v8a
    build_one armeabi-v7a
    HEADER_ABI=arm64-v8a
else
    build_one "$ABI"
    HEADER_ABI=$ABI
fi
if [[ -n "$INSTALL_ROOT" ]]; then
    # Engine GC statistics use internal definitions from this directory.
    cp "$OUT/$HEADER_ABI/include/"*.h "$INSTALL_ROOT/engine/cocos2dx/cocos2dx/base/luajit_header/"
    # The other include directories only need the public embedding API.
    for directory in engine/he.core/lua_support/luajit/Headers engine/cocos2dx/scripting/lua/lua engine/he.core/lua_support/luajit/android/include; do
        for header in lua.h lauxlib.h lualib.h luaconf.h; do
            cp "$OUT/$HEADER_ABI/include/$header" "$INSTALL_ROOT/$directory/"
        done
        if [[ "$directory" != engine/cocos2dx/scripting/lua/lua ]]; then
            cp "$OUT/$HEADER_ABI/include/luajit.h" "$OUT/$HEADER_ABI/include/lua.hpp" "$INSTALL_ROOT/$directory/"
        fi
    done
    echo "Installed into: $INSTALL_ROOT"
fi
