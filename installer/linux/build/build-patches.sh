#!/usr/bin/env bash
#
# Applies the patches that the build needs on top of the fork of vcpkg.
#
# The fork of vcpkg lives in another repository, and a change to a port of it cannot be
# committed there from here. The patch is kept in this repository instead, and this script
# puts it on the port. A port that already carries the change is left alone, so running the
# script twice is harmless.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
readonly PATCH_DIR="${REPO_ROOT}/patches"
readonly VCPKG_DIR="${REPO_ROOT}/vcpkg4aspia"

apply_patch()
{
    local name="$1"
    local file="$2"
    local patch="${PATCH_DIR}/${name}"

    if [[ ! -f "${patch}" ]]; then
        echo "No patch: ${patch}"
        return 0
    fi

    if [[ ! -f "${file}" ]]; then
        echo "No such file: ${file}"
        return 1
    fi

    if patch --dry-run -p1 -d "$(dirname "${file}")" < "${patch}" > /dev/null 2>&1; then
        echo "Applying ${name}."
        patch -p1 -d "$(dirname "${file}")" < "${patch}"
    else
        echo "Already applied: ${name}"
    fi
}

apply_patch "vcpkg4aspia-qtbase-gui-feature.patch" "${VCPKG_DIR}/ports/qtbase/portfile.cmake"
