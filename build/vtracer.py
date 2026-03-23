import os
import platform
import shutil
from subprocess import check_output

from .common import get_cmd_env, is_dev_build


def get_cargo_target(build_env):
    arch_part = "x86_64" if build_env["arch"] == "x86_64" else "aarch64"
    if build_env["platform"] == "macos":
        return f"{arch_part}-apple-darwin"
    if build_env["platform"] == "linuxbsd":
        return f"{arch_part}-unknown-linux-gnu"
    if build_env["platform"] == "windows":
        suffix = "msvc" if build_env.msvc else "gnu"
        return f"{arch_part}-pc-windows-{suffix}"
    if build_env["platform"] == "web":
        return "wasm32-unknown-emscripten"
    if build_env["platform"] == "ios":
        return f"{arch_part}-apple-ios"
    if build_env["platform"] == "android":
        return f"{arch_part}-linux-android"
    raise Exception(f"Unsupported platform: {build_env['platform']}")


def write_cargo_config_toml_file(build_env, module_dir, vtracer_prefix, find_llvm_prebuild_path):
    is_windows = platform.system().lower().startswith("win")
    llvm_prebuild_path = find_llvm_prebuild_path(build_env).replace("\\", "/")
    ar = llvm_prebuild_path + "/llvm-ar"
    min_sdk_ver = build_env["ndk_platform"].split("-")[1]
    arm64_linker = llvm_prebuild_path + f"/aarch64-linux-android{min_sdk_ver}-clang"
    arm_linker = llvm_prebuild_path + f"/arm7a-linux-android{min_sdk_ver}-clang"
    x86_linker = llvm_prebuild_path + f"/i686-linux-android{min_sdk_ver}-clang"
    x86_64_linker = llvm_prebuild_path + f"/x86_64-linux-android{min_sdk_ver}-clang"

    if is_windows:
        ar += ".exe"
        arm64_linker += ".cmd"
        arm_linker += ".cmd"
        x86_linker += ".cmd"
        x86_64_linker += ".cmd"

    text = f"""
[target.aarch64-linux-android]
ar = "{ar}"
linker = "{arm64_linker}"

[target.armv7-linux-androideabi]
ar = "{ar}"
linker = "{arm_linker}"

[target.i686-linux-android]
ar = "{ar}"
linker = "{x86_linker}"

[target.x86_64-linux-android]
ar = "{ar}"
linker = "{x86_64_linker}"
"""
    cargo_dir = os.path.join(module_dir, vtracer_prefix, ".cargo")
    os.makedirs(cargo_dir, exist_ok=True)
    with open(os.path.join(cargo_dir, "config.toml"), "w") as config_file:
        config_file.write(text)


def write_cargo_toolchain_toml_file(build_env, module_dir, vtracer_prefix):
    channel = "nightly" if build_env["platform"] == "web" else "stable"
    text = f"""[toolchain]
channel = "{channel}"
"""
    vtracer_dir = os.path.join(module_dir, vtracer_prefix)
    os.makedirs(vtracer_dir, exist_ok=True)
    with open(os.path.join(vtracer_dir, "rust-toolchain.toml"), "w") as toolchain_file:
        toolchain_file.write(text)


def get_vtracer_libs(build_env, vtracer_libs):
    lib_prefix = "" if build_env.msvc else "lib"
    lib_suffix = ".lib" if build_env.msvc else ".a"
    return [lib_prefix + lib + lib_suffix for lib in vtracer_libs]


def get_vtracer_lib_dir(build_env, vtracer_build_dir):
    rust_target = get_cargo_target(build_env)
    variant_dir = "debug" if is_dev_build(build_env) else "release"
    return os.path.join(vtracer_build_dir, rust_target, variant_dir)


def get_vtracer_lib_paths(build_env, vtracer_build_dir, vtracer_libs):
    build_dir = get_vtracer_lib_dir(build_env, vtracer_build_dir)
    lib_paths = [os.path.join(build_dir, lib) for lib in get_vtracer_libs(build_env, vtracer_libs)]
    print("LIB PATHS", lib_paths)
    return lib_paths


