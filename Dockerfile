# Dockerfile for building SimpleOS
# Base image name is historical; this container is used for the current i386 build.
FROM randomdude/gcc-cross-x86_64-elf:latest

RUN apt-get update && apt-get install -y \
    grub-pc-bin \
    grub-common \
    xorriso \
    xxd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
CMD ["make"]
