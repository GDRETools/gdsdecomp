import base64
import shutil
import sys
import os
import json
import platform
import subprocess
import glob

PLUGINS_TO_PREPOP = [
    "godotgif",
    "imgui-godot",
    "godot-rapier3d",
    "godot-rapier2d",
    "native_dialogs",
    "ffmpeg",
    "discord-sdk-gd",
    "discord-rpc-gd",
    "godot-steam-audio",
    "m_terrain",
    "steam_api",
    "orchestrator",
    "limboai",
    "fmod",
    "godot-sqlite",
    "godotsteam",
    "godot-jolt",
    "debug_draw_3d",
    "terrain_3d",
    "sg-physics-2d",
]


CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
BIN_DIR: str = os.path.join(CURRENT_DIR, "..", "..", "bin")
STANDALONE_DIR = os.path.join(CURRENT_DIR, "standalone")
TEMP_CACHE_DIR = os.path.join(CURRENT_DIR, ".tmpcache")
CACHE_DIR_ENV_VAR = "GDRE_PLUGIN_CACHE_DIR"
ASSET_LIB_PRECACHE_DIR = os.path.join(TEMP_CACHE_DIR, "asset_lib")
GITHUB_PRECACHE_DIR = os.path.join(TEMP_CACHE_DIR, "github")
GITLAB_PRECACHE_DIR = os.path.join(TEMP_CACHE_DIR, "gitlab")

HEADER_SRC_PATH: str = os.path.join(CURRENT_DIR, "misc", "static_plugin_cache.h.inc")
HEADER_DST_PATH: str = os.path.join(CURRENT_DIR, "utility", "static_plugin_cache.h")

JSON_DST_PATH: str = os.path.join(STANDALONE_DIR, "gdre_static_plugin_cache.json")

ASSET_LIB_PRECACHE_HEADER_START = "// __ASSET_LIB_PRECACHE__"
GITHUB_PRECACHE_HEADER_START = "// __GITHUB_PRECACHE__"
GITLAB_PRECACHE_HEADER_START = "// __GITLAB_PRECACHE__"

def get_godot_platform():
    if sys.platform.startswith("win"):
        return "windows"
    elif sys.platform.startswith("linux"):
        return "linux"
    elif sys.platform.startswith("darwin"):
        return "macos"
    else:
        raise ValueError(f"Unsupported platform: {sys.platform}")

def get_godot_arch():
    # if x86_64, return "x86_64"
    if platform.machine() == "AMD64":
        return "x86_64"
    elif platform.machine() == "arm64":
        return "arm64"
    else:
        raise ValueError(f"Unsupported architecture: {platform.machine()}")

DEV_MODE = os.getenv("DEV_MODE", "0") != "0"
GODOT_EXE = os.path.join(BIN_DIR, f"godot.{get_godot_platform()}.editor.{('dev.' if DEV_MODE else '')}{get_godot_arch()}")
ARGS = [GODOT_EXE, "--headless", "--path", STANDALONE_DIR]

def get_plugin_name_from_file(file_path: str) -> str:
    # get only the filename from the path
    return os.path.basename(file_path).replace(".json", "")

def get_asset_lib_plugin_name(plugin_name: str) -> str:
    # get the immediate parent directory name + the file name - .json
    return os.path.basename(os.path.dirname(plugin_name)) + "/" + os.path.basename(plugin_name).replace(".json", "")

def generate_header_file():
        # put literal quotes around the base64s
    header_src = open(HEADER_SRC_PATH, "r").read()
    asset_lib_base64s = get_base64s_from_dir(ASSET_LIB_PRECACHE_DIR)
    github_base64s = get_base64s_from_dir(GITHUB_PRECACHE_DIR)
    gitlab_base64s = get_base64s_from_dir(GITLAB_PRECACHE_DIR)

    asset_lib_base64s = [(get_asset_lib_plugin_name(file_path), base64) for file_path, base64 in asset_lib_base64s]
    github_base64s = [(get_plugin_name_from_file(file_path), base64) for file_path, base64 in github_base64s]
    gitlab_base64s = [(get_plugin_name_from_file(file_path), base64) for file_path, base64 in gitlab_base64s]

    asset_lib_base64_strs = [f'{{"{plugin_name}", "{base64}"}}' for plugin_name, base64 in asset_lib_base64s]
    github_base64_strs = [f'{{"{plugin_name}", "{base64}"}}' for plugin_name, base64 in github_base64s]
    gitlab_base64_strs = [f'{{"{plugin_name}", "{base64}"}}' for plugin_name, base64 in gitlab_base64s]
    header_src = header_src.replace(ASSET_LIB_PRECACHE_HEADER_START, ",\n".join(asset_lib_base64_strs))
    header_src = header_src.replace(GITHUB_PRECACHE_HEADER_START, ",\n".join(github_base64_strs))
    header_src = header_src.replace(GITLAB_PRECACHE_HEADER_START, ",\n".join(gitlab_base64_strs))
    with open(HEADER_DST_PATH, "w") as f:
        f.write(header_src)


