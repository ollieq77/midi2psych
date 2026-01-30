#!/bin/bash

echo "========================================"
echo "  MIDI2Psych Builder for Linux/Mac"
echo "  Using GCC/Clang"
echo "========================================"
echo ""

# Check if g++ or clang++ is available
if command -v g++ &> /dev/null; then
    CXX=g++
    echo "Using g++ compiler"
elif command -v clang++ &> /dev/null; then
    CXX=clang++
    echo "Using clang++ compiler"
else
    echo "ERROR: No C++ compiler found!"
    echo ""
    echo "Please install g++ or clang++:"
    echo "  Ubuntu/Debian: sudo apt install g++"
    echo "  Fedora/RHEL:   sudo dnf install gcc-c++"
    echo "  macOS:         xcode-select --install"
    echo ""
    exit 1
fi

$CXX --version | head -n 1
echo ""

echo "Building optimized executable..."
echo ""

# Resolve script path and repo root so script runs from any cwd
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."
OUT_DIR="$REPO_ROOT/dist"

mkdir -p "$OUT_DIR"

# Build with maximum optimization (source is in repo root)
$CXX -std=c++17 \
    -O3 \
    -march=native \
    -flto \
    -s \
    -DNDEBUG \
    -o "$OUT_DIR/midi2psych" \
    "$REPO_ROOT/midi2psych.cpp"

if [ $? -eq 0 ]; then
    echo ""
    echo "========================================"
    echo "  BUILD SUCCESSFUL!"
    echo "========================================"
    echo ""
    echo "Executable created: $OUT_DIR/midi2psych"
    echo "File size: $(ls -lh "$OUT_DIR/midi2psych" | awk '{print $5}')"
    echo ""
    echo "Make it executable:"
    echo "  chmod +x $OUT_DIR/midi2psych"
    echo ""
    echo "Test it with:"
    echo "  $OUT_DIR/midi2psych -h"
    echo ""
    
    # Make executable
    chmod +x "$OUT_DIR/midi2psych"
else
    echo ""
    echo "========================================"
    echo "  BUILD FAILED!"
    echo "========================================"
    exit 1
fi
