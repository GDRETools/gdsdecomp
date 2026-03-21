import json
import os
import shutil
from subprocess import STDOUT, check_output

from .common import get_cmd_env, is_dev_build


def get_godot_mono_decomp_libs(static_lib, build_env, libs):
    lib_prefix = "" if build_env.msvc else "lib"
    if static_lib:
        lib_suffix = ".lib" if build_env.msvc else ".a"
    else:
        if build_env["platform"] == "macos":
            lib_suffix = ".dylib"
        elif build_env.msvc:
            lib_suffix = ".lib"
        else:
            lib_suffix = ".so"
    return [lib_prefix + lib + lib_suffix for lib in libs]


def get_godot_mono_triplet(target_platform, target_arch):
    if target_platform == "macos":
        platform_part = "osx"
        arch_part = "arm64" if target_arch == "arm64" else "x64"
    elif target_platform == "linuxbsd":
        platform_part = "linux"
        arch_part = "arm64" if target_arch == "arm64" else "x64"
    elif target_platform == "windows":
        platform_part = "win"
        arch_part = "arm64" if target_arch == "arm64" else "x64"
    elif target_platform == "android":
        platform_part = "linux-bionic"
        arch_part = "arm64" if target_arch == "arm64" else "x64"
    elif target_platform == "web":
        platform_part = "browser"
        arch_part = "wasm"
    else:
        raise Exception(f"Unsupported platform: {target_platform}")
    return f"{platform_part}-{arch_part}"


def get_dotnet_variant_name(dev_build):
    return "Debug" if dev_build else "Release"


def get_godot_mono_decomp_lib_dir(godot_mono_decomp_dir, target_platform, target_arch, dev_build):
    triplet = get_godot_mono_triplet(target_platform, target_arch)
    target_framework = "net9.0"
    csproj_path = os.path.join(godot_mono_decomp_dir, "GodotMonoDecompNativeAOT.csproj")
    with open(csproj_path, "r") as csproj_file:
        for line in csproj_file:
            if "TargetFramework" in line:
                target_framework = line.split(">")[1].split("<")[0].strip()
                print("TARGET FRAMEWORK", target_framework)
                break

    return os.path.join(godot_mono_decomp_dir, "bin", get_dotnet_variant_name(dev_build), target_framework, triplet, "publish")


def get_godot_mono_decomp_lib_paths(build_env, godot_mono_decomp_dir, libs, mono_native_lib_type):
    lib_names = get_godot_mono_decomp_libs(mono_native_lib_type == "Static", build_env, libs)
    build_dir = get_godot_mono_decomp_lib_dir(
        godot_mono_decomp_dir,
        build_env["platform"],
        build_env["arch"],
        is_dev_build(build_env),
    )
    lib_paths = [os.path.join(build_dir, lib) for lib in lib_names]
    print("GODOT MONO DECOMP LIB PATHS", lib_paths)
    return lib_paths


def get_dotnet_publish_cmd(build_env, mono_native_lib_type, target_arch):
    build_variant = get_dotnet_variant_name(is_dev_build(build_env))
    dotnet_publish_cmd = [
        "dotnet",
        "publish",
        f"/p:NativeLib={mono_native_lib_type}",
        "/p:PublishProfile=AOT",
        "-c",
        build_variant,
        "-r",
        get_godot_mono_triplet(build_env["platform"], target_arch),
    ]
    if build_env["platform"] == "android" or mono_native_lib_type == "Static":
        dotnet_publish_cmd += ["-p:DisableUnsupportedError=true", "-p:PublishAotUsingRuntimePack=true"]
    if mono_native_lib_type == "Static":
        dotnet_publish_cmd += ["--use-current-runtime", "--self-contained"]
    return dotnet_publish_cmd