def generate_json_file():

    jsons = get_json_dicts_from_dir(os.path.join(TEMP_CACHE_DIR, "plugin_versions"))
    main_dict = {}
    # github_base64s = get_json_dicts_from_dir(GITHUB_PRECACHE_DIR)
    # gitlab_base64s = get_json_dicts_from_dir(GITLAB_PRECACHE_DIR)
    # asset_lib_base64s = [(get_asset_lib_plugin_name(file_path), base64) for file_path, base64 in asset_lib_base64s]
    # github_base64s = [(get_plugin_name_from_file(file_path), base64) for file_path, base64 in github_base64s]
    # gitlab_base64s = [(get_plugin_name_from_file(file_path), base64) for file_path, base64 in gitlab_base64s]
    # asset_lib_base64_dict = {plugin_name: base64 for plugin_name, base64 in asset_lib_base64s}
    # github_base64_dict = {plugin_name: base64 for plugin_name, base64 in github_base64s}
    # gitlab_base64_dict = {plugin_name: base64 for plugin_name, base64 in gitlab_base64s}
    # main_dict = {
    #     "asset_lib": asset_lib_base64_dict,
    #     "github": github_base64_dict,
    #     "gitlab": gitlab_base64_dict
    # }
    previous_file_size = 0
    for _, blob in jsons:
        for _, json_dict in blob.items():
            release_info = json_dict["release_info"]
            key = (
                release_info["plugin_source"]
                + "-"
                + str(release_info["primary_id"])
                + "-"
                + str(release_info["secondary_id"])
            )
            main_dict[key] = json_dict
    # sort dict
    main_dict = dict(sorted(main_dict.items()))
    dest_path = JSON_DST_PATH
    already_exists = os.path.exists(JSON_DST_PATH)
    if already_exists:
        # check the file_size
        previous_file_size = os.path.getsize(JSON_DST_PATH)
        dest_path = JSON_DST_PATH + ".tmp"
    with open(dest_path, "w") as f:
        json.dump(main_dict, f)
    # if os.path.getsize(dest_path) < previous_file_size:
    #     print("File size decreased, aborting")
    #     # remove the tmp file
    #     os.remove(dest_path)
    #     sys.exit(1)
    if already_exists:
        # rename the tmp file to the original file
        os.rename(dest_path, JSON_DST_PATH)


def prepop_cache():
    if not os.path.exists(GODOT_EXE):
        raise FileNotFoundError(f"Godot executable not found: {GODOT_EXE}")
    if not os.path.exists(TEMP_CACHE_DIR):
        os.makedirs(TEMP_CACHE_DIR)
    # set the environment variable
    os.environ[CACHE_DIR_ENV_VAR] = TEMP_CACHE_DIR
    args: list[str] = ARGS.copy()
    for plugin in PLUGINS_TO_PREPOP:
        args.append(f"--plcache={plugin}")
    subprocess.run(args)


def get_json_dicts_from_dir(dir_path: str) -> list[tuple[str, object]]:
    json_dicts: list[tuple[str, object]] = []
    files = glob.glob(os.path.join(dir_path, "**/*.json"), recursive=True)
    for file in files:
        json_dicts.append((file.replace(".json", ""), json.load(open(file))))
    return json_dicts

def get_base64s_from_dir(dir_path: str) -> list[tuple[str, str]]:
    base64s: list[tuple[str, str]] = []
    # recursively get all the .json files in the directory
    files = glob.glob(os.path.join(dir_path, "**/*.json"), recursive=True)
    for file in files:
        base64s.append((file.replace(".json", ""), get_base64_from_file(file)))
    return base64s

def get_base64_from_file(file_path: str) -> str:
    with open(file_path, "rb") as file:
        content = file.read()
        # parse it as json, then dump it as json with minimal whitespace
        json_content = json.loads(content)
        minimal_json_content = json.dumps(json_content)
        if minimal_json_content == "{}":
            raise ValueError(f"File {file_path} is empty")
        return base64.b64encode(minimal_json_content.encode("utf-8")).decode("utf-8")

if __name__ == "__main__":
    prepop_cache()
    generate_json_file()
