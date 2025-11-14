#!/usr/bin/env bash
#
# Build RuuviAir firmware using the Docker image published on GHCR by CI.
#
# Usage:
#   ./run_docker_fw_build.sh [script_path]
#
# Example:
#   ./run_docker_fw_build.sh  # uses default build_ruuviair_a3_release-dev.sh
#   ./run_docker_fw_build.sh build_ruuviair_a3_release-prod.sh

set -xeuo pipefail

# --- Script argument: build script path ---
SCRIPT_PATH="${1:-build_ruuviair_a3_release-dev.sh}"

# --- Ensure build script exists ---
if [[ ! -f "${SCRIPT_PATH}" ]]; then
  echo "Build script not found: ${SCRIPT_PATH}" >&2
  exit 1
fi

TAG="v2.8.0"
IMAGE="ruuvi-air-ci:${TAG}"

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

PROJECT_DIR=$(basename "$PWD")
PROJECT_NAME="$(basename "$PWD")"
NCS_VERSION="v2.8.0"
KEYS_DIR="$HOME/.signing_keys"
NCS_DOCKER_DIR="$HOME/ncs_docker"

# --- Derive build directory from script name ---
SCRIPT_BASENAME="$(basename "${SCRIPT_PATH}")"
BUILD_DIR_NAME="${SCRIPT_BASENAME%.sh}-docker"

# --- Print info ---
echo ">>> Building firmware in Docker"
echo "    Image          : ${IMAGE}"
echo "    NCS version    : ${NCS_VERSION}"
echo "    Signing keys   : ${KEYS_DIR}"
echo "    NCS Docker dir : ${NCS_DOCKER_DIR}"
echo "    Build script   : ${SCRIPT_PATH}"
echo "    Build dir      : ${BUILD_DIR_NAME}"
echo

mkdir -p "$NCS_DOCKER_DIR"
mkdir -p "$NCS_DOCKER_DIR/bin"
mkdir -p "$NCS_DOCKER_DIR/ncs"
mkdir -p "$NCS_DOCKER_DIR/ncs/${NCS_VERSION}"
mkdir -p "$NCS_DOCKER_DIR/ncs/toolchains"
mkdir -p "$NCS_DOCKER_DIR/.nrfutil"

docker run --rm -it \
  --user "$(id -u)":"$(id -g)" \
  -e HOME=$HOME \
  -e USER="$(id -un)" \
  -e HOST_UID="$(id -u)" \
  -e HOST_GID="$(id -g)" \
  -e HOST_USER="$(id -un)" \
  -e HOST_GROUP="$(id -gn)" \
  -v "$NCS_DOCKER_DIR:$HOME" \
  -v "$(pwd):$HOME/ncs/${NCS_VERSION}/${PROJECT_DIR}" \
  -v "$KEYS_DIR:$HOME/.signing_keys:ro" \
  -w $HOME \
  -e NCS_VERSION="$NCS_VERSION" \
  -e PROJECT_DIR="${PROJECT_DIR}" \
  -e SCRIPT_PATH=${SCRIPT_PATH} \
  -e BUILD_DIR_NAME=${BUILD_DIR_NAME} \
  "$IMAGE" \
  bash -lc '
    set -xeuo pipefail

    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"

    install_ncs_v2.8.0.sh
    source /usr/local/bin/dev-env-v2.8.0.sh

    cd $HOME/ncs/${NCS_VERSION}/${PROJECT_DIR}

    echo ">>> west --version"
    west --version || true
    echo ">>> python3 --version"
    python3 --version || true

    CPU_COUNT=$(getconf _NPROCESSORS_ONLN)
    echo ">>> Detected ${CPU_COUNT} CPU cores available"

    echo ">>> Running build script: ${SCRIPT_PATH}"
    "./${SCRIPT_PATH}" --build_dir="${BUILD_DIR_NAME}"
  '
