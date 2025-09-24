#!/usr/bin/env bash

# This script builds the Ruuvi Air firmware for a specific board and revision.
# It requires several command line arguments to specify the board, revision, build mode, etc.
# Usage: ./build.sh --board={ruuviair|nrf52840dk} --board_rev={1|2} --build_mode={debug|release} [--build_dir_suffix=<suffix>] [--extra_cflags=<cflags>] [--trace] [--flash] [--flash_ext]
# Example: ./build.sh --board=ruuviair --board_rev=1 --build_mode=debug --build_dir_suffix=mock --extra_cflags="-DRUUVI_MOCK_MEASUREMENTS=1"

set -x -e # Print commands and their arguments and Exit immediately if a command exits with a non-zero status.

CUR_DIR_NAME=$(basename "$PWD")

# Ensure that ZEPHYR_BASE is set
if [ -z "$ZEPHYR_BASE" ]; then
  echo "Error: ZEPHYR_BASE is not set. Please set it to the Zephyr base directory." >&2
  exit 1
fi
# Ensure that west is installed
if ! command -v west &> /dev/null; then
  echo "Error: west is not installed. Please install it to continue." >&2
  exit 1
fi
# Ensure that srec_cat is installed
if ! command -v srec_cat &> /dev/null; then
  echo "Error: srec_cat is not installed. Please install it to continue." >&2
  exit 1
fi
# Ensure that python3 is installed
if ! command -v python3 &> /dev/null; then
  echo "Error: python3 is not installed. Please install it to continue." >&2
  exit 1
fi
# Ensure that mergehex.py is available
if [ ! -f "$ZEPHYR_BASE/scripts/build/mergehex.py" ]; then
  echo "Error: mergehex.py not found in $ZEPHYR_BASE/scripts/build/. Please ensure Zephyr is properly installed." >&2
  exit 1
fi
# Ensure that the signing keys directory exists
if [ ! -d "$HOME/.signing_keys" ]; then
  echo "Error: ~/.signing_keys directory does not exist. Please ensure the signing keys are set up correctly." >&2
  exit 1
fi

# Initialize defaults (if any)
board=""
board_rev=""
build_mode=""
build_dir_suffix=""
extra_cflags=""
extra_conf=""
trace=false
flash=false
flash_ext=false
flag_clean=false
flag_prod=false

# Loop over all arguments
for arg in "$@"; do
  case $arg in
    --board=*)
      board="${arg#*=}"
      shift
      ;;
    --board_rev=*)
      board_rev="${arg#*=}"
      shift
      ;;
    --build_mode=*)
      build_mode="${arg#*=}"
      shift
      ;;
    --build_dir_suffix=*)
      build_dir_suffix="${arg#*=}"
      shift
      ;;
    --extra_cflags=*)
      extra_cflags="${arg#*=}"
      shift
      ;;
    --extra_conf=*)
      extra_conf="${arg#*=}"
      shift
      ;;
    --trace)
      trace=true
      shift
      ;;
    --clean)
      flag_clean=true
      shift
      ;;
    --prod)
      flag_prod=true
      shift
      ;;
    --flash)
      flash=true
      shift
      ;;
    --flash_ext)
      flash_ext=true
      shift
      ;;
    *)
      echo "Error: Unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done

# Ensure that the signing keys are available
if [ "$flag_prod" = true ]; then
  if [ ! -f "$HOME/.signing_keys/b0_sign_key_private-prod.pem" ]; then
    echo "Error: b0_sign_key_private-prod.pem not found in ~/.signing_keys/. Please ensure the signing keys are set up correctly." >&2
    exit 1
  fi
  if [ ! -f "$HOME/.signing_keys/image_sign-prod.pem" ]; then
    echo "Error: image_sign-prod.pem not found in ~/.signing_keys/. Please ensure the signing keys are set up correctly." >&2
    exit 1
  fi
