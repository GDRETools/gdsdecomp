from typing import Any


import os
import subprocess
import sys


GLTF_VALIDATOR_PATH = "/Users/nikita/Workspace/gltf_validator-2.0.0-dev.3.10-macos64/gltf_validator"


def get_gltf_files(output_dir: str) -> list[str]:
    gltf_files: list[str] = []
    for root, dirs, files in os.walk(output_dir):
        for file in files:
            if file.endswith(".glb"):  # or file.endswith(".gltf"):
                gltf_files.append(os.path.join(root, file))
        for dir in dirs:
            gltf_files.extend(get_gltf_files(os.path.join(root, dir)))
    return gltf_files


def main():
    # current_dir = os.path.dirname(os.path.abspath(__file__))
    # project_path = os.path.abspath(os.path.join(current_dir, "..", "standalone"))
    # # bin path is ../../..
    # bin_path = os.path.abspath(os.path.join(current_dir, "..", "..", "bin"))
    # print(bin_path)

    # godot_path = os.path.join(bin_path, "godot.macos.editor.dev.arm64")
    # print(godot_path)

    # # get args from command line
    # args = sys.argv[1:]
    # recover_path = args[0]
    # output_dir = args[1]
    # print(args)
    # cli_args = [
    #     "--headless",
    #     "--path",
    #     project_path,
    #     "--recover",
    #     recover_path,
    #     "--output-dir",
    #     output_dir,
    # ]

    # # run the process
    # process = subprocess.Popen([godot_path] + cli_args)
    # ret = process.wait()
    # print("Process finished with return code: ", ret)
    # if ret != 0:
    #     print("Process failed")
    #     return

    # # check if the output directory exists
    # if not os.path.exists(output_dir):
    #     print("Output directory does not exist")
    #     return

    # # check if the output directory is not empty
    # if len(os.listdir(output_dir)) == 0:
    #     print("Output directory is empty")
    #     return

    output_dir = "/Users/nikita/Workspace/godot-ws/test-decomps/Ex-Zodiac_decomp3"

    # recursively get all the gltf and glb files in the output directory
    gltf_files = get_gltf_files(output_dir)
    print(gltf_files)
    outputs: dict[str, str] = {}
    for file in gltf_files:
        # ret = subprocess.run([GLTF_VALIDATOR_PATH, file])
        # we need to get the stdout AND the return code
        process = subprocess.Popen([GLTF_VALIDATOR_PATH, "-m", file], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        ret = process.returncode
        if ret != 0:
            print(f"{file}")
            outputs[file] = f"{stdout.decode('utf-8')}\n{stderr.decode('utf-8')}"
    for file, output in outputs.items():
        print(f"{file}")
        print(output)
        print("-" * 100)


if __name__ == "__main__":
    main()
