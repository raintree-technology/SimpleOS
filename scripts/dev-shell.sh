#!/bin/bash

set -euo pipefail

IMAGE_NAME="${IMAGE_NAME:-simpleos-dev}"
WORKDIR="${WORKDIR:-/workspace}"

docker build -t "$IMAGE_NAME" -f .devcontainer/Dockerfile .

docker run --rm -it \
  --cap-drop=ALL \
  --security-opt=no-new-privileges:true \
  -p 3500:3500 \
  -v "$(pwd):$WORKDIR" \
  -w "$WORKDIR" \
  "$IMAGE_NAME" \
  bash
