#!/bin/sh
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist"
APPDIR="$REPO_ROOT/packaging/appimage/SNG.AppDir"
RUNTIME="${APPIMAGE_RUNTIME:-/opt/appimage-runtime}"
ARCH="$(uname -m)"

mkdir -p "$OUT_DIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cd "$REPO_ROOT"
make -f Makefile.linux clean
make -f Makefile.linux

install -m 755 sng "$APPDIR/usr/bin/sng"
install -m 644 packaging/shared/sng.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/sng.png"
install -m 644 packaging/shared/sng.png "$APPDIR/sng.png"
install -m 644 packaging/appimage/sng.desktop "$APPDIR/usr/share/applications/sng.desktop"
install -m 644 packaging/appimage/sng.desktop "$APPDIR/sng.desktop"
install -m 755 packaging/appimage/AppRun "$APPDIR/AppRun"

bundle_libs_for() {
    local target="$1"
    ldd "$target" 2>/dev/null | awk '/=>/ {print $3}' | while read -r lib; do
        [ -z "$lib" ] && continue
        [ "$lib" = "linux-vdso.so.1" ] && continue
        case "$(basename "$lib")" in
            libc.so.*|ld-linux-*|libdl.so.*|libpthread.so.*|libm.so.*|librt.so.*|libresolv.so.*) continue ;;
        esac
        local dest="$APPDIR/usr/lib/$(basename "$lib")"
        [ -f "$dest" ] && continue
        cp -L "$lib" "$dest"
        echo "    + $(basename "$lib")"
    done
}

echo ">>> bundling shared libraries"
bundle_libs_for "$APPDIR/usr/bin/sng"

echo ">>> recursively bundling deps"
prev_count=0
while :; do
    count=$(find "$APPDIR/usr/lib" -name '*.so*' | wc -l)
    [ "$count" = "$prev_count" ] && break
    prev_count=$count
    for sofile in "$APPDIR"/usr/lib/*.so*; do
        [ -f "$sofile" ] || continue
        bundle_libs_for "$sofile"
    done
done

echo ">>> packing squashfs"
SQUASHFS="$REPO_ROOT/packaging/appimage/sng.squashfs"
rm -f "$SQUASHFS"
mksquashfs "$APPDIR" "$SQUASHFS" -root-owned -noappend -comp xz -no-progress -quiet

OUT="$OUT_DIR/sng-$ARCH.AppImage"
cat "$RUNTIME" "$SQUASHFS" > "$OUT"
chmod +x "$OUT"
rm -f "$SQUASHFS"

ls -la "$OUT"
file "$OUT"
