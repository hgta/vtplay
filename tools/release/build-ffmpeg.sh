#!/bin/bash
#
# 为 win81 构建线自建 FFmpeg —— 产出**两个**东西：
#
#   $OUT/ffmpeg.exe                     静态单文件，给导出/诊断的子进程用（GPL，含 x264/x265）
#   $OUT/ffmpeg-shared/bin/*.dll        共享库，给应用自己链接（LGPL，只有 dav1d）
#   $OUT/**/ffmpeg.BUILD.txt            两份来源记录（版本 / configure 行 / sha256）
#
# 用法（在 MSYS2 的 bash 里跑，或由 build-ffmpeg.ps1 调起）：
#     bash tools/release/build-ffmpeg.sh
#     FORCE=1 bash tools/release/build-ffmpeg.sh     # 忽略已有产物，强制重建
#
# 为什么不用 MSYS2 现成的 mingw-w64-x86_64-ffmpeg（实测，不是推测）：
#   它的 configuration 挂了约 50 个外部库，其中两项在 Win8.1 上不存在，且都是**函数级**缺失：
#       libSvtAv1Enc-4.dll  硬导入 KERNEL32!SetThreadDescription  （Win10 1607 才有）
#       librsvg-2-2.dll     硬导入 bcryptprimitives!ProcessPrng     （Win10 才有）
#       （还有 libdovi.dll，经由 libplacebo，同 ProcessPrng）
#   这三项被 `check-target-imports.ps1` 抓出来，且**不是 Qt 的**——Qt5 那条线本身是干净的。
#   补文件救不了：缺的是函数，不是文件。所以只能不把它们编进去。
#
# 两个产物为什么要分开：
#   * ffmpeg.exe 是**子进程**（导出/诊断），要编码器 → x264/x265 → 只能 GPL；
#   * 应用本体只**解码**，不需要编码器 → 共享库可以不带 x264/x265 → LGPL。
#   * 更关键的是：应用链接的 avcodec 若仍是 MSYS2 那套，它会在加载时连带拉起
#     librsvg/svt-av1，**应用启动就挂**。所以共享库也必须自建。
#
# 工程约束（踩过的坑，别改回去）：
#   * configure 不能在「已有 config.h 的源码树」里做第二遍（out-of-tree 也不行），
#     所以两个产物各自解一份源码副本；
#   * vidstab 用 OpenMP，但它的 vidstab.pc 没写 -lgomp，静态链接的探针会因为 omp_* 未定义
#     而失败（报 "vidstab >= 0.98 not found"，与版本无关）→ 静态那份要补 --extra-libs=-lgomp；
#   * 共享库那份不要 --disable-postproc：FFmpeg 9 已经没有这个开关（postproc 默认不开）。
set -euo pipefail

export PATH=/mingw64/bin:/usr/bin:$PATH

VERSION=${FFMPEG_VERSION:-9.0.2}
CACHE=${CACHE_DIR:-/d/dev/cache}
OUT=${OUT_DIR:-$(cd "$(dirname "$0")/../.." && pwd)/third_party/win81}
JOBS=${JOBS:-$(nproc)}
FORCE=${FORCE:-0}

TARBALL="ffmpeg-$VERSION.tar.xz"
say() { printf '\n[ffmpeg] %s\n' "$*"; }

# ---- 0. 工具链前置：缺什么直接说清楚，别等 make 半路报错 ----
for t in gcc g++ nasm pkg-config; do
    command -v "$t" >/dev/null || {
        echo "缺少 $t（pacman -S mingw-w64-x86_64-nasm mingw-w64-x86_64-pkg-config 等）" >&2
        exit 1
    }
