#!/usr/bin/env python3

from __future__ import annotations

import argparse
import configparser
import os
import re
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IDF_TOOLS_PATH = Path(r"C:\ProgramData\Espressif")
DEFAULT_IDF_PATH = DEFAULT_IDF_TOOLS_PATH / "frameworks" / "esp-idf-v5.5.4"
DEFAULT_PYTHON_ENV_BASE = DEFAULT_IDF_TOOLS_PATH / "python_env"
ENV_KEYS_TO_CLEAR = (
    "ESP_IDF_VERSION",
    "IDF_PATH",
    "IDF_PATH_OLD",
    "IDF_PYTHON_ENV_PATH",
    "IDF_TARGET",
    "OPENOCD_SCRIPTS",
    "PYTHONHOME",
    "PYTHONPATH",
)
EXPORT_EXTRA_PATHS = (
    Path("components") / "espcoredump",
    Path("components") / "partition_table",
    Path("components") / "app_update",
)


def die(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def run(command: list[str], env: dict[str, str], cwd: Path = REPO_ROOT, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    print(f"> {' '.join(command)}")
    return subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        check=True,
        text=True,
        capture_output=capture_output,
    )


def find_python_env(python_env_base: Path) -> Path:
    candidates: list[tuple[tuple[int, int], Path]] = []
    for entry in python_env_base.glob("idf5.5_py*_env"):
        python_exe = entry / "Scripts" / "python.exe"
        if not python_exe.exists():
            continue
        match = re.fullmatch(r"idf5\.5_py(\d+)\.(\d+)_env", entry.name)
        if match is None:
            continue
        version_key = (int(match.group(1)), int(match.group(2)))
        candidates.append((version_key, entry))

    if not candidates:
        die(
            "No ESP-IDF 5.5 Python environment was found under "
            f"{python_env_base}. Run install.bat for ESP-IDF 5.5.4 first."
        )

    return max(candidates, key=lambda item: item[0])[1]


def read_device_target(device_id: str) -> str:
    device_properties_path = REPO_ROOT / "Devices" / device_id / "device.properties"
    if not device_properties_path.exists():
        die(f"Device file not found: {device_properties_path}")

    parser = configparser.RawConfigParser()
    parser.optionxform = str
    parser.read(device_properties_path, encoding="utf-8")

    if "hardware" not in parser or "target" not in parser["hardware"]:
        die(f"Could not find [hardware] target in {device_properties_path}")

    return parser["hardware"]["target"].strip().lower()


def build_clean_env(idf_tools_path: Path, idf_path: Path, idf_python_env: Path, idf_target: str) -> tuple[dict[str, str], Path]:
    idf_python = idf_python_env / "Scripts" / "python.exe"
    idf_tools_py = idf_path / "tools" / "idf_tools.py"
    host_path = os.environ.get("PATH", "")

    if not idf_path.exists():
        die(f"ESP-IDF path not found: {idf_path}")
    if not idf_python.exists():
        die(f"ESP-IDF Python not found: {idf_python}")
    if not idf_tools_py.exists():
        die(f"idf_tools.py not found: {idf_tools_py}")

    env = os.environ.copy()
    for key in ENV_KEYS_TO_CLEAR:
        env.pop(key, None)

    env["IDF_TOOLS_PATH"] = str(idf_tools_path)
    env["IDF_PATH"] = str(idf_path)
    env["IDF_PYTHON_ENV_PATH"] = str(idf_python_env)
    env["IDF_TARGET"] = idf_target
    env["PYTHONNOUSERSITE"] = "1"
    env["PATH"] = os.pathsep.join([str(idf_python.parent), env.get("PATH", "")])

    extra_paths = os.pathsep.join(str(idf_path / path) for path in EXPORT_EXTRA_PATHS)
    export_result = run(
        [
            str(idf_python),
            str(idf_tools_py),
            "export",
            "--format",
            "key-value",
            "--add_paths_extras",
            extra_paths,
        ],
        env=env,
        capture_output=True,
    )

    for line in export_result.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        env[key] = value

    env["PATH"] = os.pathsep.join(
        part for part in (env.get("PATH", ""), host_path) if part
    )

    return env, idf_python


def configure_device(idf_python: Path, env: dict[str, str], device_id: str) -> None:
    run([str(idf_python), str(REPO_ROOT / "device.py"), device_id], env=env)


def run_idf(
    idf_python: Path,
    idf_path: Path,
    env: dict[str, str],
    build_dir: str,
    action: str,
    port: str | None,
) -> None:
    idf_py = idf_path / "tools" / "idf.py"
    command = [str(idf_python), str(idf_py), "-B", build_dir]

    if action in {"flash", "monitor"}:
        if not port:
            die(f"--port is required for action '{action}'")
        command.extend(["-p", port])

    command.append(action)
    run(command, env=env)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run ESP-IDF 5.5.4 commands for this repository without depending on the caller's shell state."
    )
    parser.add_argument("action", choices=("doctor", "build", "flash", "monitor", "fullclean"))
    parser.add_argument("--device", required=True, help="Device id, for example lilygo-tdeck or m5stack-tab5")
    parser.add_argument("--build-dir", help="Build directory, for example build-tdeck")
    parser.add_argument("--port", help="Serial port used for flash/monitor, for example COM7")
    parser.add_argument("--idf-tools-path", default=str(DEFAULT_IDF_TOOLS_PATH))
    parser.add_argument("--idf-path", default=str(DEFAULT_IDF_PATH))
    parser.add_argument("--idf-python-env", help="Optional explicit ESP-IDF 5.5 Python env path")
    args = parser.parse_args()

    idf_tools_path = Path(args.idf_tools_path)
    idf_path = Path(args.idf_path)
    python_env_base = idf_tools_path / "python_env"
    idf_python_env = Path(args.idf_python_env) if args.idf_python_env else find_python_env(python_env_base)
    idf_target = read_device_target(args.device)

    if args.action != "doctor" and not args.build_dir:
        die("--build-dir is required for build, flash, monitor, and fullclean")

    print(f"Using device: {args.device}")
    print(f"Using target: {idf_target}")
    print(f"Using ESP-IDF: {idf_path}")
    print(f"Using ESP-IDF Python: {idf_python_env}")

    env, idf_python = build_clean_env(idf_tools_path, idf_path, idf_python_env, idf_target)

    if args.action == "doctor":
        run([str(idf_python), str(idf_path / "tools" / "idf.py"), "--version"], env=env)
        return

    configure_device(idf_python, env, args.device)
    run_idf(idf_python, idf_path, env, args.build_dir, args.action, args.port)


if __name__ == "__main__":
    main()
