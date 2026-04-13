#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== Regatoni: Bootstrapping fresh system ==="

# Detect distro
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO="$ID"
else
    echo "Warning: Cannot detect distro. Assuming Debian/Ubuntu."
    DISTRO="ubuntu"
fi

if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" ]]; then
    echo "[1/4] Updating package lists..."
    sudo apt-get update -qq

    echo "[2/4] Installing build essentials..."
    sudo apt-get install -y -qq \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        wget \
        pkg-config \
        zip \
        unzip

    echo "[3/4] Installing LLVM/Clang build dependencies..."
    sudo apt-get install -y -qq \
        clang \
        lld \
        llvm \
        llvm-dev \
        libclang-dev \
        libz-dev \
        zlib1g-dev \
        libtinfo-dev \
        libxml2-dev \
        libncurses5-dev

    echo "[4/4] Installing other useful tools..."
    sudo apt-get install -y -qq \
        python3 \
        python3-pip \
        python3-venv \
        ripgrep \
        jq \
        ccache

elif [[ "$DISTRO" == "alpine" ]]; then
    echo "Alpine detected..."
    sudo apk update
    sudo apk add \
        build-base cmake ninja git curl wget \
        clang llvm-dev lld \
        zlib-dev ncurses-dev libxml2-dev \
        python3 py3-pip ripgrep jq ccache \
        libgcc libstdc++
else
    echo "Unsupported distro: $DISTRO"
    echo "Install manually: cmake, ninja, git, clang, llvm-dev, zlib, python3"
    exit 1
fi

# Verify key tools
echo ""
echo "=== Verifying installations ==="
for tool in cmake ninja git clang++ llvm-config python3; do
    if command -v "$tool" &>/dev/null; then
        echo "  $tool ($(command -v "$tool"))"
    else
        echo "  $tool NOT FOUND"
    fi
done

echo ""
echo "=== Bootstrap complete ==="
echo "Next: ./scripts/build/setup-deps.sh"