#!/usr/bin/env bash

# This script builds the Ruuvi Air firmware for a specific board and revision.
# It requires several command line arguments to specify the board, revision, build mode, etc.
# Usage: ./build.sh --board={ruuviair|nrf52840dk} --board_rev_name={RuuviAir-A1|RuuviAir-A2|RuuviAir-A3} --build_mode={debug|release} [--build_dir_suffix=<suffix>] [--extra_cflags=<cflags>] [--trace] [--flash] [--flash_ext]
# Example: ./build.sh --board=ruuviair --board_rev_name=RuuviAir-A3 --build_mode=debug --build_dir_suffix=mock --extra_cflags="-DRUUVI_MOCK_MEASUREMENTS=1"

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
BUILD_DIR=""
board=""
board_rev_name=""
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
    --build_dir=*)
      BUILD_DIR="${arg#*=}"
      shift
      ;;
    --board=*)
      board="${arg#*=}"
      shift
      ;;
    --board_rev_name=*)
      board_rev_name="${arg#*=}"
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

if [ -z "$board_rev_name" ]; then
  echo "Error: --board_rev is required" >&2
  exit 1
fi
if [ "$board_rev_name" == "RuuviAir-A1" ]; then
  board_suffix="a1"
  board_rev=1
elif [ "$board_rev_name" == "RuuviAir-A2" ]; then
  board_suffix="a2"
  board_rev=2
elif [ "$board_rev_name" == "RuuviAir-A3" ]; then
  board_suffix="a3"
  # A3 is fully compatible with A2, from the firmware perspective
  board_rev=2
else
  echo "Error: Unknown board_rev_name '$board_rev_name'. Supported values are 'RuuviAir-A1', 'RuuviAir-A2', 'RuuviAir-A3'." >&2
  exit 1
fi
board_rev_id_hex=$(printf "0x%08X" $board_rev)

# IMAGE_TLV_RUUVI_HW_REV_ID:   0x48A0 = 18592
CUSTOM_TLV_RUUVI_HW_REV_ID=18592
# IMAGE_TLV_RUUVI_HW_REV_NAME: 0x48A1 = 18593
CUSTOM_TLV_RUUVI_HW_REV_NAME=18593


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

# Usage:
#   ver=$(git_tag_to_semver mcuboot_v)                 # -> "2.1.0+3"
#   ver=$(git_tag_to_semver v . dev)                  # -> "1.0.3+1-dirty-dev"
# Notes:
#   - If you pass extraversion_suffix as "", nothing extra is appended.
#   - If extraversion_suffix is non-empty and doesn't start with '-', a '-' is added.
git_tag_to_semver() {
  local prefix="${1:?usage: git_tag_to_semver <TAG_PREFIX> [workdir=. ] [extraversion_suffix]}"  # e.g. mcuboot_v | b0_v | v
  local workdir="${2:-.}"
  local extra_suffix="${3-}"  # optional; may be empty string

  echo "### git_tag_to_semver: prefix='$prefix' workdir='$workdir' extra_suffix='$extra_suffix'" >&2

  # normalize suffix: add leading '-' if provided and missing
  if [[ -n "${extra_suffix}" && "${extra_suffix}" != -* ]]; then
    extra_suffix="-${extra_suffix}"
  fi

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

  # 5) if no 4th numeric in the tag, fall back to commits-ahead
  if (( have_tag_tweak == 0 )); then
    if [[ "$clean" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}-([0-9]+)-g[0-9a-fA-F]+ ]]; then
      TWEAK="${BASH_REMATCH[1]}"
      # cap at 255 only in the commits-ahead case
      if (( TWEAK > 255 )); then
        TWEAK=255
      fi
    else
      TWEAK=0
    fi
  fi

  # 6) Get extra version from the tag suffix (e.g. -rc1)
  local extra_version=""
  if [[ "$clean" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}-[0-9]+-g[0-9a-fA-F]+(-.*)$ ]]; then
    extra_version="${BASH_REMATCH[1]}"
  else
    if [[ "$clean" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}-[0-9]+-g[0-9a-fA-F]+(-.*)$ ]]; then
      extra_version="${BASH_REMATCH[1]}"
    fi
  fi

  # 8) append optional extraversion_suffix (already normalized to start with '-' if non-empty)
  local full_extra_version="${extra_suffix}${extra_version}"

  # 6) emit MAJOR.MINOR.PATCH+TWEAK
  printf '%s.%s.%s+%s%s' "$MAJOR" "$MINOR" "$PATCH" "$TWEAK" "$full_extra_version"
}


