#!/bin/bash

set -e # Exit script on non-zero command exit status
set -x # Print commands and their arguments as they are executed.

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
export PATH=$PATH:$HOME/bin/build-wrapper-linux-x86

echo Initial cleanup
find . -type f -name "*.gcno" -exec rm -f {} \;
find . -type f -name '*.gcna' -exec rm -f {} \;
find . -type f -name "*.gcov" -exec rm -f {} \;
find . -type f -name 'gtestresults.xml' -exec rm -f {} \;
rm -rf build-sonar
mkdir -p build-sonar
cd build-sonar
which build-wrapper-linux-x86-64
build-wrapper-linux-x86-64 --out-dir ./bw-output bash ../build_ruuviair_a3_release-dev.sh --build_dir=$(realpath -m "./bw-output")
cd ..

rm -f coverage.xml
twister -c -p native_sim -v -T . --coverage --coverage-tool gcovr
gcovr \
  --object-directory twister-out \
  --sonarqube \
  --verbose \
  --output coverage.xml \
  --exclude 'tests/.*' \
  --exclude 'components/.*'

find . -type f -name "*.gcno" -exec rm -f {} \;
find . -type f -name '*.gcna' -exec rm -f {} \;
find . -type f -name "*.gcov" -exec rm -f {} \;
find . -type f -name 'gtestresults.xml' -exec rm -f {} \;
rm -rf twister-out

which sonar-scanner
sonar-scanner --version

if [ "$SONAR_TOKEN_ruuvi_air" = "" ]; then
  echo Warnings: Environment variable "SONAR_TOKEN_ruuvi_air" is not set
  echo "Skipping SonarQube scan"
else
  export SONAR_TOKEN=$SONAR_TOKEN_ruuvi_air
  sonar-scanner --debug \
    --define sonar.cfamily.build-wrapper-output="build-sonar/bw-output" \
    --define sonar.coverageReportPaths=coverage.xml \
    --define sonar.host.url=https://sonarcloud.io \
    --define sonar.cfamily.threads=$(nproc)
  rm -f coverage.xml
  rm -rf build-sonar
  rm -rf .scannerwork
fi
