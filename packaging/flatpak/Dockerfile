FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        flatpak flatpak-builder \
        ca-certificates curl xz-utils git \
        fuse3 elfutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