else
  if [ ! -f "$HOME/.signing_keys/b0_sign_key_private-dev.pem" ]; then
    echo "Error: b0_sign_key_private-dev.pem not found in ~/.signing_keys/. Please ensure the signing keys are set up correctly." >&2
    exit 1
  fi
  if [ ! -f "$HOME/.signing_keys/b0_sign_key_public-prod.pem" ]; then
    echo "Error: b0_sign_key_public-prod.pem not found in ~/.signing_keys/. Please ensure the signing keys are set up correctly." >&2
    exit 1
  fi
  if [ ! -f "$HOME/.signing_keys/image_sign-dev.pem" ]; then
    echo "Error: image_sign-dev.pem not found in ~/.signing_keys/. Please ensure the signing keys are set up correctly." >&2
    exit 1
  fi
fi

if [ -z "$board" ]; then
  echo "Error: --board is required" >&2
  exit 1
fi

if [ -z "$board_rev" ]; then
  echo "Error: --board_rev is required" >&2
  exit 1
fi
if [[ ! "$board_rev" =~ ^[0-9]+$ ]]; then
  echo "Error: --board_rev must be an integer" >&2
  exit 1
fi

if [ -z "$build_mode" ]; then
  echo "Error: --build_mode is required" >&2
  exit 1
fi
if [[ ! "$build_mode" =~ ^(debug|release)$ ]]; then
  echo "Error: --build_mode must be 'debug' or 'release'" >&2
  exit 1
fi

NRFJPROG_CFG="nrfjprog_cfg_$board.toml"
BOARD_DTS_OVERLAY="dts_$board.overlay"

if [ "$flash_ext" = true ] || [ "$flash" = true ]; then
  # Ensure that nrfjprog is installed
  if ! command -v nrfjprog &> /dev/null; then
    echo "Error: nrfjprog is not installed. Please install it to continue." >&2
    exit 1
  fi
fi

warn_default() {
  echo "Warning: no valid tag matching '${prefix}[0..255].[0..255].[0..255][.0..255]' found; using 0.0.0+0" >&2
  printf '0.0.0+0\n'
}

# Usage: ver=$(git_tag_to_semver mcuboot_v)   # -> "2.1.0+3"
#        ver=$(git_tag_to_semver v)           # for the app
git_tag_to_semver() {
  local prefix="${1:?usage: git_tag_to_semver <TAG_PREFIX>}"  # e.g. mcuboot_v | b0_v | v
  local workdir="${2:-.}"

  # 1) get a describe string limited to tags that start with the prefix
  local desc
  if ! desc=$(git -C "$workdir" describe --abbrev=12 --always --tags --dirty --match "${prefix}*" 2>/dev/null); then
    desc=""
  fi
  [[ -z "$desc" ]] && { warn_default; return 0; }

  # 2) ensure it actually starts with the prefix we requested
  if [[ "$desc" != "$prefix"* ]]; then
    warn_default; return 0
  fi

  # 3) strip the prefix and parse version numbers from the start
  local clean="${desc#$prefix}"

  # Expect: MAJOR.MINOR.PATCH[.TWEAK] immediately after prefix
  # captures: 1=MAJOR, 2=MINOR, 3=PATCH, 5=TWEAK
  local re='^([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})(\.([0-9]{1,3}))?'
  local MAJOR MINOR PATCH TWEAK have_tag_tweak=0
  if [[ "$clean" =~ $re ]]; then
    MAJOR="${BASH_REMATCH[1]}"
    MINOR="${BASH_REMATCH[2]}"
    PATCH="${BASH_REMATCH[3]}"
    if [[ -n "${BASH_REMATCH[5]}" ]]; then
      TWEAK="${BASH_REMATCH[5]}"
      have_tag_tweak=1
    fi
  else
    warn_default; return 0
  fi

  # 4) range check 0..255 for the three mandatory parts (and tweak if present in tag)
  for n in "$MAJOR" "$MINOR" "$PATCH"; do
    if (( n < 0 || n > 255 )); then
      warn_default; return 0
    fi
  done
  if (( have_tag_tweak == 1 )) && (( TWEAK < 0 || TWEAK > 255 )); then
    warn_default; return 0
  fi

  # 5) if no 4th numeric in the tag, fall back to commits-ahead from the full desc
  if (( have_tag_tweak == 0 )); then
    if [[ "$desc" =~ -([0-9]+)-g[0-9a-fA-F]+ ]]; then
      TWEAK="${BASH_REMATCH[1]}"
    else
      TWEAK=0
    fi
  fi

  # 6) emit MAJOR.MINOR.PATCH+TWEAK
  printf '%s.%s.%s+%s\n' "$MAJOR" "$MINOR" "$PATCH" "$TWEAK"
}


