FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential pkg-config ca-certificates curl file wget \
        cmake git \
        libfontconfig1-dev libfreetype6-dev libharfbuzz-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
        libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev \
        libdecor-0-dev libdbus-1-dev libpulse-dev libasound2-dev \
        libgl1-mesa-dev libegl1-mesa-dev \
        squashfs-tools desktop-file-utils patchelf zsync \
    && rm -rf /var/lib/apt/lists/*

ARG SDL_VER=3.2.10
ARG SDL_TTF_VER=3.2.2
RUN cd /tmp && \
    curl -L -o sdl.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL3-${SDL_VER}.tar.gz && \
    tar xf sdl.tar.gz && cd SDL3-${SDL_VER} && \
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DSDL_STATIC=OFF && \
    cmake --build build -j$(nproc) && cmake --install build && \
    cd /tmp && rm -rf SDL3-${SDL_VER} sdl.tar.gz && \
    curl -L -o sdl_ttf.tar.gz https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL_TTF_VER}/SDL3_ttf-${SDL_TTF_VER}.tar.gz && \
    tar xf sdl_ttf.tar.gz && cd SDL3_ttf-${SDL_TTF_VER} && \
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DSDLTTF_VENDORED=OFF && \
    cmake --build build -j$(nproc) && cmake --install build && \
    cd /tmp && rm -rf SDL3_ttf-${SDL_TTF_VER} sdl_ttf.tar.gz

ARG TARGETARCH
RUN case "$TARGETARCH" in \
        amd64) UARCH=x86_64  ;; \
        arm64) UARCH=aarch64 ;; \
        *) echo "unsupported TARGETARCH=$TARGETARCH" >&2; exit 1 ;; \
    esac && \
    curl -L -o /opt/appimage-runtime \
        "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-${UARCH}" && \
    chmod +x /opt/appimage-runtime

WORKDIR /work
