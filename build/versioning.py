import re
import shutil
from subprocess import PIPE, Popen


SEMVER_REGEX = r"^[vV]?(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$"


def doproc(cmd):
    process = Popen(cmd, stdout=PIPE, stderr=PIPE)
    output, err = process.communicate()
    if not err:
        return output.decode("utf-8").strip()
    return None


def get_version_info():
    git = shutil.which("git")
    version_info = "unknown"
    if git is None:
        print("GDRE WARNING: cannot find git on path, unknown version will be saved in gdre_version.gen.h")
    else:
        version_info = doproc([git, "describe", "--tags", "--abbrev=6"])
        if version_info is None:
            print("GDRE WARNING: git failed to run, unknown version will be saved in gdre_version.gen.h")
            version_info = "unknown"
        else:
            res = doproc([git, "describe", "--exact-match", "--tags", "HEAD"])
            if not res:
                splits = version_info.split("-")
                build_info = splits[-1]
                build_num = splits[-2]
                new_version_info = "-".join(splits[:-2])
                semver_regex_match = re.match(SEMVER_REGEX, new_version_info)
                if semver_regex_match:
                    major = semver_regex_match.group("major")
                    minor = semver_regex_match.group("minor")
                    patch = semver_regex_match.group("patch")
                    prerelease_tag = semver_regex_match.group("prerelease")
                else:
                    print("WARNING: version string does not match semver format")
                    splits = new_version_info.split(".")
                    if len(splits) < 3:
                        print("WARNING: version string is too short")
                        major = "0"
                        minor = "0"
                        patch = "0"
                    else:
                        major = splits[0]
                        minor = splits[1]
                        patch = splits[2]
                    prerelease_tag = ""
                dev_stuff = f"dev.{build_num}+{build_info}"
                if prerelease_tag:
                    prerelease_name = prerelease_tag.split(".")[0]
                    prerelease_num = prerelease_tag.split(".")[-1]
                    if prerelease_num.isdigit():
                        prerelease_num = str(int(prerelease_num) + 1)
                        print("prerelease_num", prerelease_num)
                        prerelease_tag = f"{prerelease_name}.{prerelease_num}"
                    else:
                        prerelease_tag += ".1"
                    new_version_info = f"{major}.{minor}.{patch}-{prerelease_tag}+{dev_stuff.replace('+', '-')}"
                else:
                    patch = str(int(patch) + 1) if patch.isdigit() else 0
                    new_version_info = f"{major}.{minor}.{patch}-{dev_stuff}"
                version_info = new_version_info
            else:
                version_info = res
    return version_info


def write_version_header(output_header_path):
    version_info = get_version_info()
    with open(output_header_path, "w") as header_file:
        header_file.write(f'#define GDRE_VERSION "{version_info}"\n')

