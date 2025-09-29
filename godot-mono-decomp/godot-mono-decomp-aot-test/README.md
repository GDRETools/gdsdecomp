# Godot Mono Decompiler AOT Test (Dynamic Linking)

This directory contains a C++ test program to verify that the AOT (Ahead-of-Time) build of the Godot Mono Decompiler is working correctly with dynamic linking.

## Prerequisites

- CMake 3.16 or higher
- C++17 compatible compiler (GCC, Clang, or MSVC)
- The AOT library must be built and available at `../GodotMonoDecompNativeAOT/bin/Release/net9.0/osx-arm64/publish/GodotMonoDecompNativeAOT.{dylib,dll,so}`

## Building the Test

1. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```

2. Configure with CMake:
   ```bash
   cmake ..
   ```

3. Build the test program:
   ```bash
   make
   ```

## Running the Test

The test program requires two arguments:
1. Path to a .NET assembly file to decompile
2. Output directory for the decompiled project
3. Optional: Additional reference paths for assembly resolution

Example usage:
```bash
./aot-test /path/to/your/assembly.dll ./output
./aot-test /path/to/your/assembly.dll ./output ./reference1 ./reference2
```

## What the Test Does

1. **Dynamic linking**: The program is linked directly with the NativeAOT library at build time
2. **Direct function calls**: Calls the `GodotMonoDecomp_DecompileProject` function directly without dynamic loading
3. **Decompilation**: Performs the actual decompilation of the provided assembly file
4. **Verification**: Checks that the decompilation was successful and lists created output files

## Expected Output

On success, you should see output similar to:
```
Godot Mono Decompiler AOT Test (Dynamic Linking)
================================================
Assembly Path: /path/to/your/assembly.dll
Output Directory: ./output
Output Project File: ./output/assembly.csproj
Reference Paths: 0
Calling dynamically linked AOT decompile function...
SUCCESS: Decompilation completed successfully!
Output project file created: ./output/assembly.csproj
Files in output directory:
  - assembly.csproj
  - [other decompiled files...]
Test completed.
```

## Troubleshooting

- **Linker errors**: Ensure the AOT library is built and the path in `CMakeLists.txt` is correct
- **Missing symbols**: Verify that the `GodotMonoDecomp_DecompileProject` function is properly exported from the AOT library
- **Runtime errors**: Check that all dependencies are available in the RPATH

## Notes

- The test program uses dynamic linking with the NativeAOT library
- The AOT library is still copied to the build directory in case there are remaining dynamic dependencies
- The header file `godot_mono_decomp.h` contains the function declarations for the C interface 