"""
PlatformIO pre-build script: inject CROSSPOINT_VERSION for dynamic build environments.

Environments handled:
  default    → "<base_version>-dev+<branch>.<short_sha>"  (e.g. "1.1.1-dev+fork-drift.a1b2c3d")
  gh_latest  → "<commit_count>-dev"  (git commit count, capped at 5 digits)
  gh_nightly → "<YYYYMMDD>"         (UTC build date, used for the 'nightly' OTA channel)

All other environments (gh_release, gh_release_rc, slim, custom, …) define
CROSSPOINT_VERSION statically in platformio.ini and are left untouched.

CI can override the computed values via environment variables:
  GIT_COMMIT_COUNT  - integer commit count (overrides git rev-list output)
  BUILD_DATE        - YYYYMMDD string     (overrides current UTC date)
  BUILD_TIMESTAMP   - Unix epoch seconds  (overrides build-time stamp for all envs)
  SOURCE_DATE_EPOCH - Unix epoch seconds  (reproducible-build convention, used when BUILD_TIMESTAMP unset)
"""

Import("env")  # noqa: F821 – PlatformIO SCons global

import configparser
import datetime
import os
import subprocess
import sys

DYNAMIC_ENVS = {
    "default": "local_dev",
    "gh_latest": "commit_dev",
    "gh_nightly": "date",
}

env_name = env["PIOENV"]  # noqa: F821


def get_commit_count() -> str:
    if "GIT_COMMIT_COUNT" in os.environ:
        count = int(os.environ["GIT_COMMIT_COUNT"])
    else:
        try:
            result = subprocess.run(
                ["git", "rev-list", "--count", "HEAD"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            count = int(result.stdout.strip()) if result.returncode == 0 else 0
        except Exception:
            count = 0
    return str(min(count, 99999))  # 5 digits max


def get_build_date() -> str:
    if "BUILD_DATE" in os.environ:
        return os.environ["BUILD_DATE"]
    return datetime.datetime.utcnow().strftime("%Y%m%d")


def get_build_timestamp() -> int:
    if "BUILD_TIMESTAMP" in os.environ:
        return int(os.environ["BUILD_TIMESTAMP"])
    if "SOURCE_DATE_EPOCH" in os.environ:
        return int(os.environ["SOURCE_DATE_EPOCH"])
    return int(datetime.datetime.utcnow().replace(microsecond=0).timestamp())


def inject_build_timestamp() -> None:
    timestamp = get_build_timestamp()
    defines = env.get("CPPDEFINES", [])  # noqa: F821
    defines = [d for d in defines if "CROSSPOINT_BUILD_TIMESTAMP" not in str(d)]
    env.Replace(CPPDEFINES=defines)  # noqa: F821
    env.Append(CPPDEFINES=[("CROSSPOINT_BUILD_TIMESTAMP", f"{timestamp}UL")])  # noqa: F821
    print(f">> gen_version [{env_name}]: CROSSPOINT_BUILD_TIMESTAMP={timestamp}")


def get_git_branch() -> str:
    try:
        branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            text=True,
            stderr=subprocess.PIPE,
        ).strip()
        if branch == "HEAD":
            branch = subprocess.check_output(
                ["git", "rev-parse", "--short", "HEAD"],
                text=True,
                stderr=subprocess.PIPE,
            ).strip()
        # Strip characters that would break a C string literal
        return "".join(c for c in branch if c not in '"\\')
    except Exception as e:
        print(f"WARNING [gen_version.py]: git branch failed: {e}", file=sys.stderr)
        return "unknown"


def get_git_short_sha() -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short=7", "HEAD"],
            text=True,
            stderr=subprocess.PIPE,
        ).strip()
    except Exception:
        return ""


def get_base_version() -> str:
    project_dir = env.get("PROJECT_DIR")  # noqa: F821
    if not project_dir:
        script_path = globals().get("__file__")
        if script_path:
            project_dir = os.path.dirname(os.path.dirname(os.path.abspath(script_path)))
        else:
            project_dir = os.getcwd()
    ini_path = os.path.join(project_dir, "platformio.ini")
    if not os.path.isfile(ini_path):
        return "0.0.0"
    config = configparser.ConfigParser()
    config.read(ini_path)
    if not config.has_option("crosspoint", "version"):
        return "0.0.0"
    return config.get("crosspoint", "version")


inject_build_timestamp()

if env_name not in DYNAMIC_ENVS:
    Return()  # noqa: F821

kind = DYNAMIC_ENVS[env_name]
if kind == "local_dev":
    base = get_base_version()
    branch = get_git_branch()
    sha = get_git_short_sha()
    suffix = f"{branch}.{sha}" if sha else branch
    version = f"{base}-dev+{suffix}"
elif kind == "commit_dev":
    version = f"{get_commit_count()}-dev"
else:
    version = get_build_date()

defines = env.get("CPPDEFINES", [])  # noqa: F821
defines = [d for d in defines if "CROSSPOINT_VERSION" not in str(d)]
env.Replace(CPPDEFINES=defines)  # noqa: F821
env.Append(CPPDEFINES=[("CROSSPOINT_VERSION", f'\\"{version}\\"')])  # noqa: F821

print(f">> gen_version [{env_name}]: CROSSPOINT_VERSION={version}")
