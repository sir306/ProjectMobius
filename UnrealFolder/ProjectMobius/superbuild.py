#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Project Mobius superbuild -- one cross-platform command to build every native dependency
the Unreal project needs before it will compile.

    python superbuild.py                 # Windows, macOS and Linux
    python superbuild.py --rebuild       # throw everything away and start over
    python superbuild.py --skip-ifc      # skip the long IFC++ pass

It installs, into the exact on-disk layouts UnrealBuildTool reads:

    Assimp           -> Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp/{include,lib,bin}
    HDF5             -> Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0/install/{include,lib}
    MobiusIfcBridge  -> Source/ThirdParty/MobiusIfcLibrary/install/{include,lib,bin}   (Win64 only)

None of those outputs are committed -- the install trees are gitignored in full -- so a fresh
checkout MUST run this once before opening the .uproject. Skipping it produces UnrealBuildTool
errors of the form:

    Unable to instantiate module 'MobiusIfcLibrary': MobiusIfcLibrary lib dir not found:
    ...\\Source\\ThirdParty\\MobiusIfcLibrary\\install\\lib. Run the CMake install step.


DO I NEED THIS SCRIPT, OR CAN I JUST RUN CMAKE?
-----------------------------------------------
Plain CMake works and is fully supported:

    cmake --fresh -S . -B _superbuild -DCMAKE_BUILD_TYPE=Release
    cmake --build _superbuild --config Release --parallel

`--fresh` (CMake 3.24+) is the important half: it discards the existing cache and reconfigures,
which is what you want after switching machines, compilers or Visual Studio versions. Note there
is deliberately no `-G` there -- CMake picks the newest installed Visual Studio on Windows by
itself, and the platform default for VS 2019+ generators is already the host architecture. Naming
a generator is what used to break this project: every doc hardcoded "Visual Studio 17 2022", and
on a machine with a different Visual Studio that fails with

    CMake Error: Generator "Visual Studio 17 2022" could not find any instance of Visual Studio.

which never tells you what IS installed.

This script adds five things CMake cannot do on its own:

  1. Pins the MSVC toolset to 14.38.33130 when it is installed -- the toolset Unreal Engine 5.5
     itself builds with -- so the dependency DLLs link the same CRT as the engine. A toolset can
     only be chosen at configure time via -T, and the correct spelling is not obvious (see
     msvc_toolset_argument below).
  2. On macOS, does the same job for Xcode: UE 5.5 accepts only 15.2 - 16.9, and every current Mac
     ships something newer, so an in-range Xcode is selected for the run via DEVELOPER_DIR if one
     is installed (see resolve_developer_dir below). No sudo, and the machine default is untouched.
  3. Explains WHY when no usable compiler is found, listing what is installed and printing the
     exact override to type, instead of "could not find any instance of Visual Studio".
  4. Detects a build tree that cannot be reused -- generated on another machine, or for a
     different generator, toolset, Xcode or CMake version -- and reconfigures, saying which it was.
     This is the "I copied the repo and now nothing builds" case.
  5. Prints a per-dependency [ok]/[MISSING] summary at the end, naming the file UnrealBuildTool
     would fail on.