# Usage: major=$(major_from_ver "1.2.3+4-dev")
major_from_ver() {
  local ver="$1"
  if [[ $ver =~ ^([0-9]{1,3})\.[0-9]{1,3}\.[0-9]{1,3}\+[0-9]{1,3} ]]; then
    printf "${BASH_REMATCH[1]}"
  else
    printf '0'
  fi
}

minor_from_ver() {
  local ver="$1"
  if [[ $ver =~ ^[0-9]{1,3}\.([0-9]{1,3})\.[0-9]{1,3}\+[0-9]{1,3} ]]; then
    printf "${BASH_REMATCH[1]}"
  else
    printf '0'
  fi
}

patch_from_ver() {
  local ver="$1"
  if [[ $ver =~ ^[0-9]{1,3}\.[0-9]{1,3}\.([0-9]{1,3})\+[0-9]{1,3} ]]; then
    printf "${BASH_REMATCH[1]}"
  else
    printf '0'
  fi
}

tweak_from_ver() {
  local ver="$1"
  if [[ $ver =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\+([0-9]{1,3}) ]]; then
    printf "${BASH_REMATCH[1]}"
  else
    printf '0'
  fi
}


# Usage: base=$(base_from_ver "1.2.3+4-dev")
base_from_ver() {
  local ver="$1"
  if [[ "$ver" =~ ^([0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\+[0-9]{1,3}) ]]; then
    printf "${BASH_REMATCH[1]}"
  else
    printf '0.0.0+0'
  fi
}

# Usage: extra_ver=$(get_extra_ver "1.2.3+4-dev")
get_extra_ver() {
  local ver="$1"
  if [[ $ver =~ -(.*)$ ]]; then
    printf "${BASH_REMATCH[1]}"
  else
    printf ''
  fi
}

if [ "$flag_prod" = true ]; then
  extra_ver_suffix=""
else
  extra_ver_suffix="dev"
fi


b0_full_ver=$(git_tag_to_semver b0_v . $extra_ver_suffix)
mcuboot_full_ver=$(git_tag_to_semver mcuboot_v . $extra_ver_suffix)
fwloader_full_ver=$(git_tag_to_semver fwloader_v . $extra_ver_suffix)
app_full_ver=$(git_tag_to_semver v . $extra_ver_suffix)

b0_major_ver=$(major_from_ver "$b0_full_ver")
mcuboot_major_ver=$(major_from_ver "$mcuboot_full_ver")
fwloader_major_ver=$(major_from_ver "$fwloader_full_ver")
app_major_ver=$(major_from_ver "$app_full_ver")

b0_base_ver=$(base_from_ver "$b0_full_ver")
mcuboot_base_ver=$(base_from_ver "$mcuboot_full_ver")
fwloader_base_ver=$(base_from_ver "$fwloader_full_ver")
app_base_ver=$(base_from_ver "$app_full_ver")

b0_extra_ver=$(get_extra_ver "$b0_full_ver")
mcuboot_extra_ver=$(get_extra_ver "$mcuboot_full_ver")
fwloader_extra_ver=$(get_extra_ver "$fwloader_full_ver")
app_extra_ver=$(get_extra_ver "$app_full_ver")

echo "B0 version       : $b0_full_ver"
echo "MCUBOOT version  : $mcuboot_full_ver"
echo "FWLOADER version : $fwloader_full_ver"
echo "APP version      : $app_full_ver"

echo "B0 base ver       : $b0_base_ver"
echo "MCUBOOT base ver  : $mcuboot_base_ver"
echo "FWLOADER base ver : $fwloader_base_ver"
echo "APP base ver      : $app_base_ver"

echo "B0 major ver       : $b0_major_ver"
echo "MCUBOOT major ver  : $mcuboot_major_ver"
echo "FWLOADER major ver : $fwloader_major_ver"
echo "APP major ver      : $app_major_ver"

echo "B0 extra ver       : $b0_extra_ver"
echo "MCUBOOT extra ver  : $mcuboot_extra_ver"
echo "FWLOADER extra ver : $fwloader_extra_ver"
echo "APP extra ver      : $app_extra_ver"

create_version_file() {
  local out_path="${1}"
  local full_ver="${2}"

  if [[ -z "$out_path" ]]; then
      echo "Error: You must provide a file path as an argument." >&2
      return 1
  fi
  if [[ -z "$full_ver" ]]; then
      echo "Error: You must provide a full version string." >&2
      return 1
  fi

  local VERSION_MAJOR=$(major_from_ver "$full_ver")
  local VERSION_MINOR=$(minor_from_ver "$full_ver")
  local PATCHLEVEL=$(patch_from_ver "$full_ver")
  local VERSION_TWEAK=$(tweak_from_ver "$full_ver")
  local EXTRAVERSION=$(get_extra_ver "$full_ver")
  {
    printf 'VERSION_MAJOR = %s\n' "$VERSION_MAJOR"
    printf 'VERSION_MINOR = %s\n' "$VERSION_MINOR"
    printf 'PATCHLEVEL = %s\n'    "$PATCHLEVEL"
    printf 'VERSION_TWEAK = %s\n' "$VERSION_TWEAK"
    printf 'EXTRAVERSION = %s\n'  "$EXTRAVERSION"
  } >"$out_path"
  echo "Created version file at '$out_path'"
}

create_version_file "./b0_hook/VERSION" "$b0_full_ver"
create_version_file "./mcuboot_hook/VERSION" "$mcuboot_full_ver"
create_version_file "./fw_loader/VERSION" "$fwloader_full_ver"
create_version_file "./VERSION" "$app_full_ver"

BUILD_SUFFIX=""
EXTRA_FLAGS=()
IMAGE_SIGNING_KEY_FILE=""
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
      -DSB_CONFIG_SECURE_BOOT_MCUBOOT_VERSION=\"${mcuboot_base_ver}\"
      -Dfirmware_loader_CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"${fwloader_base_ver}\"
      -DCONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"${app_base_ver}\"
      "-DCONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS=\"--custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_ID $board_rev_id_hex --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_NAME $board_rev_name\""
      "-Dfirmware_loader_CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS=\"--custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_ID $board_rev_id_hex --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_NAME $board_rev_name\""

      -Db0_CONFIG_FW_INFO_FIRMWARE_VERSION=${b0_major_ver}
      -Dmcuboot_CONFIG_FW_INFO_FIRMWARE_VERSION=${mcuboot_major_ver}
      -Dfirmware_loader_CONFIG_FW_INFO_FIRMWARE_VERSION=${fwloader_major_ver}
      -DCONFIG_FW_INFO_FIRMWARE_VERSION=${app_major_ver}

      -Db0_EXTRA_DTC_OVERLAY_FILE="$PWD/sysbuild/b0/$BOARD_DTS_OVERLAY;$PWD/sysbuild/b0/dts_common.overlay"
      -Dmcuboot_EXTRA_DTC_OVERLAY_FILE="$PWD/sysbuild/mcuboot/$BOARD_DTS_OVERLAY;$PWD/sysbuild/mcuboot/dts_common.overlay"
      -Dfirmware_loader_EXTRA_DTC_OVERLAY_FILE="$PWD/fw_loader/$BOARD_DTS_OVERLAY;$PWD/fw_loader/dts_${board}_hw${board_rev}.overlay;$PWD/fw_loader/dts_common.overlay"
    )
    if [ "$flag_prod" = true ]; then
      BUILD_SUFFIX="-prod"
      IMAGE_SIGNING_KEY_FILE="$HOME/.signing_keys/image_sign-prod.pem"
      EXTRA_FLAGS+=(
        -DSB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE=\"$HOME/.signing_keys/b0_sign_key_private-prod.pem\"
        -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$IMAGE_SIGNING_KEY_FILE\"
      )
    else
      BUILD_SUFFIX="-dev"
      IMAGE_SIGNING_KEY_FILE="$HOME/.signing_keys/image_sign-dev.pem"
      EXTRA_FLAGS+=(
        -DSB_CONFIG_SECURE_BOOT_SIGNING_KEY_FILE=\"$HOME/.signing_keys/b0_sign_key_private-dev.pem\"
        -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$IMAGE_SIGNING_KEY_FILE\"
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

if [ -z "$BUILD_DIR" ]; then
  BUILD_DIR="build_${board}_${board_suffix}_${build_mode}${build_dir_suffix:+_$build_dir_suffix}${BUILD_SUFFIX}"
fi

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
    BOARD_NORMALIZED="nrf52840dk_ruuviair_nrf52840"
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
  s0_image_size=$(yq '.s0_image.size' "$PM_STATIC_YML_FILE")
  s1_image_size=$(yq '.s1_image.size' "$PM_STATIC_YML_FILE")

  python3 \
    "$ZEPHYR_BASE/../bootloader/mcuboot/scripts/imgtool.py" sign \
    --version $mcuboot_base_ver --align 4 --slot-size $s0_image_size \
    --pad-header --header-size 0x200 \
    -k $IMAGE_SIGNING_KEY_FILE \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_ID $board_rev_id_hex \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_NAME "$board_rev_name" \
    $BUILD_DIR/signed_by_b0_mcuboot.bin \
    $BUILD_DIR/signed_by_mcuboot_and_b0_mcuboot.bin
  python3 \
    "$ZEPHYR_BASE/../bootloader/mcuboot/scripts/imgtool.py" sign \
    --version $mcuboot_base_ver --align 4 --slot-size $s0_image_size \
    --pad-header --header-size 0x200 \
    -k $IMAGE_SIGNING_KEY_FILE \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_ID $board_rev_id_hex \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_NAME "$board_rev_name" \
    $BUILD_DIR/signed_by_b0_mcuboot.hex \
    $BUILD_DIR/signed_by_mcuboot_and_b0_mcuboot.hex
  python3 \
    "$ZEPHYR_BASE/../bootloader/mcuboot/scripts/imgtool.py" sign \
    --version $mcuboot_base_ver --align 4 --slot-size $s1_image_size \
    --pad-header --header-size 0x200 \
    -k $IMAGE_SIGNING_KEY_FILE \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_ID $board_rev_id_hex \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_NAME "$board_rev_name" \
    $BUILD_DIR/signed_by_b0_s1_image.bin \
    $BUILD_DIR/signed_by_mcuboot_and_b0_s1_image.bin
  python3 \
    "$ZEPHYR_BASE/../bootloader/mcuboot/scripts/imgtool.py" sign \
    --version $mcuboot_base_ver --align 4 --slot-size $s1_image_size \
    --pad-header --header-size 0x200 \
    -k $IMAGE_SIGNING_KEY_FILE \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_ID $board_rev_id_hex \
    --custom-tlv $CUSTOM_TLV_RUUVI_HW_REV_NAME "$board_rev_name" \
    $BUILD_DIR/signed_by_b0_s1_image.hex \
    $BUILD_DIR/signed_by_mcuboot_and_b0_s1_image.hex
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
