# Dockerfile
#
# @copyright Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause
#
# --------------------------------------------------------------------
# Thin base with all system deps only.
# NCS installation is deferred to /root/install_ncs_v2.8.0.sh (run at CI time).
# $HOME/.nrfutil and $HOME/ncs are volume-mounted from GitHub Action and cached between runs.
# --------------------------------------------------------------------

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
ENV NCS_VERSION=v2.8.0

# Core build + Zephyr/NCS deps + tools used in workflows (no nrfutil here)
RUN bash -euo pipefail <<'BASH'
apt-get update
apt-get install -y --no-install-recommends \
    bash git wget curl ca-certificates unzip libarchive-tools \
    python3 python3-pip python3-venv python3-setuptools python3-serial \
    python3-click python3-cryptography python3-future python3-pyparsing \
    python3-pyelftools \
    gcc g++ make cmake ninja-build \
    libncurses-dev flex bison gperf \
    clang-format srecord ccache libffi-dev libssl-dev \
    ruby
# Install the 32-bit libraries required for building unit-tests for 'native_sim' target
# Otherwise, the build will fail with the error "/usr/include/stdint.h:26:10: fatal error: bits/libc-header-start.h: No such file or directory"
apt-get install -y --no-install-recommends gcc-multilib libc6-dev-i386
apt-get install -y --no-install-recommends locales
locale-gen de_DE.UTF-8
apt-get clean
rm -rf /var/lib/apt/lists/*

# Workaround for the problem on Ububtu_22.04: "ImportError: libffi.so.7: cannot open shared object file: No such file or directory"
wget -O /tmp/libffi7_3.3-4_amd64.deb \
    http://archive.ubuntu.com/ubuntu/pool/main/libf/libffi/libffi7_3.3-4_amd64.deb
dpkg -i /tmp/libffi7_3.3-4_amd64.deb
rm -f /tmp/libffi7_3.3-4_amd64.deb

YQ_VERSION=v4.46.1
curl -fL --retry 3 --retry-delay 3 \
  "https://github.com/mikefarah/yq/releases/download/${YQ_VERSION}/yq_linux_amd64" \
  -o /usr/local/bin/yq
chmod +x /usr/local/bin/yq

wget -q --retry-connrefused --waitretry=2 --read-timeout=20 --timeout=15 -t 3 \
  -O /usr/local/bin/nrfutil \
  "https://files.nordicsemi.com/ui/api/v1/download?repoKey=swtools&path=external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil&isNativeBrowsing=false"
chmod +x /usr/local/bin/nrfutil

BASH

# --------------------------------------------------------------------
# Create script: /usr/local/bin/docker-entrypoint.sh
# --------------------------------------------------------------------
RUN bash -euo pipefail <<'BASH'
set -xeuo pipefail

# ------ Create script: /usr/local/bin/docker-entrypoint.sh ------
cat >/usr/local/bin/docker-entrypoint.sh <<'EOSH'
#!/usr/bin/env bash
set -xeuo pipefail

HOST_UID=${HOST_UID:-1000}
HOST_GID=${HOST_GID:-1000}
HOST_USER=${HOST_USER:-runner}
HOST_GROUP=${HOST_GROUP:-runner}

is_writable() { test -w "$1" 2>/dev/null; }

if [ "${EUID}" -ne 0 ]; then
  # --- Non-root path: cannot modify /etc/passwd or /etc/group ---
  current_uid="$(id -u)"
  current_gid="$(id -g)"
  current_user="$(id -un || echo "user${current_uid}")"
  current_group="$(id -gn || echo "group${current_gid}")"

  export USER="${HOST_USER}"

  # Ensure HOME points to a writable place
  if [ -z "${HOME:-}" ] || ! is_writable "${HOME}"; then
    mkdir -p "${HOME}" 2>/dev/null || true
    chown -R ${current_uid}:${current_gid} "${HOME}" 2>/dev/null || true
    ls -la $HOME
  fi

  # If working dir isn’t writable (e.g. -w set to non-existent or /__w), switch to $HOME
  if ! is_writable "."; then
    echo "warning: working directory '$(pwd)' is not writable; switching to HOME='${HOME}'" >&2
    cd "${HOME}" 2>/dev/null || cd /tmp || true
  fi

  exec "$@"
fi

# --- Root path: we can ensure/rename the target account ---

# --------------------------
# Ensure/rename GROUP (GID)
# --------------------------
if getent group "${HOST_GID}" >/dev/null; then
  current_group="$(getent group "${HOST_GID}" | cut -d: -f1)"
  if [ "${current_group}" != "${HOST_GROUP}" ]; then
    # If desired name already exists (with another GID), keep the existing name
    if getent group "${HOST_GROUP}" >/dev/null; then
      HOST_GROUP="${current_group}"
    else
      groupmod -n "${HOST_GROUP}" "${current_group}"
    fi
  fi
else
  # No group with HOST_GID yet
  if getent group "${HOST_GROUP}" >/dev/null; then
    # Name taken by different GID → create a neutral name bound to HOST_GID
    groupadd -g "${HOST_GID}" "grp${HOST_GID}"
    HOST_GROUP="grp${HOST_GID}"
  else
    groupadd -g "${HOST_GID}" "${HOST_GROUP}"
  fi
fi

# ------------------------
# Ensure/rename USER (UID)
# ------------------------
if getent passwd "${HOST_UID}" >/dev/null; then
  current_user="$(getent passwd "${HOST_UID}" | cut -d: -f1)"
  old_home="$(getent passwd "${HOST_UID}" | cut -d: -f6)"
else
  current_user=""  # none with HOST_UID yet
fi

new_home="/home/${HOST_USER}"

if [ -n "${current_user}" ] && [ "${current_user}" != "${HOST_USER}" ]; then
  # We must rename current_user -> HOST_USER
  # If Docker pre-created -w /home/${HOST_USER}, migrate files first and then rename without -m.
  if [ -d "${new_home}" ]; then
    # Migrate contents from old_home to new_home (if different)
    if [ -n "${old_home}" ] && [ "${old_home}" != "${new_home}" ] && [ -d "${old_home}" ]; then
      # If new_home is empty, try a fast move of contents; otherwise, copy preserving attrs.
      if [ -z "$(ls -A "${new_home}")" ]; then
        # Move dotfiles too
        shopt -s dotglob nullglob
        mv -f "${old_home}"/* "${new_home}/" 2>/dev/null || true
        shopt -u dotglob nullglob
      else
        # Fallback copy preserving perms/mtime without extra deps
        tar -C "${old_home}" -cf - . | tar -C "${new_home}" -xf -
      fi
      # Best-effort clean old home if now empty
      rmdir "${old_home}" 2>/dev/null || true
    fi

    # Now rename user, pointing to existing new_home (no -m)
    usermod -l "${HOST_USER}" -d "${new_home}" "${current_user}"
  else
    # Target home does not exist yet; let usermod move it
    usermod -l "${HOST_USER}" -d "${new_home}" -m "${current_user}"
  fi
elif [ -z "${current_user}" ]; then
  # No user with HOST_UID yet; create or adjust by name
  if getent passwd "${HOST_USER}" >/dev/null; then
    # Name exists with different UID → set UID/GID
    usermod -u "${HOST_UID}" -g "${HOST_GID}" "${HOST_USER}"
    new_home="$(getent passwd "${HOST_USER}" | cut -d: -f6)"
  else
    useradd -m -u "${HOST_UID}" -g "${HOST_GID}" -s /bin/bash "${HOST_USER}"
    new_home="/home/${HOST_USER}"
  fi
fi

# -----------------------------------------
# Handy supplemental groups (if they exist)
# -----------------------------------------
for g in dialout plugdev video; do
  if getent group "$g" >/dev/null; then
    usermod -aG "$g" "${HOST_USER}" || true
  fi
done

# ----------------------------
# HOME/ownership & environment
# ----------------------------
export USER="${HOST_USER}"
export HOME="${new_home}"
mkdir -p "${HOME}" || true
chown -R "${HOST_UID}:${HOST_GID}" "${HOME}" || true

# If current PWD was under the old home, relocate to the equivalent path under the new home
if [ -n "${old_home:-}" ] && [ "${old_home}" != "${new_home}" ]; then
  case "$(pwd -P 2>/dev/null || true)" in
    "${old_home}"|${old_home}/*)
      suffix="${PWD#${old_home}}"
      target="${new_home}${suffix}"
      if [ -d "${target}" ] || [ -f "${target}" ]; then
        cd "${target}" 2>/dev/null || cd "${new_home}" || true
      else
        cd "${new_home}" 2>/dev/null || true
      fi
      ;;
  esac
fi

# ----------------------------
# Drop to the requested UID/GID
# (no extra deps; util-linux setpriv)
# ----------------------------
exec setpriv --reuid="${HOST_UID}" --regid="${HOST_GID}" --init-groups "$@"
EOSH

chmod +x /usr/local/bin/docker-entrypoint.sh
echo "Created /usr/local/bin/docker-entrypoint.sh"
BASH


# --------------------------------------------------------------------
# Create the installer script: /usr/local/bin/install_ncs_v2.8.0.sh
# This script installs nrfutil + Toolchain Manager, then installs NCS
# for $NCS_VERSION and runs west init/update/zephyr-export.
# It is idempotent and safe to re-run.
# --------------------------------------------------------------------
RUN bash -euo pipefail <<'BASH'
set -euo pipefail

# ------ Create installer script: /usr/local/bin/install_ncs_v2.8.0.sh ------
cat >/usr/local/bin/install_ncs_v2.8.0.sh <<'EOSH'
#!/usr/bin/env bash
set -xeuo pipefail

# Default and sanity
: "${NCS_VERSION:=v2.8.0}"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH-}"

echo ">>> NCS_VERSION=${NCS_VERSION}"

# 1) Install Toolchain Manager and nrf5sdk tools if missing
if ! nrfutil list | grep -q toolchain-manager || ! nrfutil toolchain-manager --help >/dev/null 2>&1; then
  echo ">>> Installing Toolchain Manager..."
  nrfutil install toolchain-manager
else
  echo ">>> Toolchain Manager already installed"
fi
if ! nrfutil list | grep -q nrf5sdk-tools || ! nrfutil nrf5sdk-tools --help >/dev/null 2>&1; then
  echo ">>> Installing nrf5sdk-tools..."
  nrfutil install nrf5sdk-tools
else
  echo ">>> nrf5sdk-tools already installed"
fi

# 2) Install the NCS toolchain for this version if not present
mkdir -p $HOME/ncs
if [ ! -d "$HOME/ncs/toolchains" ] || [ -z "$(ls -A $HOME/ncs/toolchains 2>/dev/null || true)" ]; then
  echo ">>> Installing NCS toolchain ${NCS_VERSION} ..."
  nrfutil toolchain-manager install --ncs-version "${NCS_VERSION}"
  # Workaround libpsl/libunistring mismatch on Ubuntu 24.04
  rm -f $HOME/ncs/toolchains/*/usr/lib/x86_64-linux-gnu/libpsl.so.5* || true
