import glob
import os
import platform
from typing import Any


def is_dev_build(build_env):
    return build_env["dev_build"] if "dev_build" in build_env else False


def get_sources(module_dir, rel_path, filters=None, exclude_filters=None):
    if filters is None:
        filters = ["*.h", "*.hpp", "*.cpp"]
    if exclude_filters is None:
        exclude_filters = []

    abs_path = os.path.join(module_dir, rel_path)
    if not os.path.exists(abs_path):
        raise Exception(
            f"Path {abs_path} does not exist, please run `git submodule update --init --recursive` in the patchwork_editor directory"
        )

    sources = []
    for suffix in filters:
        globstr = os.path.join(abs_path, "**", suffix)
        new_sources = glob.glob(globstr, recursive=True)
        sources += [
            source
            for source in new_sources
            if (not exclude_filters or not any(exclude_filter in source for exclude_filter in exclude_filters))
        ]
    return [os.path.relpath(source, module_dir) for source in sources]


def host_is_windows():
    return platform.system().lower().startswith("win")


def find_llvm_prebuild_path(build_env):
    host_os_name = platform.system().lower()
    if host_os_name.startswith("win"):
        host_os_name = "win"
    ndk_llvm_path = os.path.join(build_env["ANDROID_NDK_ROOT"], "toolchains", "llvm", "prebuilt")
    for folder_name in os.listdir(ndk_llvm_path):
        if folder_name.startswith(host_os_name):
            return os.path.join(ndk_llvm_path, folder_name, "bin")
    raise Exception(f"Could not find LLVM prebuild path for {host_os_name} in {ndk_llvm_path}")


def get_cmd_env(build_env):
    cmd_env = os.environ.copy()
    if build_env["platform"] != "android":
        return cmd_env
    ndk_llvm_path = find_llvm_prebuild_path(build_env)
    print("NDK_LLVM_PATH", ndk_llvm_path)
    path_joiner = ":" if not host_is_windows() else ";"
    cmd_env["PATH"] = ndk_llvm_path + path_joiner + os.environ.get("PATH", "")
    cmd_env["ANDROID_HOME"] = build_env["ANDROID_HOME"]
    cmd_env["ANDROID_SDK_ROOT"] = cmd_env["ANDROID_HOME"]
    cmd_env["ANDROID_NDK_ROOT"] = build_env["ANDROID_NDK_ROOT"]
    return cmd_env


def add_libs_to_env(env, env_gdsdecomp, module_obj, libs, sources):
    for lib in libs:
        env_gdsdecomp.Depends(lib, sources)
        if env.msvc:
            if not lib.endswith(".lib"):
                lib = lib.rsplit(".", 1)[0] + ".lib"
            env.Append(LINKFLAGS=[lib])
            continue

        lib_name = os.path.basename(lib).split(".")[0]
        print("LIB NAME", lib_name)
        env.Append(LIBS=[lib_name])
    env.Depends(module_obj, libs)


# TODO: This helper is currently unused in SCsub.
# Keep it for future external CMake dependencies instead of deleting it.
def cmake_builder(external_dir, source_dir, build_dir, libs, check_output_fn, config_options=None):
    output = bytes()
    config_cmd = ["cmake", "-S", source_dir, "-B", build_dir]
    if config_options:
        config_cmd += config_options
    try:
        output += check_output_fn(["cmake", "-E", "make_directory", external_dir]) + b"\n"
        output += check_output_fn(["cmake", "-E", "make_directory", build_dir]) + b"\n"
        output += check_output_fn(config_cmd) + b"\n"
        output += check_output_fn(["cmake", "--build", build_dir]) + b"\n"
        for lib in libs:
            lib_path = os.path.join(external_dir, lib)
            if os.path.exists(lib_path):
                os.remove(lib_path)
        output += check_output_fn(["cmake", "--install", build_dir, "--prefix", external_dir]) + b"\n"
    except Exception as exc:
        print(f"Failed to build external CMake project: {exc}")
        print(f"Output: {output.decode('utf-8')}")
        raise


def append_cpppaths(build_env, paths):
    for include_path in paths:
        build_env.Append(CPPPATH=[include_path])


def add_source_groups(build_env, module_obj, source_globs):
    for source_glob in source_globs:
        build_env.add_source_files(module_obj, source_glob)

