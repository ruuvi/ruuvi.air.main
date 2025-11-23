#!/usr/bin/env bash
#
# SonarCube Scan RuuviAir firmware using the Docker image.
#

set -xeuo pipefail

PRJ_ABS_PATH=$(realpath "$PWD")

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

mkdir -p "$HOME/bin"
if [ ! -d "$HOME/bin/sonar-scanner" ]; then
  # Get the link to the latest version from:
  # https://docs.sonarsource.com/sonarqube-server/10.6/analyzing-source-code/scanners/sonarscanner
  SONAR_SCANNER_VER="7.3.0.5189"
  SONAR_SCANNER_ZIP="sonar-scanner-cli-$SONAR_SCANNER_VER-linux-x64.zip"
  rm -f "$HOME/bin/$SONAR_SCANNER_ZIP"
  wget -P "$HOME/bin" "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/$SONAR_SCANNER_ZIP"
  unzip -q -o "$HOME/bin/$SONAR_SCANNER_ZIP" -d "$HOME/bin"
  mv "$HOME/bin/sonar-scanner-$SONAR_SCANNER_VER-linux-x64" "$HOME/bin/sonar-scanner"
fi
export PATH=$PATH:$HOME/bin/sonar-scanner/bin

rm -f "$HOME/bin/build-wrapper-linux-x86.zip"
wget -P "$HOME/bin" https://sonarcloud.io/static/cpp/build-wrapper-linux-x86.zip
unzip -o "$HOME/bin/build-wrapper-linux-x86.zip" -d "$HOME/bin"

echo Initial cleanup
find . -type f -name "*.gcno" -exec rm -f {} \;
find . -type f -name '*.gcna' -exec rm -f {} \;
find . -type f -name "*.gcov" -exec rm -f {} \;
find . -type f -name 'gtestresults.xml' -exec rm -f {} \;
rm -rf .scannerwork || true
rm -rf b0_hook/.scannerwork || true
rm -rf mcuboot_hook/.scannerwork || true
rm -rf fw_loader/.scannerwork || true
rm -rf build-sonar || true
rm -rf twister-out || true
rm -f coverage.xml || true

PROJECT_DIR=$(basename "$PWD")
KEYS_DIR="$HOME/.signing_keys"
NCS_DOCKER_DIR="$HOME/ncs_docker"
USERNAME="$(id -un)"

# --- Print info ---
echo ">>> Building firmware in Docker"
echo "    Image          : ${IMAGE}"
echo "    NCS version    : ${NCS_VERSION}"
echo "    Signing keys   : ${KEYS_DIR}"
echo "    NCS Docker dir : ${NCS_DOCKER_DIR}"
echo

mkdir -p "$NCS_DOCKER_DIR"
mkdir -p "$NCS_DOCKER_DIR/bin"
mkdir -p "$NCS_DOCKER_DIR/ncs"
mkdir -p "$NCS_DOCKER_DIR/ncs/${NCS_VERSION}"
mkdir -p "$NCS_DOCKER_DIR/ncs/toolchains"
mkdir -p "$NCS_DOCKER_DIR/.nrfutil"

docker run --rm -it \
  --network none \
  --cap-drop NET_ADMIN \
  --cap-drop NET_RAW \
  --cap-drop NET_BIND_SERVICE \
  --user "$(id -u)":"$(id -g)" \
  -e HOME=/home/$USERNAME \
  -e USER=$USERNAME \
  -e HOST_UID="$(id -u)" \
  -e HOST_GID="$(id -g)" \
  -e HOST_USER=$USERNAME \
  -e HOST_GROUP="$(id -gn)" \
  -v "$NCS_DOCKER_DIR:/home/$USERNAME" \
  -v "$(pwd):/home/$USERNAME/ncs/${NCS_VERSION}/${PROJECT_DIR}" \
  -v "$KEYS_DIR:/home/$USERNAME/.signing_keys:ro" \
  -v "$HOME/bin/build-wrapper-linux-x86:/home/$USERNAME/bin/build-wrapper-linux-x86" \
  -v "$HOME/bin/sonar-scanner:/home/$USERNAME/bin/sonar-scanner" \
  -w /home/$USERNAME \
  -e NCS_VERSION="$NCS_VERSION" \
  -e PROJECT_DIR="${PROJECT_DIR}" \
  "$IMAGE" \
  bash -lc '
    set -xeuo pipefail

    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"

    export PATH="$HOME/bin/build-wrapper-linux-x86:$HOME/bin/sonar-scanner/bin:$PATH"
    which build-wrapper-linux-x86-64
    which sonar-scanner

    install_ncs_${NCS_VERSION}.sh
    source /usr/local/bin/dev-env-${NCS_VERSION}.sh

    cd $HOME/ncs/${NCS_VERSION}/${PROJECT_DIR}

    mkdir -p build-sonar
    cd build-sonar
    build-wrapper-linux-x86-64 --out-dir ./bw-output bash ../build_ruuviair_a3_release-dev.sh --build_dir=$(realpath -m "./bw-output")
    cd ..

    twister -c -p native_sim -v -T . --coverage --coverage-tool gcovr
    gcovr \
      --object-directory twister-out \
      --sonarqube \
      --output coverage.xml \
      --exclude "tests/.*" \
      --exclude "components/.*"

    ls -la coverage.xml
    sleep 1

    find . -type f -name "*.gcno" -exec rm -f {} \;
    find . -type f -name "*.gcna" -exec rm -f {} \;
    find . -type f -name "*.gcov" -exec rm -f {} \;
    find . -type f -name "gtestresults.xml" -exec rm -f {} \;
    rm -rf twister-out

    # if [ ! -z "$SONAR_TOKEN" ]; then
    #   sonar-scanner --debug \
    #     --define sonar.cfamily.compile-commands=build-sonar/bw-output/compile_commands.json \
    #     --define sonar.coverageReportPaths=coverage.xml
    # fi
  '

which sonar-scanner
sonar-scanner --version

if [ "$SONAR_TOKEN_ruuvi_air" = "" ]; then
  echo Warnings: Environment variable "SONAR_TOKEN_ruuvi_air" is not set
  echo "Skipping SonarQube scan"
else
  export SONAR_TOKEN=$SONAR_TOKEN_ruuvi_air
  cd $PRJ_ABS_PATH
  sonar-scanner --debug \
    --define sonar.cfamily.compile-commands=build-sonar/bw-output/compile_commands.json \
    --define sonar.coverageReportPaths=coverage.xml \
    --define sonar.cfamily.threads=$(nproc)
  rm -rf .scannerwork
  cd b0_hook
  sonar-scanner --debug \
    --define sonar.cfamily.compile-commands=../build-sonar/bw-output/compile_commands.json \
    --define sonar.coverageReportPaths=../coverage.xml \
    --define sonar.cfamily.threads=$(nproc)
  rm -rf .scannerwork
  cd ../mcuboot_hook
  sonar-scanner --debug \
    --define sonar.cfamily.compile-commands=../build-sonar/bw-output/compile_commands.json \
    --define sonar.coverageReportPaths=../coverage.xml \
    --define sonar.cfamily.threads=$(nproc)
  rm -rf .scannerwork
  cd ../fw_loader
  sonar-scanner --debug \
    --define sonar.cfamily.compile-commands=../build-sonar/bw-output/compile_commands.json \
    --define sonar.coverageReportPaths=../coverage.xml \
    --define sonar.cfamily.threads=$(nproc)
  rm -rf .scannerwork
  cd ..
  rm -f coverage.xml
  rm -rf build-sonar
fi