else
  echo ">>> NCS toolchain seems present at $HOME/ncs/${NCS_VERSION}"
fi

# 3) Export toolchain env (gives west, cmake, python toolchain, etc.)
echo ">>> Activating toolchain env..."
eval "$(nrfutil toolchain-manager env --ncs-version ${NCS_VERSION} --as-script)"

# 4) Initialize and update the west workspace (idempotent)
cd $HOME/ncs
if [ ! -d "$HOME/ncs/${NCS_VERSION}/.west" ]; then
  echo ">>> west init ${NCS_VERSION}"
  west init -m https://github.com/nrfconnect/sdk-nrf --mr "${NCS_VERSION}" "${NCS_VERSION}"
  echo ">>> west update"
  cd "$HOME/ncs/${NCS_VERSION}" && west update
else
  echo ">>> west workspace already initialized at $HOME/ncs/${NCS_VERSION}"
fi

echo ">>> zephyr-export"
cd "$HOME/ncs/${NCS_VERSION}" && nrfutil toolchain-manager launch west -- zephyr-export

cd $HOME

echo ">>> Install python ecdsa package"
eval "$(nrfutil toolchain-manager env --ncs-version $NCS_VERSION --as-script)"
source $HOME/ncs/$NCS_VERSION/zephyr/zephyr-env.sh
if ! python3 -m pip show ecdsa > /dev/null 2>&1; then
  python3 -m pip install --upgrade pip
  python3 -m pip install ecdsa
