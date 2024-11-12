# Godot RE Tools

_This software in an alpha stage. Please report any bugs on the github page._

## Introduction

![Code Screenshot](images/screenshot.png)

This module includes following features:

- Full project recovery
- PCK archive extractor / creator.
- GDScript batch decompiler.
- Resource text <-> binary batch converter.

Full project recovery performs the following:

- Loads project resources from an APK, PCK, or embedded EXE file
- Decompiles all GDScript scripts
- Recovers the original project file
- Converts all imported resources back to their original import formats
- Converts any auto-converted binary resources back to their original text formats
- Recreates any plugin configuration files

This module has support for decompiling Godot 4.x, 3.x, and 2.x projects.

Grab the latest release version from here: https://github.com/bruvzg/gdsdecomp/releases

## Usage

### GUI

- To perform full project recovery from the GUI, select "Recover project..." from the "RE Tools" menu:
  ![Menu screenshot](images/recovery_gui.png)
- Or, just drag and drop the PCK/EXE onto the application window.

### Command Line

#### Usage:

```bash
gdre_tools --headless <main_command> [options]
```

#### Main commands:

- `--recover=<GAME_PCK/EXE/APK/DIR>` : Perform full project recovery on the specified PCK, APK, EXE, or extracted project directory.
- `--extract=<GAME_PCK/EXE/APK>` : Extract the specified PCK, APK, or EXE.
- `--compile=<GD_FILE>` : Compile GDScript files to bytecode (can be repeated and use globs, requires --bytecode)
- `--list-bytecode-versions` : List all available bytecode versions

#### Recover/Extract Options:

- `--key=<KEY>` : The Key to use if project is encrypted as a 64-character hex string, e.g.: '000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F'
- `--output-dir=<DIR>` : Output directory, defaults to <NAME_extracted>, or the project directory if one of specified
- `--scripts-only` : Only extract/recover scripts
- `--include=<GLOB>` : Include files matching the glob pattern (can be repeated)
- `--exclude=<GLOB>` : Exclude files matching the glob pattern (can be repeated)
- `--ignore-checksum-errors` : Ignore MD5 checksum errors when extracting/recovering

#### Compile Options:

- `--bytecode=<COMMIT_OR_VERSION>` : Either the commit hash of the bytecode revision (e.g. 'f3f05dc'), or the version of the engine (e.g. '4.3.0')
- `--output-dir=<DIR>` : Directory where compiled files will be output to. If not specified, compiled files will be output to the same location (e.g. '<PROJ_DIR>/main.gd' -> '<PROJ_DIR>/main.gdc')

#### Notes on Include/Exclude globs:

- Recursive patterns can be specified with `**`
  - Example: `res://**/*.gdc` matches `res://main.gdc`, `res://scripts/script.gdc`, etc.
- Globs should be rooted to `res://` or `user://`
  - Example: `res://*.gdc` will match all .gdc files in the root of the project, but not any of the subdirectories.
- If not rooted, globs will be rooted to `res://`
  - Example: `addons/plugin/main.gdc` is equivalent to `res://addons/plugin/main.gdc`
- As a special case, if the glob has a wildcard and does not contain a directory, it will be assumed to be a recursive pattern.
  - Example: `*.gdc` would be equivalent to `res://**/*.gdc`
- Include/Exclude globs will only match files that are actually in the project PCK/dir, not any non-present resource source files.
  - Example:
    - A project contains the file `res://main.gdc`. `res://main.gd` is the source file of `res://main.gdc`, but is not included in the project PCK.
      - Performing project recovery with the include glob `res://main.gd` would not recover `res://main.gd`.
      - Performing project recovery with the include glob `res://main.gdc` would recover `res://main.gd`

Use the same Godot tools version that the original game was compiled in to edit the project; the recovery log will state what version was detected.

## Limitations

Support has yet to be implemented for converting the following resources:

- 3.x and 2.x models (`dae`, `fbx`, `glb`, etc.)
- OBJ meshes
- Bitmap and image fonts (recovering 4.x TTF/OTF fontfiles is supported)
- GDNative or GDExtension scripts (e.g. GDMono and CSharp)

Support for converting certain resources is limited:

- Recovered .csv translation files will likely have missing keys; this is due to `.translation` files only storing the hashes of the keys. It is recommended to just politely ask the developer if you want to add additional translations.

There is no support for decompiling any GDNative/GDExtension or GDMono scripts. For Mono/CSharp, you can use [Ilspy](https://github.com/icsharpcode/ILSpy) or dotPeek.

## Compiling from source

Clone this repository into Godot's `modules` subfolder as `gdsdecomp`.
Rebuild Godot engine as described in https://docs.godotengine.org/en/latest/development/compiling/index.html.

For ease of bootstrapping development, we have included launch, build, and settings templates for vscode in the .vscode directory. Once you have read the instructions for compiling Godot above and set up your build environment: put these in the .vscode folder in the Godot directory (not gdsdecomp), remove the ".template" from each, and launch vscode from the Godot directory.

### Requirements

Godot 4.0 (master branch) @ ec6a1c0e792ac8be44990749800a4654a293b9ee



- Support for building on 3.x has been dropped and no new features are being pushed
  - Godot RE Tools still retains the ability to decompile 3.x and 2.x projects, however.

### Standalone

Assuming you compiled with `scons platform=linuxbsd target=template_debug`,

```bash
$ bin/godot.linuxbsd.template_debug.x86_64.llvm --headless --path=modules/gdsdecomp/standalone --recover=<pck/apk/exe>
```

## License

The source code of the module is licensed under MIT license.
