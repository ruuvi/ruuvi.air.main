#!/usr/bin/env bash
#
# Build Docker image.
#

set -xeuo pipefail

NCS_VERSION="v2.9.2"
IMAGE="ruuvi-air-ci:${NCS_VERSION}"

# Create a fingerprint of the Dockerfile and build context
HASH=$(find Dockerfile . -type f -maxdepth 1 -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-12)
echo "Dockerfile hash: $HASH"

# Check existing image label
EXISTING_HASH=$(docker image inspect "$IMAGE" --format '{{ index .Config.Labels "ruuvi.hash" }}' 2>/dev/null || true)
if [ "$HASH" != "$EXISTING_HASH" ]; then
    echo "Building new image because Dockerfile/context changed or image missing..."
    docker build --label "ruuvi.hash=${HASH}" -t "$IMAGE" .
else
    echo "Cached image is up to date."
fi