done
command -v mingw32-make >/dev/null || { echo '缺少 mingw32-make（mingw-w64-x86_64-make）' >&2; exit 1; }
# 静态库：动态导入库（.dll.a）不能满足 -static，缺了就是链接期 undefined。
# 包名逐个写出来——vidstab 的包名带点（mingw-w64-x86_64-vid.stab），写错会让 CI 卡在装依赖那步。
for lib in libx264.a libx265.a libdav1d.a libvidstab.a; do
    [ -f "/mingw64/lib/$lib" ] || {
        echo "缺少 /mingw64/lib/$lib —— 装对应的开发包：" >&2
        echo '  pacman -S mingw-w64-x86_64-libx264 mingw-w64-x86_64-x265 mingw-w64-x86_64-dav1d mingw-w64-x86_64-vid.stab' >&2
        exit 1
    }
done

mkdir -p "$CACHE" "$OUT"

# ---- 1. 源码 tarball（两个产物共用一份压缩包） ----
cd "$CACHE"
if [ ! -f "$TARBALL" ]; then
    say "下载 $TARBALL"
    curl -fL --retry 3 -o "$TARBALL" "https://ffmpeg.org/releases/$TARBALL"
fi

# 每个产物一份干净源码副本；已解过就复用（重复删 120MB 源码树既慢又没必要）
extract_src() {
    local dst=$1
    if [ -f "$dst/configure" ]; then return 0; fi
    say "解包到 $dst"
    mkdir -p "$dst"
    tar xf "$CACHE/$TARBALL" -C "$dst" --strip-components=1
}

# ---- 2. 静态 ffmpeg.exe（导出/诊断的子进程） ----
build_static() {
    local exe="$OUT/ffmpeg.exe"
    if [ -f "$exe" ] && [ "$FORCE" != "1" ]; then
        say "已存在 $exe —— 跳过（FORCE=1 强制重建）"
        return 0
    fi
    local src="$CACHE/ffmpeg-$VERSION-static-src"
    extract_src "$src"
    cd "$src"
    [ -f config.h ] && { echo "$src 里已有 config.h（源码树被 configure 过），请换目录或删掉它" >&2; exit 1; }

    say "configure（静态，$VERSION）"
    ./configure \
        --prefix="$CACHE/ffmpeg-$VERSION-install" \
        --arch=x86_64 --target-os=mingw32 --cc=gcc --cxx=g++ \
        --disable-autodetect --enable-gpl \
        --enable-libx264 --enable-libx265 --enable-libdav1d --enable-libvidstab \
        --enable-static --disable-shared --pkg-config-flags=--static \
        --extra-ldflags=-static --extra-libs=-lgomp \
        --disable-doc --disable-debug --disable-ffplay \
        > "$CACHE/ffmpeg-configure.log" 2>&1 || {
            echo 'configure 失败，末尾日志：' >&2
            tail -25 "$CACHE/ffmpeg-configure.log" >&2
            exit 1
        }

    say "编译（-j$JOBS）"
    mingw32-make -j"$JOBS" > "$CACHE/ffmpeg-build.log" 2>&1 || {
        echo '编译失败，末尾日志：' >&2
        tail -30 "$CACHE/ffmpeg-build.log" >&2
        exit 1
    }

    # 验收：不只是「编出来了」，而是「编出来的东西真能干活」
    local built="$src/ffmpeg.exe"
    [ -f "$built" ] || { echo "没有产出 $built" >&2; exit 1; }
    say "验收"
    "$built" -hide_banner -version | head -1
    local cfg
    cfg=$("$built" -hide_banner -version | grep -m1 '^configuration:')
    for bad in libsvtav1 librsvg libplacebo; do
        case "$cfg" in
            *"--enable-$bad"*) echo "错误：构建里含 $bad（Win8.1 上会加载失败）" >&2; exit 1 ;;
        esac
    done
    # 用文件而不是 grep -q 接管道：grep -q 提前退出会给 ffmpeg 送 SIGPIPE，
    # 配上 pipefail 会把「成功」判成「失败」（随机复现，第一次跑就被坑到）
    local enc
    enc=$("$built" -hide_banner -encoders 2>/dev/null)
    local flt dec
    flt=$("$built" -hide_banner -filters 2>/dev/null)
    dec=$("$built" -hide_banner -decoders 2>/dev/null)
    grep -q 'libx264' <<<"$enc"        || { echo '错误：缺 libx264 编码器' >&2; exit 1; }
    grep -q 'libx265' <<<"$enc"        || { echo '错误：缺 libx265 编码器' >&2; exit 1; }
    grep -q 'vidstabdetect' <<<"$flt"  || { echo '错误：缺 vidstabdetect 滤镜（修复导出要用）' >&2; exit 1; }
    grep -q 'libdav1d' <<<"$dec"       || { echo '错误：缺 libdav1d 解码器' >&2; exit 1; }

    say "导入表（应只有系统 DLL + GCC 运行时）"
    objdump -p "$built" | grep -E 'DLL Name:' | sed 's/.*DLL Name: /  /'

    cp -f "$built" "$exe"
    {
        echo "artifact: ffmpeg.exe（静态，导出/诊断子进程用）"
        echo "source:   https://ffmpeg.org/releases/$TARBALL"
        echo "version:  $("$built" -hide_banner -version | head -1)"
        echo "built:    $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        echo "gcc:      $(gcc --version | head -1)"
        echo "sha256:   $(sha256sum "$built" | cut -d' ' -f1)"
        echo "license:  GPL-2.0-or-later（含 x264/x265）"
        echo "config:   $cfg"
    } > "$OUT/ffmpeg.BUILD.txt"
    say "产物：$exe ($(du -h "$exe" | cut -f1))"
}