Every option is a thin pass-through to CMake, so nothing here is load-bearing magic. If this
script is ever in your way, use the two cmake commands above.
"""

from __future__ import annotations

import argparse
import os
import platform
import plistlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Directory containing this script == the Unreal project root (where CMakeLists.txt lives).
PROJECT_DIR = Path(__file__).resolve().parent

IS_WINDOWS = platform.system() == "Windows"
IS_MACOS = platform.system() == "Darwin"

# The toolset Unreal Engine 5.5 builds with. Preferred, not required: the shim's DLL boundary is a
# C ABI, so a mismatch would most likely work anyway, but matching removes an axis of divergence in
# the CRT for free.
PREFERRED_MSVC_TOOLSET = "14.38.33130"

# The macOS equivalent of the toolset pin above. Unreal Engine 5.5 accepts exactly this range of
# Xcode versions -- taken from the engine's own Engine/Config/Apple/Apple_SDK.json (UE 5.5.4:
# MainVersion 15.2, MinVersion 15.2.0, MaxVersion 16.9.0), which is what UnrealBuildTool enforces.
# It matters here because the dependency dylibs this script produces are linked into the editor:
# building them with a clang the engine itself refuses to use is the same class of mistake as
# building them with the wrong MSVC toolset. A Mac bought recently ships an Xcode well past 16.9,
# so 'whatever xcode-select points at' is the wrong default on current hardware.
UE55_XCODE_MIN = (15, 2, 0)
UE55_XCODE_MAX = (16, 9, 0)

# Exact process names that mean "an Unreal Editor is live". Matched against the process NAME, not
# the command line -- see unreal_editor_processes() for why that distinction is the whole point.
#
# The kernel truncates the recorded name (16 characters on macOS, 15 on Linux), so only names that
# fit can ever match: "UnrealEditor" (12) and, on macOS only, "UnrealEditor-Cmd" (16). Longer
# variants such as "UnrealEditor-Mac-DebugGame" are deliberately NOT listed -- they could never
# match, and listing them would imply a check that does not exist.
UNREAL_EDITOR_PROCESS_NAMES = ("UnrealEditor", "UnrealEditor-Cmd")

# Visual Studio major version -> CMake generator name. Adding a row here is the ONLY change a
# future Visual Studio release should need. (Run `cmake --help` to see the exact spelling your
# CMake supports.)
VS_MAJOR_TO_GENERATOR = {
    15: "Visual Studio 15 2017",
    16: "Visual Studio 16 2019",
    17: "Visual Studio 17 2022",
    18: "Visual Studio 18 2026",
}

# Files that must exist when the superbuild has done its job. Shown as the closing summary; each is
# a path UnrealBuildTool itself probes, so a [MISSING] line here is a UBT failure you have not hit
# yet. Keyed by dependency so --skip-* runs can be read honestly.
EXPECTED_ARTIFACTS = [
    ("assimp", "Assimp headers",
     "Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp/include/assimp/version.h"),
    ("assimp", "Assimp import lib",
     "Plugins/UE4_Assimp/Source/ThirdParty/UE_AssimpLibrary/assimp/lib"),
    ("hdf5", "HDF5 headers",
     "Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0/install/include/hdf5.h"),
    ("hdf5", "HDF5 libs",
     "Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0/install/lib"),
    ("ifc", "IFC bridge header",
     "Source/ThirdParty/MobiusIfcLibrary/install/include/MobiusIfcBridge.h"),
]

# The IFC bridge's binary artifact differs by platform: Win64 produces an import lib + DLL, macOS a
# single .dylib (no import lib). List only what the current platform actually produces, so the closing
# summary neither misses the real file nor demands a Windows .lib on a Mac. Linux is not wired (the
# summary loop skips IFC there entirely).
if IS_WINDOWS:
    EXPECTED_ARTIFACTS += [
        ("ifc", "IFC bridge import lib",
         "Source/ThirdParty/MobiusIfcLibrary/install/lib/MobiusIfcBridge.lib"),
        ("ifc", "IFC bridge DLL",
         "Source/ThirdParty/MobiusIfcLibrary/install/bin/MobiusIfcBridge.dll"),
    ]
elif IS_MACOS:
    EXPECTED_ARTIFACTS += [
        ("ifc", "IFC bridge dylib",
         "Source/ThirdParty/MobiusIfcLibrary/install/bin/libMobiusIfcBridge.dylib"),
    ]


# ---------------------------------------------------------------------------------------------
# Small output helpers. Colour is opt-out via the NO_COLOR convention, and off entirely when the
# output is redirected to a file or a CI log.
# ---------------------------------------------------------------------------------------------
_USE_COLOUR = sys.stdout.isatty() and "NO_COLOR" not in os.environ


def _paint(text: str, code: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _USE_COLOUR else text


def step(text: str) -> None:
    print("\n" + _paint(f"==> {text}", "36;1"))


def note(text: str) -> None:
    print(f"    {text}")


def warn(text: str) -> None:
    print(_paint(f"WARNING: {text}", "33"))


def fail(text: str) -> "None":
    """Print a multi-line explanation and exit non-zero.

    Deliberately not an exception: these are user-facing setup problems with a known remedy, and a
    Python traceback on top of the remedy is pure noise.
    """
    print(_paint("\nERROR: " + text, "31;1"), file=sys.stderr)
    sys.exit(1)


def run(command: list[str], *, cwd: Path | None = None) -> int:
    """Run a command with its output attached to our own stdout/stderr, and return its exit code.

    Streaming (rather than capturing) matters here: the IFC++ pass runs for tens of minutes and a
    silent script looks hung.
    """
    note(" ".join(str(part) for part in command))
    return subprocess.call([str(part) for part in command], cwd=str(cwd) if cwd else None)


# ---------------------------------------------------------------------------------------------
# CMake discovery
# ---------------------------------------------------------------------------------------------
def find_cmake() -> str:
    cmake = shutil.which("cmake")
    if not cmake:
        fail(
            "cmake was not found on PATH.\n\n"
            "  Install CMake 3.21 or newer from https://cmake.org/download/.\n"
            "  On Windows tick 'Add CMake to the system PATH' in the installer.\n"
            "  On macOS: brew install cmake"
        )
    return cmake


def cmake_version(cmake: str) -> tuple[int, ...]:
    """Return the running CMake version as a tuple, e.g. (4, 4, 0).

    The first line looks like 'cmake version 4.4.0-rc2'; the release-candidate suffix has to be
    stripped before int() sees it.
    """
    first_line = subprocess.check_output([cmake, "--version"], text=True).splitlines()[0]
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", first_line)
    if not match:
        fail(f"Could not parse the CMake version from: {first_line!r}")
    return tuple(int(group) for group in match.groups())


# ---------------------------------------------------------------------------------------------
# Visual Studio discovery (Windows only)
# ---------------------------------------------------------------------------------------------
def find_vswhere() -> str | None:
    """vswhere.exe ships with the Visual Studio Installer and is the only supported way to locate
    Visual Studio. Its path is fixed by Microsoft, so this is a lookup, not a search."""
    for env_var in ("ProgramFiles(x86)", "ProgramFiles"):
        root = os.environ.get(env_var)
        if not root:
            continue
        candidate = Path(root) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        if candidate.is_file():
            return str(candidate)
    return None


def query_vswhere(vswhere: str, prop: str, *, require_cxx: bool) -> list[str]:
    """Ask vswhere for one property, one value per line.

    NOTE: vswhere's -property takes exactly ONE name. Passing two ('-property a b') makes it exit
    87 'Argument expected', which reads downstream as 'no Visual Studio found' on a machine that
    plainly has one. Hence one call per property.
    """
    command = [vswhere, "-products", "*", "-format", "value", "-property", prop]
    if require_cxx:
        # -latest alone can select an install with no C++ compiler in it.
        command[1:1] = ["-latest"]
        command += ["-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"]
    try:
        output = subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError:
        return []
    return [line.strip() for line in output.stdout.splitlines() if line.strip()]


class VisualStudio:
    def __init__(self, install_path: str, version: str, generator: str):
        self.install_path = install_path
        self.version = version
        self.generator = generator

    def installed_toolsets(self) -> list[str]:
        """MSVC toolset versions present in this install, oldest first."""
        tools_root = Path(self.install_path) / "VC" / "Tools" / "MSVC"
        if not tools_root.is_dir():
            return []
        return sorted(entry.name for entry in tools_root.iterdir() if entry.is_dir())


_vs_cache: list = []  # one-element memo; vswhere is slow enough to be worth not calling twice


def detect_visual_studio() -> VisualStudio | None:
    if _vs_cache:
        return _vs_cache[0]

    result = None
    vswhere = find_vswhere()
    if vswhere:
        paths = query_vswhere(vswhere, "installationPath", require_cxx=True)
        versions = query_vswhere(vswhere, "installationVersion", require_cxx=True)
        if paths and versions:
            major = int(versions[0].split(".")[0])
            generator = VS_MAJOR_TO_GENERATOR.get(major)
            if generator:
                result = VisualStudio(paths[0], versions[0], generator)

    _vs_cache.append(result)
    return result


def explain_no_visual_studio() -> str:
    """Build the message shown when detection fails.

    There are three distinct failure modes with three different fixes, and saying only 'no Visual
    Studio found' when one is installed is the single most time-wasting message this tooling could
    produce. So: say which case it is, list what IS installed, and print the literal override.
    """
    supported = "\n".join(
        f"      {name}   (VS major {major})" for major, name in sorted(VS_MAJOR_TO_GENERATOR.items())
    )
    override = "      python superbuild.py --generator 'Visual Studio 17 2022'"

    vswhere = find_vswhere()
    if not vswhere:
        return (
            "No Visual Studio could be detected: vswhere.exe is not installed.\n\n"
            "  vswhere ships with the Visual Studio Installer, so this normally means Visual\n"
            "  Studio itself is not installed. Install Visual Studio 2022 or 2026 with the\n"
            "  'Desktop development with C++' workload.\n\n"
            "  If Visual Studio IS installed, name the generator yourself:\n\n"
            f"{override}\n\n  Supported generators:\n" + supported
        )

    all_paths = query_vswhere(vswhere, "installationPath", require_cxx=False)
    all_versions = query_vswhere(vswhere, "installationVersion", require_cxx=False)

    if not all_paths:
        return (
            "No Visual Studio installation was found at all (vswhere returned nothing).\n\n"
            "  Install Visual Studio 2022 or 2026 with the 'Desktop development with C++'\n"
            "  workload. To override detection anyway:\n\n"
            f"{override}\n\n  Supported generators:\n" + supported
        )

    listing = "\n".join(
        f"      {all_versions[i] if i < len(all_versions) else '?'}  {path}"
        for i, path in enumerate(all_paths)
    )
    majors = [int(v.split(".")[0]) for v in all_versions if v.split(".")[0].isdigit()]
    unknown = sorted({m for m in majors if m not in VS_MAJOR_TO_GENERATOR})

    if unknown and not any(m in VS_MAJOR_TO_GENERATOR for m in majors):
        return (
            "The installed Visual Studio is newer than this script knows about.\n\n"
            f"  Installed (version, path):\n{listing}\n\n"
            f"  Unrecognised major version(s): {', '.join(str(m) for m in unknown)}\n\n"
            "  Fix permanently: add a row to VS_MAJOR_TO_GENERATOR near the top of\n"
            "      superbuild.py\n"
            "  mapping that major version to its CMake generator name (`cmake --help` lists the\n"
            "  exact spellings your CMake supports).\n\n"
            "  Work around it now:\n\n"
            "      python superbuild.py --generator '<name from cmake --help>'\n\n"
            "  Supported generators:\n" + supported
        )

    return (
        "Visual Studio is installed, but no installation has the C++ toolset.\n\n"
        f"  Installed (version, path):\n{listing}\n\n"
        "  None carries the component\n"
        "      Microsoft.VisualStudio.Component.VC.Tools.x86.x64\n\n"
        "  Fix: Visual Studio Installer -> Modify -> Workloads -> tick\n"
        "  'Desktop development with C++'. Under Individual components, 'MSVC v143 14.38.33130'\n"
        "  is also worth ticking; it is the toolset Unreal Engine 5.5 itself builds with.\n\n"
        "  To try anyway with an explicit generator:\n\n"
        f"{override} --toolset none\n\n  Supported generators:\n" + supported
    )


def resolve_toolset(requested: str) -> str:
    """Pick an MSVC toolset version.

    requested == 'none' (or '')  -> no -T argument; use whatever the generator defaults to
    requested == 'auto'          -> PREFERRED_MSVC_TOOLSET if installed, else newest installed
    anything else                -> that exact version, failing if it is not installed
    """
    if requested in ("", "none"):
        return ""

    vs = detect_visual_studio()
    installed = vs.installed_toolsets() if vs else []

    if requested != "auto":
        if installed and requested not in installed:
            fail(
                f"Requested MSVC toolset {requested} is not installed.\n\n"
                f"  Installed: {', '.join(installed)}\n\n"
                "  Pass --toolset with one of those, --toolset auto to choose automatically,\n"
                "  or --toolset none to use the generator default."
            )
        return requested

    if PREFERRED_MSVC_TOOLSET in installed:
        return PREFERRED_MSVC_TOOLSET

    if installed:
        newest = installed[-1]
        warn(
            f"MSVC toolset {PREFERRED_MSVC_TOOLSET} (the one UE 5.5 builds with) is not installed; "
            f"using {newest}. Installed: {', '.join(installed)}. Install {PREFERRED_MSVC_TOOLSET} "
            "via the Visual Studio Installer -> Individual components for an exact ABI match."
        )
        return newest

    warn("Could not enumerate installed MSVC toolsets; letting the generator pick its default.")
    return ""


def msvc_toolset_argument(toolset: str) -> str:
    """Turn an MSVC version (14.38.33130) into the full CMake -T value (v143,version=14.38.33130).

    The version alone is NOT enough, and this is the non-obvious bit. `-T version=14.38.33130`
    under the "Visual Studio 18 2026" generator fails with

        given toolset and version specification v145,version=14.38.33130
        contains an invalid version specification

    because CMake pairs the requested version with the generator's DEFAULT platform toolset (v145),
    and 14.38 belongs to v143. Naming the platform toolset explicitly resolves it. Note there is no
    v144: VS2022 spans 14.30-14.4x on v143, and VS2026 starts v145 at 14.5x.
    """
    if not toolset:
        return ""
    parts = toolset.split(".")
    if len(parts) < 2 or parts[0] != "14":
        return f"version={toolset}"
    minor = int(parts[1])
    if minor < 10:
        platform_toolset = "v140"
    elif minor < 20:
        platform_toolset = "v141"
    elif minor < 30:
        platform_toolset = "v142"
    elif minor < 50:
        platform_toolset = "v143"
    else:
        platform_toolset = "v145"
    return f"{platform_toolset},version={toolset}"


# ---------------------------------------------------------------------------------------------
# Xcode discovery (macOS only)
#
# The Windows half of this script pins the MSVC toolset to the one UE 5.5 builds with. This is the
# same job for macOS, and it is more pressing rather than less: Unreal Engine 5.5 hard-refuses any
# Xcode outside 15.2 - 16.9 (see UE55_XCODE_MIN/MAX above), and every Mac sold since is well past
# that, so anyone following the obvious "install Xcode, run the build" path ends up with
# dependencies compiled by a clang the engine will not use.
# ---------------------------------------------------------------------------------------------
def parse_version(text: str) -> tuple[int, int, int] | None:
    """'16.4.1 (16F7)' -> (16, 4, 1). Padded to three parts so comparisons against the UE range
    are total: bare '16' must compare as 16.0.0, not sort short."""
    parts = [int(number) for number in re.findall(r"\d+", text)[:3]]
    if not parts:
        return None
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts)  # type: ignore[return-value]


def format_version(version: tuple[int, ...]) -> str:
    return ".".join(str(part) for part in version)


def xcode_version(developer_dir: Path) -> tuple[int, int, int] | None:
    """Version of the Xcode owning this Developer directory, or None if it is not one.

    DEVELOPER_DIR points at <Xcode.app>/Contents/Developer, so the version lives one level up in
    Contents/version.plist. Reading the plist beats running `xcodebuild -version`: xcodebuild takes
    seconds to answer, and errors out entirely when only the Command Line Tools are installed --
    which is precisely one of the cases this needs to report clearly rather than crash on. It also
    means a Command Line Tools directory (/Library/Developer/CommandLineTools, no version.plist
    beside it) correctly returns None instead of a bogus version.
    """
    try:
        with (developer_dir.parent / "version.plist").open("rb") as handle:
            data = plistlib.load(handle)
    except (OSError, ValueError):
        return None
    return parse_version(str(data.get("CFBundleShortVersionString", "")))


def active_developer_dir() -> Path | None:
    """The Xcode (or Command Line Tools) directory the machine is currently set to use.

    DEVELOPER_DIR in the environment wins over xcode-select, matching how every Apple tool resolves
    it -- so a caller who already exports it does not get quietly overruled by this script.
    """
    from_env = os.environ.get("DEVELOPER_DIR")
    if from_env:
        return Path(from_env)
    try:
        output = subprocess.check_output(["xcode-select", "-p"], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    return Path(output) if output else None


def installed_xcodes() -> list[tuple[tuple[int, int, int], Path]]:
    """Every Xcode under /Applications as (version, Developer dir), newest first.

    /Applications is the only location Apple's own tooling assumes, and side-by-side installs there
    (Xcode_15.4.app alongside Xcode.app) are the normal way to keep an older Xcode for a project
    that needs one -- which is exactly this project's situation.
    """
    found = []
    for app in Path("/Applications").glob("Xcode*.app"):
        developer = app / "Contents" / "Developer"
        version = xcode_version(developer)
        if version and developer.is_dir():
            found.append((version, developer))
    return sorted(found, reverse=True)


def xcode_is_supported(version: tuple[int, int, int]) -> bool:
    return UE55_XCODE_MIN <= version <= UE55_XCODE_MAX


def explain_no_xcode() -> str:
    """Message for 'no full Xcode is installed'. Says which of the two cases it is, because the
    remedies differ, and always ends with the one-line escape hatch."""
    active = active_developer_dir()
    if active and "CommandLineTools" in str(active):
        situation = (f"Only the Command Line Tools are installed ({active}).\n\n"
                     "  They can compile these dependencies, but Unreal Engine 5.5 itself needs a\n"
                     "  full Xcode, so you will hit this again when you open the project.\n\n")
    else:
        situation = "No Xcode installation was found under /Applications.\n\n"

    return (
        situation +
        f"  Unreal Engine 5.5 accepts Xcode {format_version(UE55_XCODE_MIN)} to "
        f"{format_version(UE55_XCODE_MAX)} inclusive\n"
        "  (its own Engine/Config/Apple/Apple_SDK.json). Older releases are at\n"
        "      https://developer.apple.com/download/all/?q=xcode\n"
        "  Install one, then either select it machine-wide:\n\n"
        "      sudo xcode-select -s /Applications/Xcode_16.2.app\n\n"
        "  or point this build at it without changing the machine default:\n\n"
        "      python3 superbuild.py --xcode /Applications/Xcode_16.2.app\n\n"
        "  To build the dependencies with the current toolchain regardless:\n\n"
        "      python3 superbuild.py --xcode none"
    )


def resolve_developer_dir(requested: str) -> Path | None:
    """Pick the Xcode whose clang builds the dependencies, as a Developer directory to export via
    DEVELOPER_DIR -- or None to leave the environment untouched.

    requested == 'none'  -> leave it alone; use whatever xcode-select points at
    requested == 'auto'  -> the active Xcode if UE 5.5 accepts it, otherwise the newest installed
                            Xcode that it does accept
    anything else        -> that path, an Xcode.app or a Developer dir, used as given

    Mirrors resolve_toolset() on the Windows side deliberately, including where it draws the line
    between failing and warning: no usable compiler at all is fatal, but a version outside the
    preferred range only warns, because it will very likely still produce working libraries.
    """
    if requested in ("", "none"):
        note("Xcode selection skipped (--xcode none); using whatever xcode-select points at.")
        return None

    if requested != "auto":
        # Resolve to an absolute path immediately. This value ends up in DEVELOPER_DIR, which is
        # inherited by cmake and every compiler probe it spawns -- and those run from deeply nested
        # scratch dirs (CMakeFiles/CMakeScratch/TryCompile-*). A relative --xcode resolves fine here
        # (against the repo root) but points at nothing from there, so xcrun dies with
        # "missing DEVELOPER_DIR path: ../../../Xcode.app/Contents/Developer".
        chosen = Path(requested).expanduser().resolve()
        # Accept either spelling: people copy the .app path far more readily than the Developer
        # subdirectory buried inside it.
        if chosen.suffix == ".app":
            chosen = chosen / "Contents" / "Developer"
        if not chosen.is_dir():
            fail(f"--xcode {requested}: no such directory ({chosen}).\n\n"
                 "  Pass the path to an Xcode.app, e.g. /Applications/Xcode_16.2.app")
        version = xcode_version(chosen)
        if version and not xcode_is_supported(version):
            warn(f"Xcode {format_version(version)} at {chosen} is outside the range UE 5.5 accepts "
                 f"({format_version(UE55_XCODE_MIN)}-{format_version(UE55_XCODE_MAX)}). Using it "
                 "because you named it explicitly.")
        return chosen

    active = active_developer_dir()
    active_version = xcode_version(active) if active else None

    if active_version and xcode_is_supported(active_version):
        note(f"Xcode {format_version(active_version)} ({active}) -- accepted by UE 5.5.")
        return None  # Already correct: do not perturb the environment for no reason.

    candidates = [entry for entry in installed_xcodes() if xcode_is_supported(entry[0])]
    if candidates:
        version, developer = candidates[0]
        if active_version:
            warn(f"The active Xcode is {format_version(active_version)} ({active}), outside the "
                 f"range UE 5.5 accepts ({format_version(UE55_XCODE_MIN)}-"
                 f"{format_version(UE55_XCODE_MAX)}).")
        note(f"Building with Xcode {format_version(version)} instead ({developer}).")
        note("This run only -- the machine default is unchanged. Make it permanent with:")
        note(f"    sudo xcode-select -s {developer.parent.parent}")
        return developer

    if active_version:
        # An Xcode is installed and usable, just newer than the engine supports. Warn rather than
        # stop: it will almost certainly build these libraries, and refusing here would block the
        # only Xcode the machine has.
        warn(f"Xcode {format_version(active_version)} ({active}) is newer than UE 5.5 supports "
             f"({format_version(UE55_XCODE_MIN)}-{format_version(UE55_XCODE_MAX)}) and no "
             "in-range Xcode is installed under /Applications. Building the dependencies with it "
             "anyway. Unreal Engine itself will refuse this version, so install an in-range Xcode "
             "before opening the project: https://developer.apple.com/download/all/?q=xcode")
        return None

    fail(explain_no_xcode())
    return None  # unreachable; fail() exits


# ---------------------------------------------------------------------------------------------
# Build-tree hygiene
# ---------------------------------------------------------------------------------------------
def read_cache_entry(cache_path: Path, key: str) -> str | None:
    """Read one CMakeCache.txt entry. Lines look like KEY:TYPE=VALUE."""
    pattern = re.compile(rf"^{re.escape(key)}:[^=]*=(.*)$")
    try:
        with cache_path.open(encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = pattern.match(line.rstrip("\n"))
                if match:
                    return match.group(1)
    except OSError:
        return None
    return None


def stale_reason(cache_path: Path, generator: str, toolset_arg: str,
                 running_cmake: tuple[int, ...], developer_dir: Path | None = None) -> str | None:
    """Return why the existing build tree cannot be reused, or None if it can.

    Each of these otherwise produces a CMake error that reads like a project fault rather than a
    stale-directory fault, which is exactly how "I copied the repo across and now nothing builds"
    turns into an afternoon.
    """
    cached_source = read_cache_entry(cache_path, "CMAKE_HOME_DIRECTORY")
    if cached_source:
        # Compare resolved paths so D:\x, D:/x and a trailing slash all agree.
        try:
            same = Path(cached_source).resolve() == PROJECT_DIR
        except OSError:
            same = False
        if not same:
            return (f"it was generated for a different source directory ({cached_source!r}). "
                    "This is what a build tree copied in from another machine looks like.")

    cached_generator = read_cache_entry(cache_path, "CMAKE_GENERATOR")
    if cached_generator and cached_generator != generator:
        return f"the generator changed ({cached_generator!r} -> {generator!r})."

    # A toolset change matters as much as a generator change: CMake refuses to reuse the tree, and
    # the dependency filenames themselves move (assimp-vc143-mt.dll vs assimp-vc145-mt.dll).
    cached_toolset = read_cache_entry(cache_path, "CMAKE_GENERATOR_TOOLSET") or ""
    if cached_toolset != toolset_arg:
        shown_old = cached_toolset or "<generator default>"
        shown_new = toolset_arg or "<generator default>"
        return f"the MSVC toolset changed ({shown_old!r} -> {shown_new!r})."

    # The macOS counterpart of the toolset check. CMAKE_CXX_COMPILER is no use for this: CMake
    # usually records the /usr/bin/c++ shim, whose path is identical for every Xcode. The SDK
    # sysroot is the entry that actually moves, since it lives inside the Developer directory --
    # so a tree configured against a different Xcode is caught here rather than silently reused
    # with the other clang's headers.
    if developer_dir is not None:
        cached_sysroot = read_cache_entry(cache_path, "CMAKE_OSX_SYSROOT") or ""
        # CMAKE_OSX_SYSROOT is allowed to hold a bare SDK NAME ('macosx') rather than a path, and
        # that form says nothing about which Xcode owns it. Comparing it would wipe a perfectly
        # good build tree on every single run, so only an absolute path is evidence here.
        if cached_sysroot and Path(cached_sysroot).is_absolute():
            try:
                # Resolve both sides before comparing: a trailing slash, or /Applications/Xcode.app
                # being a symlink, must not read as a different Xcode. strict=False, because an SDK
                # path inherited from another machine legitimately does not exist here.
                cached_path = Path(cached_sysroot).resolve()
                owner = developer_dir.resolve()
                under = cached_path == owner or owner in cached_path.parents
            except OSError:
                under = True  # Cannot tell -- do not throw the tree away on a guess.
            if not under:
                return (f"it was configured against a different Xcode (SDK {cached_sysroot!r} is "
                        f"not under {str(developer_dir)!r}).")

    major = read_cache_entry(cache_path, "CMAKE_CACHE_MAJOR_VERSION")
    minor = read_cache_entry(cache_path, "CMAKE_CACHE_MINOR_VERSION")
    if major and minor and (int(major), int(minor)) != running_cmake[:2]:
        return (f"it was written by CMake {major}.{minor} and you are running "
                f"{running_cmake[0]}.{running_cmake[1]}.")

    return None


def unreal_editor_processes() -> list[str]:
    """Return running Unreal Editor processes, so we can refuse to fight one for a file lock. The
    Assimp stage writes into Plugins/UE4_Assimp/Binaries/<Platform>, which a live editor holds
    open, and rebuilding a dependency under a live editor can also trigger a hot-reload restart.

    The match must be on the process NAME. This used to run `pgrep -f UnrealEditor`, and -f matches
    the full COMMAND LINE of every process -- so anything that merely mentions the editor counted:
    a CrashReportClient still showing a report from a previous run, a Finder/LaunchServices helper
    registered for .uproject files, an editor path sitting in some other tool's arguments. The
    result was a macOS machine with no editor open being told the editor was open, on every run.
    `pgrep -x` matches the name exactly, which is also why an exact list beats a prefix: a prefix
    match on "UnrealEditor" would still catch UnrealEditorServices.
    """
    try:
        if IS_WINDOWS:
            # tasklist matches the image name only, so it never had the problem described above.
            output = subprocess.check_output(
                ["tasklist", "/FO", "CSV", "/NH"], text=True, errors="replace")
            return [line.split(",")[0].strip('"') for line in output.splitlines()
                    if line.lower().startswith('"unrealeditor')]
    except (OSError, subprocess.CalledProcessError):
        # tasklist may be absent in odd environments. Not a reason to stop -- this check is a
        # courtesy, not a gate we can guarantee.
        return []

    found: list[str] = []
    for name in UNREAL_EDITOR_PROCESS_NAMES:
        try:
            output = subprocess.check_output(["pgrep", "-x", name], text=True)
        except (OSError, subprocess.CalledProcessError):
            continue  # pgrep exits 1 when nothing matches, which is the normal case.
        # Report the pid too: if this ever fires wrongly again, the message names something the
        # reader can look up directly instead of a bare process name they have to go hunting for.
        found += [f"{name} (pid {pid.strip()})" for pid in output.splitlines() if pid.strip()]
    return found


# ---------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------
def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build every native dependency Project Mobius needs.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Equivalent raw CMake:\n"
               "  cmake --fresh -S . -B _superbuild -DCMAKE_BUILD_TYPE=Release\n"
               "  cmake --build _superbuild --config Release --parallel\n")
    parser.add_argument("--config", default="Release", choices=["Release", "Debug"],
                        help="Build configuration (default: Release).")
    parser.add_argument("--generator", default="auto",
                        help="CMake generator. 'auto' detects Visual Studio on Windows and lets "
                             "CMake choose elsewhere. Pass a name to override.")
    parser.add_argument("--toolset", default="auto",
                        help="MSVC toolset version. 'auto' prefers %s, 'none' uses the generator "
                             "default, or give an exact version. Windows only."
                             % PREFERRED_MSVC_TOOLSET)
    parser.add_argument("--xcode", default="auto",
                        help="Xcode to build with. 'auto' uses the active one if UE 5.5 accepts "
                             "it (%s-%s) and otherwise substitutes an installed Xcode that it "
                             "does, 'none' uses whatever xcode-select points at, or give a path "
                             "to an Xcode.app. macOS only."
                             % (format_version(UE55_XCODE_MIN), format_version(UE55_XCODE_MAX)))
    parser.add_argument("--build-dir", default="_superbuild",
                        help="CMake build tree (default: _superbuild).")
    parser.add_argument("--clean", action="store_true",
                        help="Delete the build tree first. Installed dependencies are kept.")
    parser.add_argument("--rebuild", action="store_true",
                        help="--clean plus: delete the installed dependency trees too, so "
                             "everything is rebuilt from scratch.")
    parser.add_argument("--skip-assimp", action="store_true", help="Do not build Assimp.")
    parser.add_argument("--skip-hdf5", action="store_true", help="Do not build HDF5.")
    parser.add_argument("--skip-ifc", action="store_true",
                        help="Do not build the IFC++ bridge (by far the longest pass). The Unreal "
                             "build will fail on MobiusIfcLibrary until it has run once.")
    parser.add_argument("--force-ifc", action="store_true",
                        help="Rebuild MobiusIfcBridge even though its outputs exist. Needed after "
                             "editing MobiusIfcBridge.cpp/.h.")
    parser.add_argument("--skip-tests", action="store_true",
                        help="Do not run the staging checks at the end of the build.")
    parser.add_argument("--allow-editor-running", action="store_true",
                        help="Proceed even though an Unreal Editor process is running.")
    return parser.parse_args()


def main() -> int:
    # Line-buffer our own output. Python block-buffers stdout when it is redirected to a file or a
    # CI log, while the cmake/MSBuild child processes we spawn write straight through -- so without
    # this the script's own headings appear at the END of a captured log, after the build output
    # they were supposed to introduce. Harmless interactively, deeply confusing in a saved log.
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except AttributeError:  # Python < 3.7
        pass

    args = parse_arguments()
    build_path = Path(args.build_dir)
    if not build_path.is_absolute():
        build_path = PROJECT_DIR / build_path

    # -- Prerequisites -------------------------------------------------------------------------
    step("Checking prerequisites")

    cmake = find_cmake()
    version = cmake_version(cmake)
    if version < (3, 21):
        fail(f"CMake {'.'.join(map(str, version))} is too old; this superbuild needs 3.21 or newer.")
    note(f"cmake {'.'.join(map(str, version))} ({cmake})")

    generator = args.generator
    toolset_arg = ""
    if IS_WINDOWS:
        if generator in ("", "auto"):
            vs = detect_visual_studio()
            if not vs:
                fail(explain_no_visual_studio())
            generator = vs.generator
            note(f"detected {vs.generator} at {vs.install_path}")
        toolset = resolve_toolset(args.toolset)
        toolset_arg = msvc_toolset_argument(toolset)
        note(f"generator {generator!r}, toolset {toolset or '<generator default>'!r}")
    else:
        # CMake's default generator (Ninja or Unix Makefiles) is correct off Windows, and -T is a
        # Visual Studio concept. On macOS the CMakeLists drives the IFC++ bridge via
        # Build-MobiusIfcBridge.sh; on Linux it skips IFC on its own (not yet ported).
        if generator == "auto":
            generator = ""
        note(f"generator {generator or '<CMake default>'!r} ({platform.system()})")

    # Which Xcode compiles the dependencies. Chosen per run via DEVELOPER_DIR rather than
    # `sudo xcode-select -s`, so this needs no privileges and leaves the machine as it found it.
    # Setting it in our own environment is enough: cmake, the compilers it drives and anything
    # they shell out to (xcrun in particular) all inherit it.
    developer_dir = resolve_developer_dir(args.xcode) if IS_MACOS else None
    if developer_dir:
        os.environ["DEVELOPER_DIR"] = str(developer_dir)

    # Used only to spot a build tree configured against a different Xcode. When resolution left
    # the environment alone there is still an active toolchain to compare against, so ask for it.
    sysroot_owner = developer_dir or (active_developer_dir() if IS_MACOS else None)

    running_editors = unreal_editor_processes()
    if running_editors and not args.allow_editor_running:
        listing = "\n".join(f"      {name}" for name in sorted(set(running_editors)))
        fail(
            f"Unreal Editor is running:\n\n{listing}\n\n"
            "  The Assimp stage writes into Plugins/UE4_Assimp/Binaries/<Platform>, which the\n"
            "  editor holds open, and rebuilding a dependency under a live editor can trigger a\n"
            "  hot-reload restart.\n\n"
            "  Close the editor, or pass --allow-editor-running if this run cannot touch it.\n\n"
            "  Those are real processes matched by name, not by command line. If none of them is\n"
            "  an editor you opened, that is a bug in this check -- please report the lines above."
        )

    # -- Build-tree hygiene --------------------------------------------------------------------
    force_ifc = args.force_ifc

    if args.rebuild:
        step("Removing installed dependency trees (--rebuild)")
        for relative in (
            "Plugins/MobiusDataImporter/Source/ThirdParty/hdf5-2.0.0/install",
            "Source/ThirdParty/MobiusIfcLibrary/install/lib",
            "Source/ThirdParty/MobiusIfcLibrary/install/bin",
            "Intermediate/IfcBridgeBuild",
        ):
            target = PROJECT_DIR / relative
            if target.exists():
                note(f"removing {target}")
                shutil.rmtree(target, ignore_errors=True)
        args.clean = True

    cache_path = build_path / "CMakeCache.txt"
    if args.clean and build_path.exists():
        step(f"Removing build tree {build_path} (--clean)")
        shutil.rmtree(build_path, ignore_errors=True)
    elif cache_path.is_file():
        reason = stale_reason(cache_path, generator, toolset_arg, version, sysroot_owner)
        if reason:
            step(f"Discarding unusable build tree {build_path}")
            note(reason)
            shutil.rmtree(build_path, ignore_errors=True)
            # Wiping the build tree drops the Assimp/HDF5 ExternalProject stamps, so those rebuild.
            # MobiusIfcBridge.dll/.lib live in the INSTALL tree, which survives, so its custom
            # command would see its outputs as up to date and silently keep a DLL built by the old
            # toolchain. Nothing on disk records which toolset produced it, so force it.
            if not args.skip_ifc and not force_ifc:
                note("Also rebuilding MobiusIfcBridge, so it matches the new toolchain.")
                force_ifc = True

    # -- Configure -----------------------------------------------------------------------------
    step(f"Configuring {build_path}")

    configure = [cmake, "-S", PROJECT_DIR, "-B", build_path]
    if generator:
        configure += ["-G", generator]
    if toolset_arg:
        configure += ["-T", toolset_arg]
    configure += [
        # Ignored by multi-config generators (Visual Studio, Xcode); load-bearing for Ninja/Make.
        f"-DCMAKE_BUILD_TYPE={args.config}",
        f"-DSUPERBUILD_BUILD_ASSIMP={'OFF' if args.skip_assimp else 'ON'}",
        f"-DSUPERBUILD_BUILD_HDF5={'OFF' if args.skip_hdf5 else 'ON'}",
        f"-DSUPERBUILD_BUILD_IFC={'OFF' if args.skip_ifc else 'ON'}",
        f"-DSUPERBUILD_IFC_FORCE={'ON' if force_ifc else 'OFF'}",
        f"-DSUPERBUILD_RUN_TESTS={'OFF' if args.skip_tests else 'ON'}",
        f"-DSUPERBUILD_IFC_CONFIG={args.config}",
        f"-DSUPERBUILD_CTEST_CONFIG={args.config}",
    ]
    if run(configure) != 0:
        fail("CMake configure failed. The first error above is the real one.")

    # -- Build ---------------------------------------------------------------------------------
    step(f"Building ({args.config})")
    if not args.skip_ifc and (IS_WINDOWS or IS_MACOS):
        note("The IFC++ pass is the long one -- expect tens of minutes on a first run.")

    if run([cmake, "--build", build_path, "--config", args.config, "--parallel"]) != 0:
        fail(
            "Superbuild failed. The first error above is the real one; later ones are usually\n"
            "  fallout. If a staging check failed, its message names the exact file\n"
            "  UnrealBuildTool will also fail to find."
        )

    # -- Summary -------------------------------------------------------------------------------
    step("Dependency status")

    skipped = {"assimp": args.skip_assimp, "hdf5": args.skip_hdf5, "ifc": args.skip_ifc}
    missing = []
    for dependency, label, relative in EXPECTED_ARTIFACTS:
        # The IFC bridge is wired for Win64 and macOS; a Linux run is complete without it (not ported).
        if dependency == "ifc" and not (IS_WINDOWS or IS_MACOS):
            continue
        present = (PROJECT_DIR / relative).exists()
        if present:
            print("  " + _paint(f"[ok]      {label:<22} {relative}", "32"))
        elif skipped[dependency]:
            print(f"  [skipped] {label:<22} {relative}")
        else:
            print("  " + _paint(f"[MISSING] {label:<22} {relative}", "33"))
            missing.append(label)

    print()
    if missing:
        warn("Not every dependency is present: " + ", ".join(missing) + ". UnrealBuildTool will "
             "fail on the corresponding module. Re-run without the matching --skip-* option, or "
             "with --rebuild to start from scratch.")
    else:
        print(_paint("All native dependencies are built and staged.", "32;1"))

    print("\nNext: open ProjectMobius.uproject in Unreal Engine 5.5 and let it compile the editor "
          "target.")
    print(f"Re-run the staging checks on their own with:\n"
          f"  ctest --test-dir \"{build_path}\" -C {args.config} --output-on-failure")
    return 0


if __name__ == "__main__":
    sys.exit(main())
