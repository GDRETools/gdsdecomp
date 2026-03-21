import os


EXTERNAL_STEM = "external_install"

MBEDTLS_THIRDPARTY_DIR = "#thirdparty/mbedtls/include/"
MMP3_THIRDPARTY_DIR = "#thirdparty/minimp3/"
LIBOGG_THIRDPARTY_DIR = "#thirdparty/libogg/"
LIBTHEORA_THIRDPARTY_DIR = "#thirdparty/libtheora/"
LIBVORBIS_THIRDPARTY_DIR = "#thirdparty/libvorbis/"
WEBP_THIRDPARTY_DIR = "#thirdparty/libwebp/"
THORSVG_THIRDPARTY_DIR = "#thirdparty/thorsvg/"
MODULE_INCLUDE_DIR = "#modules/gdsdecomp/"
VTRACER_INCLUDE_DIR = "#modules/gdsdecomp/external/vtracer/include"
GODOT_MONO_DECOMP_INCLUDE_DIR = "#modules/gdsdecomp/godot-mono-decomp/GodotMonoDecompNativeAOT/include"

VTRACER_PREFIX = "external/vtracer"
VTRACER_LIBS = ["vtracer"]

GODOT_MONO_DECOMP_PARENT = "godot-mono-decomp"
GODOT_MONO_DECOMP_PREFIX = "godot-mono-decomp/GodotMonoDecompNativeAOT"
GODOT_MONO_DECOMP_LIBS = ["GodotMonoDecompNativeAOT"]


def get_module_dir(env):
    return env.Dir("#modules/gdsdecomp").abspath


def get_build_dir(env):
    return env.Dir("#bin").abspath


def get_external_dir(module_dir):
    return os.path.join(module_dir, EXTERNAL_STEM)


def get_vtracer_dir(module_dir):
    return os.path.join(module_dir, VTRACER_PREFIX)


def get_vtracer_build_dir(module_dir):
    return os.path.join(get_vtracer_dir(module_dir), "target")


def get_godot_mono_decomp_dir(module_dir):
    return os.path.join(module_dir, GODOT_MONO_DECOMP_PREFIX)

