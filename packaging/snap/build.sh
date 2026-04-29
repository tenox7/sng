#!/bin/sh
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist"
ROOTFS="$REPO_ROOT/packaging/snap/sng-snap"
VERSION="${VERSION:-$(awk -F'"' '/SNG_VERSION/ {print $2}' "$REPO_ROOT/version.h")}"
ARCH="$(dpkg --print-architecture)"

mkdir -p "$OUT_DIR"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS/bin" "$ROOTFS/usr/lib" "$ROOTFS/meta/gui"

cd "$REPO_ROOT"
make -f Makefile.linux clean
make -f Makefile.linux NO_SHELL=1
install -m 755 sng "$ROOTFS/bin/sng"

cp packaging/snap/snap/gui/icon.png "$ROOTFS/meta/gui/icon.png"
cp packaging/snap/snap/gui/sng.desktop "$ROOTFS/meta/gui/sng.desktop"

bundle_libs_for() {
    local target="$1"
    ldd "$target" 2>/dev/null | awk '/=>/ {print $3}' | while read -r lib; do
        [ -z "$lib" ] && continue
        [ "$lib" = "linux-vdso.so.1" ] && continue
        case "$(basename "$lib")" in
            libc.so.*|ld-linux-*|libdl.so.*|libpthread.so.*|libm.so.*|librt.so.*|libresolv.so.*) continue ;;
        esac
        local dest="$ROOTFS/usr/lib/$(basename "$lib")"
        [ -f "$dest" ] && continue
        cp -L "$lib" "$dest"
        echo "    + $(basename "$lib")"
    done
}

echo ">>> bundling shared libraries"
bundle_libs_for "$ROOTFS/bin/sng"

prev_count=0
while :; do
    count=$(find "$ROOTFS/usr/lib" -name '*.so*' | wc -l)
    [ "$count" = "$prev_count" ] && break
    prev_count=$count
    for sofile in "$ROOTFS"/usr/lib/*.so*; do
        [ -f "$sofile" ] || continue
        bundle_libs_for "$sofile"
    done
done

cat > "$ROOTFS/meta/snap.yaml" <<EOF
name: sng
version: '$VERSION'
summary: System and network grapher
description: |
  Plot ICMP ping latency, SNMP and local interface throughput,
  CPU, memory, and load average.

  ICMP ping requires the network-observe interface. After install run:
      sudo snap connect sng:network-observe

  The shell= config directive is disabled in this build because of
  strict-confinement sandboxing.

architectures:
  - $ARCH
base: core22
grade: stable
confinement: strict
license: MIT
apps:
  sng:
    command: snap-wrapper
    plugs:
      - network
      - network-bind
      - network-observe
      - desktop
      - wayland
      - x11
      - home
EOF

cat > "$ROOTFS/snap-wrapper" <<'EOF'
#!/bin/sh
export LD_LIBRARY_PATH="$SNAP/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$SNAP/bin/sng" "$@"
EOF
chmod +x "$ROOTFS/snap-wrapper"

OUT="$OUT_DIR/sng_${VERSION}_${ARCH}.snap"
rm -f "$OUT"
mksquashfs "$ROOTFS" "$OUT" -noappend -comp xz -all-root -no-xattrs -no-fragments -no-progress -quiet

ls -la "$OUT"
file "$OUT"
