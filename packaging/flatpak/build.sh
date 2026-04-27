#!/bin/sh
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/dist"
BUILD_DIR="$REPO_ROOT/packaging/flatpak/build"
REPO_DIR="$REPO_ROOT/packaging/flatpak/repo"
APP_ID="io.github.tenox7.sng"

mkdir -p "$OUT_DIR"
rm -rf "$BUILD_DIR" "$REPO_DIR"

flatpak remote-add --if-not-exists --user flathub https://flathub.org/repo/flathub.flatpakrepo

flatpak install -y --user --noninteractive flathub \
    org.freedesktop.Platform//24.08 \
    org.freedesktop.Sdk//24.08

cd "$REPO_ROOT/packaging/flatpak"

flatpak-builder --user --force-clean --disable-rofiles-fuse --repo="$REPO_DIR" "$BUILD_DIR" "$APP_ID.yaml"

ARCH="$(flatpak --default-arch)"
OUT="$OUT_DIR/sng-${ARCH}.flatpak"
flatpak build-bundle --arch="$ARCH" "$REPO_DIR" "$OUT" "$APP_ID"

ls -la "$OUT"