def cargo_builder(
    module_env,
    external_dir,
    source_dir,
    build_dir,
    libs,
    build_env,
    module_dir,
    vtracer_prefix,
    find_llvm_prebuild_path,
):
    if build_env is None:
        raise Exception("build_env is required")
    build_variant = "Debug" if is_dev_build(build_env) else "Release"
    print("BUILD VARIANT", build_variant)
    cargo_target = get_cargo_target(build_env)
    cbindgen_dir = os.path.join(source_dir, "include", "vtracer")

    cargo_cmd = ["cargo", "build", "--lib", f"--target={cargo_target}"]
    if not is_dev_build(build_env):
        cargo_cmd.append("--release")
    if "disable_gifski" in build_env and build_env["disable_gifski"]:
        cargo_cmd.extend(["--no-default-features"])

    cargo_env = get_cmd_env(build_env)
    if build_env["platform"] == "android":
        write_cargo_config_toml_file(build_env, module_dir, vtracer_prefix, find_llvm_prebuild_path)
    write_cargo_toolchain_toml_file(build_env, module_dir, vtracer_prefix)
    cargo_env["CARGO_TARGET_DIR"] = build_dir
    cargo_env["CBINDGEN_TARGET_DIR"] = cbindgen_dir

    if build_env["platform"] == "web":
        cargo_env["RUSTFLAGS"] = (
            "-Cpanic=abort -Ctarget-feature=+atomics,+bulk-memory,+mutable-globals "
            "-Clink-arg=-fno-exceptions -Clink-arg=-sDISABLE_EXCEPTION_THROWING=1 "
            "-Clink-arg=-sDISABLE_EXCEPTION_CATCHING=1 -Cllvm-args=-enable-emscripten-cxx-exceptions=0 "
            "-Clink_arg=--no-entry -Zlink-native-libraries=no"
        )
        cargo_cmd.append("-Zbuild-std=std,panic_abort")

    print("CARGO CMD", cargo_cmd)
    output = check_output(cargo_cmd, cwd=source_dir, env=cargo_env)
    print(output.decode("utf-8"))

    if module_env.msvc:
        destination_lib_dir = get_vtracer_lib_dir(module_env, build_dir)
        for lib in libs:
            lib_path = os.path.join(destination_lib_dir, os.path.basename(lib).split(".")[0] + module_env["LIBSUFFIX"])
            if os.path.exists(lib):
                shutil.copy(lib, lib_path)


def build_vtracer(
    root_env,
    env_gdsdecomp,
    module_obj,
    module_dir,
    external_dir,
    vtracer_prefix,
    vtracer_dir,
    vtracer_build_dir,
    vtracer_libs,
    get_sources,
    add_libs_to_env,
    builder_class,
    find_llvm_prebuild_path,
):
    source_suffixes = ["*.h", "*.cpp", "*.rs", "*.txt"]
    lib_suffix = ".lib" if root_env.msvc else ".a"

    def vtracer_builder(target, source, env):
        print("VTRACER BUILD")
        print(str(target[0]))
        print(source)
        cargo_builder(
            root_env,
            external_dir,
            vtracer_dir,
            vtracer_build_dir,
            get_vtracer_lib_paths(root_env, vtracer_build_dir, vtracer_libs),
            env,
            module_dir,
            vtracer_prefix,
            find_llvm_prebuild_path,
        )

    env_gdsdecomp["BUILDERS"]["vtracerBuilder"] = builder_class(
        action=vtracer_builder,
        suffix=lib_suffix,
        src_suffix=source_suffixes,
    )
    libs = get_vtracer_lib_paths(root_env, vtracer_build_dir, vtracer_libs)
    vtracer_sources = get_sources(module_dir, vtracer_prefix, source_suffixes)
    env_gdsdecomp.Alias("vtracerlib", [env_gdsdecomp.vtracerBuilder(libs, vtracer_sources)])
    add_libs_to_env(root_env, env_gdsdecomp, module_obj, libs, vtracer_sources)
    if root_env.msvc:
        root_env.Append(LINKFLAGS=["userenv.lib"])

