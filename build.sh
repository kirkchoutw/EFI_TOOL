#!/usr/bin/env bash
# Build BdtPkg inside an existing edk2 workspace.
#
#   ./build.sh [X64|IA32] [DEBUG|RELEASE|NOOPT] [extra edk2 build args...]
#
# Environment:
#   EDK2_DIR   edk2 checkout with BaseTools already built (default: ~/edk2)
#   TOOLCHAIN  edk2 tool chain tag (default: GCC)

TOOL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2_DIR="${EDK2_DIR:-$HOME/edk2}"
TOOLCHAIN="${TOOLCHAIN:-GCC}"
ARCH="${1:-X64}"
TARGET="${2:-DEBUG}"
EXTRA_ARGS=("${@:3}")

export WORKSPACE="$EDK2_DIR"
export PACKAGES_PATH="$EDK2_DIR:$TOOL_DIR"

cd "$EDK2_DIR" || exit 1
set --
source edksetup.sh > /dev/null || exit 1

build -p BdtPkg/BdtPkg.dsc -a "$ARCH" -b "$TARGET" -t "$TOOLCHAIN" "${EXTRA_ARGS[@]}" || exit 1
echo "Output: $WORKSPACE/Build/BdtPkg/${TARGET}_${TOOLCHAIN}/$ARCH/Bdt.efi"