# Usage: major=$(major_from_ver "1.2.3+4")
major_from_ver() {
  local ver="$1"
  if [[ $ver =~ ^([0-9]+)\.[0-9]+\.[0-9]+\+[0-9]+$ ]]; then
    printf "${BASH_REMATCH[1]}"
  else
    printf '0'
  fi
}

b0_ver=$(git_tag_to_semver b0_v)
mcuboot_ver=$(git_tag_to_semver mcuboot_v)
fwloader_ver=$(git_tag_to_semver fwloader_v)
app_ver=$(git_tag_to_semver v)

b0_major=$(major_from_ver "$b0_ver")
mcuboot_major=$(major_from_ver "$mcuboot_ver")
fwloader_major=$(major_from_ver "$fwloader_ver")
app_major=$(major_from_ver "$app_ver")

echo "B0 version: $b0_ver"
echo "MCUBOOT version: $mcuboot_ver"
echo "FWLOADER version: $fwloader_ver"
echo "APP version: $app_ver"

echo "B0 major: $b0_major"
echo "MCUBOOT major: $mcuboot_major"
echo "FWLOADER major: $fwloader_major"
echo "APP major: $app_major"

BUILD_SUFFIX=""
EXTRA_FLAGS=()
case "$build_mode" in
  debug)
    CONF_FILE="prj_common.conf;prj_hw${board_rev}.conf;prj_${build_mode}.conf"
    EXTRA_FLAGS+=(
    )
    ;;
  release)
    CONF_FILE="prj_common.conf;prj_hw${board_rev}.conf;prj_mcumgr.conf;prj_${build_mode}.conf"
    EXTRA_FLAGS+=(
      -Db0_EXTRA_CONF_FILE="$PWD/sysbuild/b0.conf"
      -Dmcuboot_EXTRA_CONF_FILE="$PWD/sysbuild/mcuboot.conf"
      -DSB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION=\"${mcuboot_ver}\"
      -Dfirmware_loader_CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"${fwloader_ver}\"
      -DCONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"${app_ver}\"

      -Db0_CONFIG_FW_INFO_FIRMWARE_VERSION=${b0_major}
      -Dmcuboot_CONFIG_FW_INFO_FIRMWARE_VERSION=${mcuboot_major}
      -Dfirmware_loader_CONFIG_FW_INFO_FIRMWARE_VERSION=${fwloader_major}
      -DCONFIG_FW_INFO_FIRMWARE_VERSION=${app_major}

      -Db0_EXTRA_DTC_OVERLAY_FILE="$PWD/sysbuild/b0/$BOARD_DTS_OVERLAY;$PWD/sysbuild/b0/dts_common.overlay"
      -Dmcuboot_EXTRA_DTC_OVERLAY_FILE="$PWD/sysbuild/mcuboot/$BOARD_DTS_OVERLAY;$PWD/sysbuild/mcuboot/dts_common.overlay"
      -Dfirmware_loader_EXTRA_DTC_OVERLAY_FILE="$PWD/fw_loader/$BOARD_DTS_OVERLAY;$PWD/fw_loader/dts_common.overlay"
    )
    if [ "$flag_prod" = true ]; then
      BUILD_SUFFIX="-prod"
      EXTRA_FLAGS+=(
        -DSB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE=\"$HOME/.signing_keys/b0_sign_key_private-prod.pem\"
        -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$HOME/.signing_keys/image_sign-prod.pem\"
      )
    else
      BUILD_SUFFIX="-dev"
      EXTRA_FLAGS+=(
        -DSB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE=\"$HOME/.signing_keys/b0_sign_key_private-dev.pem\"
        -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$HOME/.signing_keys/image_sign-dev.pem\"
        -DSB_CONFIG_SECURE_BOOT_PUBLIC_KEY_FILES=\"$HOME/.signing_keys/b0_sign_key_public-prod.pem\"
      )
    fi
    ;;
  *)
    echo "Error: --build_mode must be 'debug' or 'release'" >&2
    exit 1
    ;;