# ---- 3. 共享库（应用链接用，只解码） ----
build_shared() {
    local dst="$OUT/ffmpeg-shared"
    if [ -f "$dst/bin/avcodec-63.dll" ] && [ "$FORCE" != "1" ]; then
        say "已存在 $dst/bin/*.dll —— 跳过（FORCE=1 强制重建）"
        return 0
    fi
    local src="$CACHE/ffmpeg-$VERSION-shared-src"
    extract_src "$src"
    cd "$src"
    [ -f config.h ] && { echo "$src 里已有 config.h（源码树被 configure 过），请换目录或删掉它" >&2; exit 1; }

    say "configure（共享库，应用链接用）"
    ./configure \
        --prefix="$dst" \
        --arch=x86_64 --target-os=mingw32 --cc=gcc --cxx=g++ \
        --disable-autodetect --enable-libdav1d \
        --pkg-config-flags=--static \
        --enable-shared --disable-static \
        --disable-programs --disable-avdevice --disable-avfilter \
        --disable-doc --disable-debug \
        > "$CACHE/ffmpeg-shared-configure.log" 2>&1 || {
            echo '共享库 configure 失败：' >&2
            tail -25 "$CACHE/ffmpeg-shared-configure.log" >&2
            exit 1
        }

    say "编译共享库（-j$JOBS）"
    mingw32-make -j"$JOBS" > "$CACHE/ffmpeg-shared-build.log" 2>&1 || {
        echo '共享库编译失败：' >&2
        tail -30 "$CACHE/ffmpeg-shared-build.log" >&2
        exit 1
    }
    mingw32-make install >> "$CACHE/ffmpeg-shared-build.log" 2>&1

    say "共享库导入表"
    local f
    for f in "$dst/bin"/*.dll; do
        printf '  %s ->' "$(basename "$f")"
        objdump -p "$f" | grep -E 'DLL Name:' | sed 's/.*DLL Name: / /' | tr '\n' ' '
        echo
    done
    {
        echo "artifact: avcodec/avformat/avutil/swscale/swresample（共享，应用链接用）"
        echo "source:   https://ffmpeg.org/releases/$TARBALL"
        echo "built:    $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        echo "gcc:      $(gcc --version | head -1)"
        echo "license:  LGPL-2.1-or-later（--disable-autodetect，无 GPL 组件、无编码器）"
        echo "config:   $(grep -m1 '^  configuration' "$CACHE/ffmpeg-shared-configure.log" | sed 's/^  configuration: //')"
    } > "$dst/ffmpeg.BUILD.txt"
    say "产物：$dst/bin/*.dll"
}

build_static
build_shared

say '完成。下一步：用 tools\release\build-app.ps1 重建应用（会自动把 PKG_CONFIG_PATH 指过来），再打包'
