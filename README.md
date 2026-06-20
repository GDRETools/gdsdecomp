# Godot RE Tools

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

## Installation

Grab the latest release version from here: https://github.com/GDRETools/gdsdecomp/releases

On Windows, you can also install it from [Scoop](https://scoop.sh):

```
scoop bucket add games
scoop install gdsdecomp
```

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
```
Main commands:
--recover=<GAME_PCK/EXE/APK/DIR>              Perform full project recovery on the specified PCK, APK, EXE, or extracted project directory.
--extract=<GAME_PCK/EXE/APK>                  Extract the specified PCK, APK, or EXE.
--list-files=<GAME_PCK/EXE/APK>               List all files in the specified PCK, APK, or EXE and exit (can be repeated)
--compile=<GD_FILE>                           Compile GDScript files to bytecode (can be repeated and use globs, requires --bytecode)
--decompile=<GDC_FILE>                        Decompile GDC files to text (can be repeated and use globs)
--pck-create=<PCK_DIR>                        Create a PCK file from the specified directory (requires --pck-version and --pck-engine-version)
--pck-patch=<GAME_PCK/EXE>                    Patch a PCK file with the specified files
--list-bytecode-versions                      List all available bytecode versions
--dump-bytecode-versions=<DIR>                Dump all available bytecode definitions to the specified directory in JSON format
--txt-to-bin=<FILE>                           Convert text-based scene or resource files to binary format (can be repeated)
--bin-to-txt=<FILE>                           Convert binary scene or resource files to text-based format (can be repeated)
--patch-translations=<CSV_FILE>=<SRC_PATH>    Patch translations with the specified CSV file and source path
                                                 (e.g. "/path/to/translation.csv=res://translations/translation.csv") (can be repeated)
--godot-version                               Print the version of Godot engine and exit
--godot-help                                  Print the help message of Godot engine and exit
--help, --gdre-help                           Print this help message and exit
--version, --gdre-version                     Print this version of GDRE tools and exit

Recover/Extract Options:

--key=<KEY>                          The Key to use if project is encrypted as a 64-character hex string,
                                         e.g.: '000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F'
--output=<DIR>                       Output directory, defaults to <NAME_extracted>, or the project directory if one of specified
--scripts-only                       Only extract/recover scripts
--include=<GLOB>                     Include files matching the glob pattern (can be repeated, see notes below)
--exclude=<GLOB>                     Exclude files matching the glob pattern (can be repeated, see notes below)
--ignore-checksum-errors             Ignore MD5 checksum errors when extracting/recovering
--skip-checksum-check                Skip MD5 checksum check when extracting/recovering
--csharp-assembly=<PATH>             Optional path to the C# assembly for C# projects; auto-detected from PCK path if not specified
--force-bytecode-version=<VERSION>   Force the bytecode version to be the specified value. Can be either a commit hash (e.g. 'f3f05dc') or version string (e.g. '4.3.0')
--load-custom-bytecode=<JSON_FILE>   Load a custom bytecode definition file from the specified JSON file and use it for the recovery session
--translation-hint=<FILE>            Load a translation key hint file (.csv, .txt, .po, .mo) and use it during translation recovery
--skip-loading-resource-strings      Skip loading resource strings from all resources during translation recovery

Decompile/Compile Options:
--bytecode=<COMMIT_OR_VERSION>          Either the commit hash of the bytecode revision (e.g. 'f3f05dc'), or the version of the engine (e.g. '4.3.0')
--load-custom-bytecode=<JSON_FILE>      Load a custom bytecode definition file from the specified JSON file and use it for the session
--output=<DIR>                          Directory where compiled files will be output to.
                                          - If not specified, compiled files will be output to the same location
                                          (e.g. '<PROJ_DIR>/main.gd' -> '<PROJ_DIR>/main.gdc')

Create PCK Options:
--output=<OUTPUT_PCK/EXE>                The output PCK file to create
--pck-version=<VERSION>                  The format version of the PCK file to create (0, 1, 2)
--pck-engine-version=<ENGINE_VERSION>    The version of the engine to create the PCK for (x.y.z)
--embed=<EXE_TO_EMBED>                   The executable to embed the PCK into
--key=<KEY>                              64-character hex string to encrypt the PCK with

Patch PCK Options:
--output=<OUTPUT_PCK/EXE>                The output PCK file to create
--patch-file=<SRC_FILE>=<DEST_FILE>      The file to patch the PCK with (e.g. "/path/to/file.gd=res://file.gd") (can be repeated)
--include=<GLOB>                         Only include files from original PCK matching the glob pattern (can be repeated)
--exclude=<GLOB>                         Exclude files from original PCK matching the glob pattern (can be repeated)
--embed=<EXE_TO_EMBED>                   The executable to embed the patched PCK into
--key=<KEY>                              64-character hex string to decrypt/encrypt the PCK with

Patch Translations Options:
(Note: This can be used in combination with --pck-patch and its options)
--pck=<GAME_PCK>                        The PCK file with the source translations (if used in combination with --pck-patch, this can be omitted)
--output=<OUTPUT_DIR>                   The output directory to save the patched translations to (optional if used in combination with --pck-patch)
--locales=<LOCALES>                     The locales to patch (comma-separated list, defaults to only newly-added locales)
```

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

- 2.x models (`dae`, `fbx`, `glb`, etc.)
- GDNative or GDExtension scripts

## Compiling from source

Clone this repository into Godot's `modules` subfolder as `gdsdecomp`.
Rebuild Godot engine as described in https://docs.godotengine.org/en/latest/development/compiling/index.html.

You will also need [rustup](https://rustup.rs) and [dotnet 10 sdk](https://dotnet.microsoft.com/en-us/download/dotnet/10.0).

For ease of bootstrapping development, we have included launch, build, and settings templates for vscode in the .vscode directory. Once you have read the instructions for compiling Godot above and set up your build environment: put these in the .vscode folder in the Godot directory (not gdsdecomp), remove the ".template" from each, and launch vscode from the Godot directory.

Make sure to build the editor build first, and to launch the editor to edit the project in the `standalone` directory at least once so that resources are imported before running.

### Note:

During SCons configure, the module auto-applies the patches under `modules/gdsdecomp/patches/` to Godot core files (currently just `main/main.cpp`, to hook `gdre::modify_cli_args` into CLI parsing). The application is idempotent: re-running `scons` is a no-op once patched. If you remove the module or want to restore the originals, revert the affected files manually with `git checkout -- main/main.cpp`.

### Requirements

[Our fork of godot](https://github.com/nikitalita/godot) @ branch `gdre-wb-f964fa714f5`

- Support for building on 3.x has been dropped and no new features are being pushed
  - Godot RE Tools still retains the ability to decompile 3.x and 2.x projects, however.

### Standalone

Assuming you compiled with `scons platform=linuxbsd target=template_debug`,

```bash
$ bin/godot.linuxbsd.template_debug.x86_64.llvm --headless --path=modules/gdsdecomp/standalone --recover=<pck/apk/exe>
```

## License

The source code of the module is licensed under MIT license.