esac

if [ -n "$extra_conf" ]; then
  CONF_FILE="$CONF_FILE;$extra_conf"
fi

BUILD_DIR="build_${board}_hw${board_rev}_${build_mode}${build_dir_suffix:+_$build_dir_suffix}${BUILD_SUFFIX}"

if [ "$flag_clean" = true ]; then
  rm -rf $BUILD_DIR
fi

EXTRA_DTC_OVERLAY_FILE="dts_${board}.overlay;dts_${board}_hw${board_rev}.overlay;dts_common.overlay"

case "$board" in
  ruuviair)
    WEST_BOARD="ruuvi_ruuviair@$board_rev/nrf52840"
    BOARD_NORMALIZED="ruuvi_ruuviair_nrf52840"
    ;;
  nrf52840dk)
    WEST_BOARD="nrf52840dk_ruuviair@$board_rev/nrf52840"
    BOARD_NORMALIZED="nrf52840dk_nrf52840"
    ;;
  *)
    echo "Error: Unsupported --board" >&2
    exit 1
    ;;
esac

echo "EXTRA_FLAGS: ${EXTRA_FLAGS[@]}"

PM_STATIC_YML_FILE="$PWD/pm_static_${BOARD_NORMALIZED}_${build_mode}.yml"

west_args=(
  -b $WEST_BOARD
  -d $BUILD_DIR
  --sysbuild
  .
  --
  -DCMAKE_BUILD_TYPE=${build_mode^} # Capitalize first letter
  -DCMAKE_VERBOSE_MAKEFILE=ON
  -DFILE_SUFFIX=${build_mode}
  -DBOARD_ROOT=.
  -DPM_STATIC_YML_FILE="${PM_STATIC_YML_FILE}"
  -DEXTRA_DTC_OVERLAY_FILE="${EXTRA_DTC_OVERLAY_FILE}"
  -DCONF_FILE=${CONF_FILE}
)
west_args+=( "${EXTRA_FLAGS[@]}" )

if [ -n "$extra_cflags" ]; then
  west_args+=( -DEXTRA_CFLAGS="$extra_cflags" )
fi

# Function to create a version file with zero version
create_version_file() {
    if [[ -z "$1" ]]; then
        echo "Error: You must provide a file path as an argument." >&2
        return 1
    fi

    local file_path="$1"

    if [[ ! -f "$file_path" ]]; then
        cat > "$file_path" << EOF
VERSION_MAJOR = 0
VERSION_MINOR = 0
PATCHLEVEL = 0
VERSION_TWEAK = 0
EXTRAVERSION =
EOF
    fi
}

create_version_file "./b0_hook/VERSION"
create_version_file "./mcuboot_hook/VERSION"
create_version_file "./fw_loader/VERSION"
create_version_file "./VERSION"

if [ "$trace" = true ]; then
  echo "Tracing enabled – printing extra debug info…"
  west_args+=( --trace --trace-expand )
  west build "${west_args[@]}" 2>&1 | tee build.txt