def godot_mono_builder(
    target,
    source,
    build_env,
    mono_native_lib_type,
    godot_mono_decomp_dir,
    godot_mono_decomp_libs,
    build_dir,
):
    print("GODOT MONO DECOMP BUILD: ", target)
    libs = get_godot_mono_decomp_lib_paths(build_env, godot_mono_decomp_dir, godot_mono_decomp_libs, mono_native_lib_type)
    dev_build = is_dev_build(build_env)
    build_variant = get_dotnet_variant_name(dev_build)
    print("BUILD VARIANT ", build_variant)

    cmd_env = get_cmd_env(build_env)
    dotnet_publish_cmd = get_dotnet_publish_cmd(build_env, mono_native_lib_type, build_env["arch"])
    lib_dir = get_godot_mono_decomp_lib_dir(godot_mono_decomp_dir, build_env["platform"], build_env["arch"], dev_build)
    print("DOTNET PUBLISH CMD", " ".join(dotnet_publish_cmd))
    try:
        dotnet_publish_output = check_output(dotnet_publish_cmd, cwd=godot_mono_decomp_dir, stderr=STDOUT, env=cmd_env)
    except Exception as exc:
        print("ERROR PUBLISHING GODOT MONO DECOMP", exc)
        print(exc.output.decode("utf-8"))
        raise

    print("DOTNET PUBLISH OUTPUT", dotnet_publish_output.decode("utf-8"))
    other_lib_dir = ""
    if build_env["platform"] == "macos" and mono_native_lib_type == "Shared":
        new_arch = "x86_64" if build_env["arch"] == "arm64" else "arm64"
        dotnet_publish_cmd = get_dotnet_publish_cmd(build_env, mono_native_lib_type, new_arch)
        other_lib_dir = get_godot_mono_decomp_lib_dir(godot_mono_decomp_dir, build_env["platform"], new_arch, dev_build)
        print("DOTNET PUBLISH CMD", dotnet_publish_cmd)
        dotnet_publish_output = check_output(dotnet_publish_cmd, cwd=godot_mono_decomp_dir)
        print("DOTNET PUBLISH OUTPUT", dotnet_publish_output.decode("utf-8"))

    if build_env["platform"] == "android" or mono_native_lib_type == "Static":
        return

    if not os.path.exists(build_dir):
        os.makedirs(build_dir)

    for lib in libs:
        if build_env.msvc:
            lib = lib.replace(".lib", ".dll")
        copy_path = os.path.abspath(os.path.join(build_dir, os.path.basename(lib)))
        print("LIB PATH", lib)
        print("COPY PATH", copy_path)
        if build_env["platform"] == "macos":
            lipo_cmd = [
                "lipo",
                "-create",
                lib,
                os.path.join(other_lib_dir, os.path.basename(lib)),
                "-output",
                copy_path,
            ]
            print("LIPOING LIB TO ", copy_path)
            check_output(lipo_cmd, cwd=lib_dir)
        else:
            shutil.copy(lib, copy_path)


def get_godot_mono_static_linker_args_from_json(json_output):
    json_blob = json.loads(json_output)
    linker_args = []
    library_args = []
    framework_args = []
    for item in json_blob["Items"]["LinkerArg"]:
        line = item["Identity"].strip()
        if line.startswith("/") or line.startswith("\\") or line[0].isalpha():
            if "libbootstrapper" in line:
                continue
            linker_args.append(line)
        elif line.startswith("-l"):
            library_args.append(line.removeprefix("-l").strip())
        elif line.startswith("-framework"):
            framework_args.append(line.removeprefix("-framework").strip())
    return linker_args, library_args, framework_args


def get_android_lib_dest(build_env):
    lib_arch_dir = ""
    if build_env["arch"] == "arm32":
        lib_arch_dir = "armeabi-v7a"
    elif build_env["arch"] == "arm64":
        lib_arch_dir = "arm64-v8a"
    elif build_env["arch"] == "x86_32":
        lib_arch_dir = "x86"
    elif build_env["arch"] == "x86_64":
        lib_arch_dir = "x86_64"
    else:
        print("Architecture not suitable for embedding into APK; keeping .so at \\bin")

    lib_tools_dir = "tools/" if build_env.editor_build else ""
    if build_env.dev_build:
        lib_type_dir = "dev"
    elif build_env.debug_features:
        if build_env.editor_build and build_env["store_release"]:
            lib_type_dir = "release"
        else:
            lib_type_dir = "debug"
    else:
        lib_type_dir = "release"

    jni_libs_dir = "#platform/android/java/lib/libs/" + lib_tools_dir + lib_type_dir + "/"
    out_dir = jni_libs_dir + lib_arch_dir
    return build_env.Dir(out_dir).abspath


