# cmake/gen_version.cmake
# Usage:
#   cmake -DOUT=<path-to-output> -DWORKDIR=<repo-root> -DTAG_PREFIX=<mcuboot_v|b0_v|v> -P gen_version.cmake

#
# Examples:
#   -DTAG_PREFIX="mcuboot_v"   → strips "mcuboot_v" from "mcuboot_v1-4-gabc-dirty"
#   -DTAG_PREFIX="b0_v"        → strips "b0_v" from "b0_v1-4-gabc-dirty"
#   -DTAG_PREFIX="v"           → strips "v" from app tags like "v1.2.3"
#
# Output file format:
#   VERSION_MAJOR = <0..255>
#   VERSION_MINOR = <0..255>
#   PATCHLEVEL    = <0..255>
#   VERSION_TWEAK = <0..255>     (commits ahead; capped at 255 with warning)
#   EXTRAVERSION  = <string>     (-rcX, -alpha.1, plus '-dirty' if present)

if(NOT DEFINED OUT)
  message(FATAL_ERROR "gen_version.cmake: OUT not set")
endif()
if(NOT DEFINED WORKDIR)
  set(WORKDIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
if(NOT DEFINED TAG_PREFIX OR TAG_PREFIX STREQUAL "")
  message(FATAL_ERROR "gen_version.cmake: TAG_PREFIX must be defined and non-empty (e.g. 'mcuboot_v', 'b0_v', or 'v').")
endif()

# Normalize: trim spaces and strip accidental surrounding quotes
string(STRIP "${TAG_PREFIX}" TAG_PREFIX)
string(REGEX REPLACE "^\"(.*)\"$" "\\1" TAG_PREFIX "${TAG_PREFIX}")

# Build the match pattern once
set(_match_pattern "${TAG_PREFIX}*")

# 1) Run git describe (quietly if not a git repo)
execute_process(
  COMMAND git -C "${WORKDIR}" describe --abbrev=12 --always --tags --dirty --match "${_match_pattern}"
  OUTPUT_VARIABLE DESC
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

# Fallback if not in a git repo
if("${DESC}" STREQUAL "")
  set(DESC "0.0.0-0-g000000000000")
endif()

# ---- Strip TAG_PREFIX from the start of DESC to form DESC_CLEAN ----
# Escape regex metacharacters in TAG_PREFIX for safe use inside ^${_rx}
set(_rx "${TAG_PREFIX}")
string(REGEX REPLACE "([][()+.*^$?|{}\\\\])" "\\\\\\1" _rx "${_rx}")

string(REGEX MATCH "^${_rx}" _has_prefix "${DESC}")
if(_has_prefix)
  string(REGEX REPLACE "^${_rx}" "" DESC_CLEAN "${DESC}")
else()
  message(WARNING
    "git describe returned '${DESC}', which does not start with required prefix '${TAG_PREFIX}'. "
    "Version numbers may default to 0 if no tag is reachable with that prefix."
  )
  set(DESC_CLEAN "0.0.0.0-dirty")
endif()

# 2) Extract MAJOR[.MINOR[.PATCH[.TWEAK]]]
#    Examples:
#      v1.2.3.4
#      v1.2.3
#      v1.2.3
#      v1.2.3-rc1
#      v1.2.3-4-gabcdef123456
#      mcuboot_v1
#      b0_v1-2-gabcdef123456
#      b0_v2
# Initialize defaults so missing parts become 0
set(VERSION_MAJOR "0")
set(VERSION_MINOR "0")
set(PATCHLEVEL    "0")

# Match start: MAJOR, optional .MINOR, optional .PATCH, optional .TWEAK
# Capture groups: 1=MAJOR, 3=MINOR, 5=PATCH, 7=TWEAK (if present in tag)
string(REGEX MATCH "^([0-9]+)(\\.([0-9]+))?(\\.([0-9]+))?(\\.([0-9]+))?" _ver "${DESC_CLEAN}")
set(_tweak_from_tag FALSE)
if(_ver)
  set(VERSION_MAJOR "${CMAKE_MATCH_1}")
  if(DEFINED CMAKE_MATCH_3 AND NOT CMAKE_MATCH_3 STREQUAL "")
    set(VERSION_MINOR "${CMAKE_MATCH_3}")
  endif()
  if(DEFINED CMAKE_MATCH_5 AND NOT CMAKE_MATCH_5 STREQUAL "")
    set(PATCHLEVEL "${CMAKE_MATCH_5}")
  endif()
  if(DEFINED CMAKE_MATCH_7 AND NOT CMAKE_MATCH_7 STREQUAL "")
    # Tag has 4th numeric field -> use as VERSION_TWEAK
    set(VERSION_TWEAK "${CMAKE_MATCH_7}")
    set(_tweak_from_tag TRUE)
  endif()
endif()

# 3) Derive VERSION_TWEAK from commits-ahead (default 0); based on the full DESC
#    - If tag had 4th number, keep it (already set above).
#    - Else use commits-ahead (matches ...-<N>-g<hash>... in full DESC).
if(NOT _tweak_from_tag)
  set(VERSION_TWEAK "0")
  string(REGEX MATCH "-([0-9]+)-g[0-9a-fA-F]+" _m "${DESC}")
  if(_m)
    string(REGEX REPLACE ".*-([0-9]+)-g[0-9a-fA-F]+.*" "\\1" VERSION_TWEAK "${DESC}")
  endif()
endif()

# 4) Compose EXTRAVERSION = pre-release (e.g., -rc1) + optional -dirty
# EXTRAVERSION: capture '-rcX' (or similar) after MAJOR[.MINOR[.PATCH[.TWEAK]]]
set(EXTRAVERSION "")
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?(\\.[0-9]+)?(-[A-Za-z][A-Za-z0-9\\.-]*)" _pre "${DESC_CLEAN}")
if(_pre)
  # The 4th capturing group is the pre-release chunk with leading '-'
  set(EXTRAVERSION "${CMAKE_MATCH_4}")
endif()
string(REGEX MATCH "-dirty$" _dirty "${DESC_CLEAN}")
if(_dirty)
  if (NOT EXTRAVERSION MATCHES ".*-dirty$")
    # Append '-dirty' if not already present
    set(EXTRAVERSION "${EXTRAVERSION}-dirty")
  endif()
endif()

# 5) Range checking:
#    - VERSION_MAJOR/MINOR/PATCHLEVEL must be 0..255 -> FATAL on violation
#    - VERSION_TWEAK must be 0..255 -> cap to 255 with a WARNING
foreach(var VERSION_MAJOR VERSION_MINOR PATCHLEVEL)
  if(${var} LESS 0 OR ${var} GREATER 255)
    message(FATAL_ERROR "${var}=${${var}} is out of allowed range (0..255), from tag '${DESC}'")
  endif()
endforeach()

# Cap VERSION_TWEAK with warning instead of failing
if(VERSION_TWEAK LESS 0)
  message(WARNING "VERSION_TWEAK=${VERSION_TWEAK} < 0 from '${DESC}'. Forcing to 0.")
  set(VERSION_TWEAK 0)
elseif(VERSION_TWEAK GREATER 255)
  message(WARNING "VERSION_TWEAK=${VERSION_TWEAK} > 255 from '${DESC}'. Capping to 255.")
  set(VERSION_TWEAK 255)
endif()

# 6) Write the file
file(WRITE "${OUT}" "VERSION_MAJOR = ${VERSION_MAJOR}\n")
file(APPEND "${OUT}" "VERSION_MINOR = ${VERSION_MINOR}\n")
file(APPEND "${OUT}" "PATCHLEVEL = ${PATCHLEVEL}\n")
file(APPEND "${OUT}" "VERSION_TWEAK = ${VERSION_TWEAK}\n")
file(APPEND "${OUT}" "EXTRAVERSION = ${EXTRAVERSION}\n")

