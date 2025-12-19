Import("env")
import shutil
import os
import datetime


def create_release(source, target, env):
    """
    This function creates release for the built binaries (firmware or filesystem)
    Copies the binary in two forms:
    1- With current version
    2- With current version + buildtime (optional)

    :param source: Description
    :param target: Description
    :param env: Description
    """
    now = datetime.datetime.now()
    timestamp = now.strftime("%Y%m%d-%H%M%S")
    parsed = env.ParseFlags(env["BUILD_FLAGS"])
    defines = {k: v for (k, v) in parsed.get("CPPDEFINES", [])}
    firmware_version = f'{defines["VERSION_MAJOR"]}.{defines["VERSION_MINOR"]}.{defines["VERSION_PATCH"]}'
    
    bin_path = str(target[0])
    bin_filename = os.path.basename(bin_path)
    
    # Determine binary type
    if bin_filename == "littlefs.bin" or bin_filename == "spiffs.bin":
        bin_type = "filesystem"
    else:
        bin_type = "firmware"
        
    proj_dir = str(env["PROJECT_DIR"])
    dest_path = os.path.join(proj_dir, "release", bin_type)
    
    if not os.path.exists(dest_path):
        os.makedirs(dest_path)
        
    release_name = os.path.join(
        dest_path, f"nigga_{bin_type}_v{firmware_version}.bin"
    )
    # release_name_verbose = os.path.join(
    #     dest_path, f"nigga_{bin_type}_v{firmware_version}_{timestamp}.bin"
    # )
    
    shutil.copy2(bin_path, release_name)
    # shutil.copy2(bin_path, release_name_verbose)
    print(f"[{bin_type}] Copied to {release_name}")


# Hook for Firmware
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", create_release)

# Hook for Filesystem (LittleFS)
env.AddPostAction("$BUILD_DIR/littlefs.bin", create_release)
