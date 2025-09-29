#!/bin/bash

# Build script for the Godot Mono Decompiler AOT Test (Static Linking)

set -e

echo "Building Godot Mono Decompiler AOT Test (Static Linking)..."
echo "=========================================================="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run this script from the godot-mono-decomp-aot-test directory."
    exit 1
fi

# Check if the header file exists
if [ ! -f "godot_mono_decomp.h" ]; then
    echo "Error: godot_mono_decomp.h not found."
    exit 1
fi

# Check if the AOT library exists
AOT_LIB_PATH="../godot-mono-decomp/bin/Release/net9.0/osx-arm64/publish/godot-mono-decomp.dylib"
if [ ! -f "$AOT_LIB_PATH" ]; then
    echo "Error: AOT library not found at $AOT_LIB_PATH"
    echo "Please build the AOT library first."
    exit 1
fi

echo "Found AOT library at: $AOT_LIB_PATH"
echo "Using static linking with NativeAOT library"

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake ..

# Build the project
echo "Building the test program..."
make

echo "Build completed successfully!"
echo "============================"
echo "To run the test:"
echo "  ./aot-test <assembly_path> <output_directory> [<reference_paths>...]"
echo ""
echo "Example:"
echo "  ./aot-test /path/to/test.dll ./test_output"
echo "  ./aot-test /path/to/test.dll ./test_output ./reference1 ./reference2" 