def build_godot_mono_decomp(
    env,
    env_gdsdecomp,
    module_obj,
    module_dir,
    build_dir,
    mono_native_lib_type,
    godot_mono_decomp_parent,
    godot_mono_decomp_dir,
    godot_mono_decomp_libs,
    get_sources,
    add_libs_to_env,
    builder_class,
    copy_action,
):
    libs = get_godot_mono_decomp_lib_paths(env, godot_mono_decomp_dir, godot_mono_decomp_libs, mono_native_lib_type)
    if mono_native_lib_type == "Static":
        lib_suffix = ".lib" if env.msvc else ".a"
    else:
        if env.msvc:
            lib_suffix = ".lib"
        elif env["platform"] == "macos":
            lib_suffix = ".dylib"
        else:
            lib_suffix = ".so"

    src_suffixes = ["*.h", "*.cs", "*.csproj", "*.props", "*.targets", "*.pubxml"]

    def _builder_action(target, source, env):
        return godot_mono_builder(
            target,
            source,
            env,
            mono_native_lib_type,
            godot_mono_decomp_dir,
            godot_mono_decomp_libs,
            build_dir,
        )

    env_gdsdecomp["BUILDERS"]["godotMonoDecompBuilder"] = builder_class(
        action=_builder_action,
        suffix=lib_suffix,
        src_suffix=src_suffixes,
    )

    depends_libs = []
    if env["platform"] != "android" and mono_native_lib_type == "Shared":
        for lib in libs:
            copied_lib = os.path.join(build_dir, os.path.basename(lib))
            if env.msvc:
                copied_lib = copied_lib.replace(".lib", ".dll")
            depends_libs.append(copied_lib)

    all_libs = libs + depends_libs
    mono_sources = get_sources(
        module_dir,
        godot_mono_decomp_parent,
        src_suffixes,
        ["obj/", "bin/", "obj\\", "bin\\"],
    )
    env_gdsdecomp.Alias("godotMonoDecomp", [env_gdsdecomp.godotMonoDecompBuilder(all_libs, mono_sources)])

    if env["platform"] == "android" and mono_native_lib_type == "Shared":
        android_lib_dest = get_android_lib_dest(env) + "/libGodotMonoDecompNativeAOT.so"
        env_gdsdecomp.CommandNoCache(android_lib_dest, libs[0], copy_action("$TARGET", "$SOURCE"))

    add_libs_to_env(env, env_gdsdecomp, module_obj, libs, mono_sources)
    env.Depends(module_obj, depends_libs)

    if env["platform"] == "linuxbsd" and mono_native_lib_type == "Shared":
        env.Append(LINKFLAGS=["-z", "origin"])
        env.Append(RPATH=env.Literal("\\$$ORIGIN"))

    if mono_native_lib_type == "Static":
        # TODO: Keep static-linking path intact until dotnet static issues are resolved.
        if env.msvc:
            env.Append(LINKFLAGS=["/FORCE:MULTIPLE"])
        else:
            env.Append(LINKFLAGS=["-Wl,--allow-multiple-definition"])

        cmd_env = get_cmd_env(env)
        properties = {
            "RuntimeIdentifier": get_godot_mono_triplet(env["platform"], env["arch"]),
            "UseCurrentRuntimeIdentifier": "True",
            "NativeLib": "Static",
            "PublishProfile": "AOT",
            "SelfContained": "true",
            "_IsPublishing": "true",
            "Configuration": get_dotnet_variant_name(is_dev_build(env)),
        }
        # TODO: This is intentionally unconditional for now to preserve current behavior.
        properties["DisableUnsupportedError"] = "true"
        properties["PublishAotUsingRuntimePack"] = "true"

        dotnet_get_linker_args_cmd = [
            "dotnet",
            "msbuild",
            "-restore",
            "-target:PrintLinkerArgs",
            "-getItem:LinkerArg",
        ]
        for property_name, value in properties.items():
            dotnet_get_linker_args_cmd.append(f"-p:{property_name}={value}")
        for property_name, value in properties.items():
            dotnet_get_linker_args_cmd.append(f"-restoreProperty:{property_name}={value}")
        print("RUNNING RESTORE: ", " ".join(dotnet_get_linker_args_cmd))
        ret = check_output(args=dotnet_get_linker_args_cmd, cwd=godot_mono_decomp_dir, env=cmd_env)

        linker_args, library_args, framework_args = get_godot_mono_static_linker_args_from_json(ret.decode("utf-8"))
        if len(linker_args) == 0:
            raise Exception("No linker args found in msbuild.log!!!!!!!!!!!!")
        print("MONO DECOMP LINKER ARGS", linker_args)
        print("MONO DECOMP LIBRARY ARGS", library_args)
        print("MONO DECOMP FRAMEWORK ARGS", framework_args)
        env.Append(LINKFLAGS=linker_args)
        env.Append(LIBS=library_args)
        if env["platform"] == "macos":
            for framework in framework_args:
                env.Append(LINKFLAGS=["-framework", framework])

