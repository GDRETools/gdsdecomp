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
- If the project is encrypted, select the "Set encryption key..." menu option first first and enter the key.
- In the file dialog, select the apk, pck, or embedded exe you want to recover:

![File dialog](images/file_dialog.png)

- In the PCK explorer dialog, select "Full Recovery" or "Extract Only", then select the directory you want to recover the project to, then click "Extract...":

![Recovery dialog screenshot](images/recovery_dialog.png)

- After it finishes, it will pop up a recovery box telling you the location of the log file, what editor version you should use when editing the recovered project, report any non-recovered assets:

![Recovery log](images/recovery_log.png)

### Command Line

#### Example:

```bash
gdre_tools --headless --recover=game.pck
```

#### Main arguments:

- `--recover=<pck/exe/apk>` : Perform full project recovery on the specified PCK/EXE/APK.
- `--extract=<pck/exe/apk>` : Perform extraction only (no resource conversion) on the specified PCK/EXE/APK
- `--compile=<GD_FILE>` : Compile GDScript files to bytecode. Can be repeated and use globs (e.g. `scripts/**/*.gd`). Requires `--bytecode`.
- `--list-bytecode-versions`: List all available bytecode versions

#### Optional arguments:

- `--output-dir=<out_dir>` : Output directory, defaults to `<NAME>_extracted`, or the project directory if one is specified
- `--key=<key>` : The Key to use if PAK/EXE/APK is encrypted (64 character hex string)
- `--ignore-checksum-errors` : Ignore MD5 errors during PCK checking and perform recovery/extraction anyway
- `--scripts-only` : Only extract/recover scripts
- `--bytecode=<COMMIT_OR_VERSION>`: Required when using `--compile`. Takes in either the commit hash of the bytecode revision (e.g. 'f3f05dc') or the version of the engine (e.g. '4.3.0')

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

Godot 4.0 (master branch) @ 4c4e67334412f73c9deba5e5d29afa8651418af2

- Support for building on 3.x has been dropped and no new features are being pushed
  - Godot RE Tools still retains the ability to decompile 3.x and 2.x projects, however.

### Standalone

Assuming you compiled with `scons platform=linuxbsd target=template_debug`,

```bash
$ bin/godot.linuxbsd.template_debug.x86_64.llvm --headless --path=modules/gdsdecomp/standalone --recover=<pck/apk/exe>
```

## License

The source code of the module is licensed under MIT license.