else
  west build "${west_args[@]}"
fi

RUUVI_AIR_BUILD_DIR="$BUILD_DIR/$CUR_DIR_NAME/zephyr"

# check if build_mode is release
if [ "$build_mode" = "release" ]; then
  python3 \
    $ZEPHYR_BASE/scripts/build/mergehex.py \
      -o $BUILD_DIR/merged.hex \
      --overlap=replace \
      $BUILD_DIR/app_provision.hex \
      $BUILD_DIR/b0_container.hex \
      $BUILD_DIR/s0_image.hex \
      $BUILD_DIR/s0.hex \
      $BUILD_DIR/s1.hex \
      $BUILD_DIR/mcuboot_secondary_app.hex \
      $BUILD_DIR/mcuboot_secondary.hex \
      $BUILD_DIR/b0/zephyr/ruuvi_air_b0.hex \
      $BUILD_DIR/signed_by_mcuboot_and_b0_mcuboot.hex \
      $BUILD_DIR/signed_by_mcuboot_and_b0_s1_image.hex \
      $BUILD_DIR/firmware_loader/zephyr/ruuvi_air_fw_loader.signed.hex \
      $RUUVI_AIR_BUILD_DIR/ruuvi_air_fw.signed.hex


  OFFSET=0x12000000
  srec_cat "$BUILD_DIR/app_provision.hex" -intel --offset $OFFSET -o "$BUILD_DIR/app_provision.ext_flash.hex" -intel
  srec_cat "$BUILD_DIR/signed_by_mcuboot_and_b0_mcuboot.hex" -intel --offset $OFFSET -o "$BUILD_DIR/signed_by_mcuboot_and_b0_mcuboot.ext_flash.hex" -intel
  srec_cat "$BUILD_DIR/signed_by_mcuboot_and_b0_s1_image.hex" -intel --offset $OFFSET -o "$BUILD_DIR/signed_by_mcuboot_and_b0_s1_image.ext_flash.hex" -intel
  srec_cat "$BUILD_DIR/firmware_loader/zephyr/ruuvi_air_fw_loader.signed.hex" -intel --offset $OFFSET -o "$BUILD_DIR/firmware_loader/zephyr/ruuvi_air_fw_loader.signed.ext_flash.hex" -intel
  srec_cat "$RUUVI_AIR_BUILD_DIR/ruuvi_air_fw.signed.hex" -intel --offset $OFFSET -o "$RUUVI_AIR_BUILD_DIR/ruuvi_air_fw.signed.ext_flash.hex" -intel
  srec_cat \
      "$BUILD_DIR/app_provision.ext_flash.hex" -intel \
      "$BUILD_DIR/signed_by_mcuboot_and_b0_mcuboot.ext_flash.hex" -intel \
      "$BUILD_DIR/signed_by_mcuboot_and_b0_s1_image.ext_flash.hex" -intel \
      "$BUILD_DIR/firmware_loader/zephyr/ruuvi_air_fw_loader.signed.ext_flash.hex" -intel \
      "$RUUVI_AIR_BUILD_DIR/ruuvi_air_fw.signed.ext_flash.hex" -intel \
      -o "$BUILD_DIR/merged.ext_flash.hex" -intel
fi

if [ "$flash_ext" = true ]; then
  if [ ! -f "$NRFJPROG_CFG" ]; then
    echo "Error: $NRFJPROG_CFG does not exist." >&2
    exit 1
  fi
  nrfjprog -f nrf52 --config $NRFJPROG_CFG --program "$BUILD_DIR/merged.ext_flash.hex" --qspisectorerase --verify
fi

if [ "$flash" = true ]; then
  flash_args=(
    --program $BUILD_DIR/merged.hex
    --chiperase
    --verify
    --hardreset
  )
  nrfjprog ${flash_args[@]}
fi
