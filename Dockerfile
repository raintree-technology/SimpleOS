# Dockerfile for building SimpleOS
FROM randomdude/gcc-cross-x86_64-elf:latest

RUN apt-get update && apt-get install -y \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
CMD ["make"]
