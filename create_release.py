Import("env")
import shutil
import os
import datetime


def create_release(source, target, env):
    """
    This function creates release for the built firmware.bin
    Copies the firmware in two forms
    1- With current version
    2- With current version + buildtime

    :param source: Description
    :param target: Description
    :param env: Description
    """
    now = datetime.datetime.now()
    timestamp = now.strftime("%Y%m%d-%H%M%S")
    parsed = env.ParseFlags(env["BUILD_FLAGS"])
    defines = {k: v for (k, v) in parsed.get("CPPDEFINES", [])}
    firmware_version = f'{defines["VERSION_MAJOR"]}.{defines["VERSION_MINOR"]}.{defines["VERSION_PATCH"]}'
    bin_path = str(target[0])  # e.g., full path to firmware.bin
    # print("Available env keys:", list(env.keys()))  # First 20 keys
    proj_dir = str(env["PROJECT_DIR"])
    dest_path = os.path.join(proj_dir, "release")
    try:
        os.mkdir(dest_path)
    except FileExistsError:
        pass
    firmware_release = os.path.join(
        dest_path, f"nigga_firmware_v{firmware_version}.bin"
    )
    firmware_release_verbose = os.path.join(
        dest_path, f"nigga_firmware_v{firmware_version}_{timestamp}.bin"
    )
    shutil.copy2(bin_path, firmware_release)
    shutil.copy2(bin_path, firmware_release_verbose)
    print(f"Copied to {dest_path}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", create_release)