else
    echo "ecdsa is already installed"
fi

echo ">>> Done. Zephyr base should be at: /root/ncs/${NCS_VERSION}/zephyr"
EOSH

chmod +x /usr/local/bin/install_ncs_v2.8.0.sh
echo "Created /usr/local/bin/install_ncs_v2.8.0.sh"

# ------ Create dev-env script: /usr/local/bin/dev-env-v2.8.0.sh ------
cat >/usr/local/bin/dev-env-v2.8.0.sh <<'EOSH'
#!/usr/bin/env bash
# dev-env-v2.8.0.sh — set up Zephyr + nRF toolchain
# exec: source dev-env-v2.8.0.sh

set -xeuo pipefail

echo ">>> Activating toolchain env..."
eval "$(nrfutil toolchain-manager env --ncs-version ${NCS_VERSION} --as-script)"
echo ">>> zephyr-export"
cd "$HOME/ncs/${NCS_VERSION}" && nrfutil toolchain-manager launch west -- zephyr-export
cd $HOME
echo ">>> Sourcing Zephyr env..."
source "$HOME/ncs/${NCS_VERSION}/zephyr/zephyr-env.sh"
echo ">>> Done. Zephyr base should be at: $HOME/ncs/${NCS_VERSION}/zephyr"

EOSH

chmod +x /usr/local/bin/dev-env-v2.8.0.sh
echo "Created /usr/local/bin/dev-env-v2.8.0.sh"

BASH

# Optional: set a neutral WORKDIR for Actions
WORKDIR /__w

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["bash"]
