#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Add scripts to sys.path to import boards
sys.path.append(os.path.join(os.path.dirname(__file__), "scripts"))
from boards import SUPPORTED_BOARDS

BOARDS = list(SUPPORTED_BOARDS.keys())

STEPS = ["webapp", "splash", "firmware"]
PROJECT_IDF_TARGET = "esp32s3"
PROJECT_ROOT = Path(__file__).resolve().parent


def idf_invocation(board, extra_args, debug=False, environ=None):
    """Return deterministic ESP-IDF arguments, post-build arguments, and environment."""
    sdkconfig_defaults = f"sdkconfig.defaults;boards/sdkconfig.defaults.{board}"
    if debug:
        sdkconfig_defaults += ";sdkconfig.defaults.debug"

    environment = dict(os.environ if environ is None else environ)
    inherited_target = environment.get("IDF_TARGET")
    if inherited_target and inherited_target != PROJECT_IDF_TARGET:
        print(
            f"Ignoring inherited IDF_TARGET={inherited_target}; "
            f"this project requires {PROJECT_IDF_TARGET}."
        )
    environment["IDF_TARGET"] = PROJECT_IDF_TARGET

    cmake_defines = []
    post_build_args = []
    for argument in extra_args:
        if argument.startswith("-DIDF_TARGET="):
            requested_target = argument.split("=", 1)[1]
            if requested_target != PROJECT_IDF_TARGET:
                print(
                    f"Ignoring caller IDF_TARGET={requested_target}; "
                    f"this project requires {PROJECT_IDF_TARGET}."
                )
            continue
        if argument.startswith("-D"):
            cmake_defines.append(argument)
        else:
            post_build_args.append(argument)

    idf_base = [
        "idf.py",
        f"-DIDF_TARGET={PROJECT_IDF_TARGET}",
        f"-DSDKCONFIG_DEFAULTS={sdkconfig_defaults}",
    ]
    return idf_base, cmake_defines, post_build_args, environment


def clean_project_state(project_root=None):
    """Delete only generated project configuration/build state."""
    project_root = PROJECT_ROOT if project_root is None else Path(project_root)
    for filename in ("sdkconfig", "partitions.csv"):
        path = project_root / filename
        if path.exists():
            path.unlink()
            print(f"  ✓ Removed {filename}")
    build_dir = project_root / "build"
    if build_dir.is_dir():
        shutil.rmtree(build_dir)
        print("  ✓ Removed build/")


def build_webapp():
    """Build the webapp (npm install + npm run build)."""
    print("\n=== Building webapp ===")
    try:
        subprocess.run("npm install", shell=True, check=True, cwd="webapp")
        subprocess.run("npm run build", shell=True, check=True, cwd="webapp")
    except subprocess.CalledProcessError as e:
        print(f"  ✗ Webapp build failed with exit code {e.returncode}")
        sys.exit(e.returncode)
    except FileNotFoundError:
        print(
            "  ✗ 'npm' not found. Please ensure Node.js is installed and in your PATH."
        )
        sys.exit(1)


def generate_splash(board):
    """Generate splash screen EPDGZ for the target board."""
    print(f"\n=== Generating splash screen for {board} ===", flush=True)
    output_dir = os.path.join(os.path.dirname(__file__), "main", "splash_data")
    script = os.path.join(os.path.dirname(__file__), "scripts", "generate_splash.py")
    process_cli_dir = os.path.join(os.path.dirname(__file__), "process-cli")

    # Ensure process-cli dependencies are installed
    node_modules = os.path.join(process_cli_dir, "node_modules")
    if not os.path.isdir(node_modules):
        print("  Installing process-cli dependencies...")
        try:
            subprocess.run("npm ci", shell=True, check=True, cwd=process_cli_dir)
        except subprocess.CalledProcessError as e:
            print(f"  ✗ npm ci failed in process-cli with exit code {e.returncode}")
            sys.exit(e.returncode)

    try:
        subprocess.run(
            [sys.executable, script, "--board", board, "--output-dir", output_dir],
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"  ✗ Splash generation failed with exit code {e.returncode}")
        sys.exit(e.returncode)


def build_firmware(board, extra_args, debug=False):
    """Build firmware with idf.py."""
    print(f"\n=== Building firmware for {board}{' [debug]' if debug else ''} ===")
    idf_base, cmake_defines, post_build_args, environment = idf_invocation(
        board, extra_args, debug=debug
    )

    build_cmd = idf_base + cmake_defines + ["build"]
    print(f"Running: {' '.join(build_cmd)}")

    try:
        subprocess.run(build_cmd, check=True, env=environment)
    except subprocess.CalledProcessError as e:
        print(f"Build failed with exit code {e.returncode}")
        sys.exit(e.returncode)
    except FileNotFoundError:
        print(
            "Error: 'idf.py' not found. Please ensure ESP-IDF is correctly installed and activated."
        )
        sys.exit(1)

    # Run post-build commands (flash, monitor, etc.)
    if post_build_args:
        post_cmd = idf_base + post_build_args
        print(f"Running: {' '.join(post_cmd)}")
        try:
            subprocess.run(post_cmd, check=True, env=environment)
        except subprocess.CalledProcessError as e:
            print(f"Post-build command failed with exit code {e.returncode}")
            sys.exit(e.returncode)


def main():
    parser = argparse.ArgumentParser(description="Build firmware for different boards")
    parser.add_argument(
        "--board",
        choices=BOARDS,
        default="waveshare_photopainter_73",
        help="Board type to build",
    )
    parser.add_argument(
        "--fullclean",
        action="store_true",
        help="Remove generated sdkconfig, partitions.csv, and build/ before building",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Debug build: enable core-dump-to-flash capture. Changes the "
        "partition table (adds a coredump partition) — do not ship to users.",
    )
    parser.add_argument(
        "--step",
        choices=STEPS,
        action="append",
        help="Run only specific step(s). Can be specified multiple times. "
        "If omitted, all steps run.",
    )
    # Allow passing extra arguments to idf.py
    args, extra_args = parser.parse_known_args()

    steps = args.step if args.step else STEPS

    if args.fullclean:
        print("Performing full clean...")
        clean_project_state()

    if "webapp" in steps:
        build_webapp()

    if "splash" in steps:
        generate_splash(args.board)

    if "firmware" in steps:
        build_firmware(args.board, extra_args, debug=args.debug)


if __name__ == "__main__":
    main()
