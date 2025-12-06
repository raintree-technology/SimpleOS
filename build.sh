#!/bin/bash
# Build SimpleOS using Docker (no local toolchain needed)

set -e

echo "Building SimpleOS with Docker..."

docker build -t simpleos-builder .
docker run --rm -v "$(pwd)":/src simpleos-builder make clean
docker run --rm -v "$(pwd)":/src simpleos-builder make

echo ""
if [ -f simpleos.iso ]; then
    echo "✓ Build successful! ISO created: simpleos.iso"

    # Copy to web app if it exists
    if [ -d "web/public/os" ]; then
        cp simpleos.iso web/public/os/
        echo "✓ Copied to web/public/os/"
        echo ""
        echo "Run the web app:"
        echo "  cd web && npm run dev"
    fi
else
    echo "✗ Build failed - no ISO created"
    exit 1
fi
