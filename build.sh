#!/bin/bash
# Build SimpleOS with Docker when a local cross-toolchain is unavailable.

set -euo pipefail

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is not installed or not on PATH."
    echo "Install Docker, or build locally with the required i686 ELF cross-toolchain."
    exit 1
fi

echo "Building SimpleOS with Docker..."

docker build -t simpleos-builder .
docker run --rm -v "$(pwd)":/src simpleos-builder \
    sh -c "make CROSS_PREFIX=x86_64-elf- GRUB_MKRESCUE=grub-mkrescue clean && cd userspace && make distclean"
docker run --rm -v "$(pwd)":/src simpleos-builder \
    sh -c "cd userspace && make CROSS_PREFIX=x86_64-elf- headers && cd /src && make CROSS_PREFIX=x86_64-elf- GRUB_MKRESCUE=grub-mkrescue"

echo ""
if [ -f simpleos.iso ]; then
    echo "Build successful. ISO created: simpleos.iso"

    # Copy to web app demo path
    if [ -d "web/public" ]; then
        mkdir -p web/public/os
        cp simpleos.iso web/public/os/simpleos.iso
        echo "Copied ISO to web/public/os/simpleos.iso"
        echo ""
        echo "Run the web app:"
        echo "  cd web && npm run dev"
    fi
else
    echo "Build failed: simpleos.iso was not created."
    exit 1
fi
