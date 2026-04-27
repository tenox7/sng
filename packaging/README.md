# Linux universal packages

All three formats build via Docker so they work on macOS as well as Linux.
Output goes to `dist/`.

## Quick start

```
make appimage         # amd64 AppImage (broadest reach)
make flatpak          # host-arch .flatpak
make snap             # host-arch .snap
make linux-packages   # all three (host arch for flatpak/snap, amd64 for AppImage)
```

## Architecture targeting

```
make appimage-amd64   make appimage-arm64   make appimage-all
make flatpak-amd64    make flatpak-arm64    make flatpak-all
make snap-amd64       make snap-arm64       make snap-all
make linux-packages-all
```

Output filenames carry the arch:

| Format   | Filename                       |
|----------|--------------------------------|
| AppImage | `dist/sng-x86_64.AppImage` / `dist/sng-aarch64.AppImage` |
| Flatpak  | `dist/sng-x86_64.flatpak`  / `dist/sng-aarch64.flatpak`  |
| Snap     | `dist/sng_<ver>_amd64.snap` / `dist/sng_<ver>_arm64.snap` |

**On Apple Silicon Mac:**
- `appimage-amd64` works (Rosetta emulates the gcc/cmake build fine — no sandboxing involved).
- `appimage-arm64` works natively.
- `flatpak-arm64` works natively.
- `flatpak-amd64` **fails** — flatpak-builder's bwrap calls `prctl(PR_SET_SECCOMP)` which QEMU user-mode emulation returns EINVAL for. Need a real x86_64 Linux host.
- `snap-amd64` and `snap-arm64` both work — our manual snap build only uses gcc + mksquashfs, no bwrap.

**On x86_64 Linux:** all six explicit targets work; `flatpak-arm64` is QEMU-emulated and may also hit bwrap issues depending on kernel.

## AppImage

- Built from Ubuntu 22.04 (glibc 2.35).
- SDL3 + SDL3_ttf compiled from source (3.2.10 / 3.2.2).
- AppImage runtime is the `type2-runtime` static ELF; we `cat runtime + squashfs > sng.AppImage`.
  No appimagetool or linuxdeploy is invoked at build time — those are themselves
  AppImages and don't run reliably under amd64 emulation in Docker.
- Library bundling: a small `ldd`-driven loop in `build.sh` copies the
  transitive .so closure into the AppDir, skipping libc/libdl/libpthread/etc.
- Runs unsandboxed → rootless ICMP works whenever the host's
  `net.ipv4.ping_group_range` includes the user's gid.
- `shell=` directive is **enabled** (the binary executes natively on the host).
- `setcap cap_net_raw+ep` cannot be applied to the binary inside an AppImage —
  the bundle is mounted FUSE `nosuid`.

## Flatpak

- Manifest: `io.github.tenox7.sng.yaml`. Targets `org.freedesktop.Platform//24.08`.
  Bundles SDL3/SDL3_ttf as manifest modules (the 24.08 runtime ships SDL2 only;
  25.08 is the first runtime with SDL3 in the freedesktop SDK).
- Container runs with `--privileged --security-opt seccomp=unconfined` so
  flatpak-builder's bubblewrap sandboxes work.
- Cache volume `sng-flatpak-cache` keeps the freedesktop runtime/sdk between
  runs (~600 MB download; cached after first build).
- `--share=network` keeps the host network namespace, so rootless ICMP works
  via `net.ipv4.ping_group_range`.
- `--filesystem=xdg-config/sng:create` for the config file.
- `shell=` is compiled out (`NO_SHELL=1`) because shell commands the user
  configures (e.g. `ping`, `awk`) typically don't exist inside the freedesktop runtime.
- Install: `flatpak install --user dist/sng.flatpak`

## Snap

- Built as a raw squashfs with hand-written `meta/snap.yaml` (no snapcraft).
  Snapcraft itself is gnarly to install in Docker — the apt package is a
  transitional snap-installer, the PyPI package needs git history, and the
  snap requires snapd. The squashfs format is straightforward enough to
  produce directly with `mksquashfs`.
- Base `core22`, strict confinement.
- SDL3/SDL3_ttf built from source in the Docker image, then the resulting libs
  bundled via `ldd` into `usr/lib/`.
- Plugs declared: `network`, `network-bind`, `network-observe`, `desktop`,
  `wayland`, `x11`, `home`.
- `shell=` is compiled out (`NO_SHELL=1`).
- Local install:

  ```
  sudo snap install --dangerous dist/sng_*.snap
  sudo snap connect sng:network-observe   # required for ICMP
  ```

  `network-observe` is manual-connect — the default `network` plug does not
  permit `socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)`.
- This squashfs is installable locally but **not** Snap Store-ready. Store
  publication needs the real snapcraft toolchain (review tooling, signing,
  etc.) which we don't run here. For store submission, run snapcraft on a real
  Ubuntu host or a CI runner.

## Layout

```
packaging/
├── README.md
├── shared/sng.png            256x256 icon used by all three
├── appimage/
│   ├── Dockerfile            Ubuntu 22.04 + SDL3 + type2-runtime
│   ├── build.sh              Compiles, ldd-bundles libs, packs squashfs
│   ├── AppRun                LD_LIBRARY_PATH wrapper
│   └── sng.desktop
├── flatpak/
│   ├── Dockerfile            Ubuntu 24.04 + flatpak-builder + elfutils
│   ├── build.sh              Runs flatpak-builder + flatpak build-bundle
│   ├── io.github.tenox7.sng.yaml
│   ├── io.github.tenox7.sng.desktop
│   └── io.github.tenox7.sng.metainfo.xml
└── snap/
    ├── Dockerfile            Ubuntu 22.04 + SDL3 + squashfs-tools
    ├── build.sh              Compiles, ldd-bundles libs, writes meta/, packs squashfs
    ├── snapcraft.yaml        Reference snapcraft.yaml (not used by build.sh, kept for store submission)
    └── snap/gui/{icon.png,sng.desktop}
```

## Cleaning Docker artifacts

```
docker volume rm sng-flatpak-cache sng-snap-cache sng-snap-pkgs
docker rmi sng-appimage-builder sng-flatpak-builder sng-snap-builder
```

## GitHub Actions release builds

`.github/workflows/release.yml` builds all 6 packages (3 formats × 2 arches)
when a GitHub Release is published, and attaches them to the release.

Trigger: `release: published` (created via the GitHub UI or `gh release create`).
Manually triggerable via `workflow_dispatch` for testing — in that mode the
artifacts upload to the workflow run page instead of a release.

The amd64 jobs run on `ubuntu-22.04`; arm64 jobs run on the free
`ubuntu-24.04-arm` runners. Both are native — no QEMU emulation, so
`flatpak-amd64` works in CI even though it can't on Apple Silicon locally.

Snap version is read from `version.h` (the `SNG_VERSION` macro) — bump that
before tagging so the snap filename matches the release.
