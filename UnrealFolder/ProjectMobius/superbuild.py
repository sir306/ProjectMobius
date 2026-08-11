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

This script adds four things CMake cannot do on its own:

  1. Pins the MSVC toolset to 14.38.33130 when it is installed -- the toolset Unreal Engine 5.5
     itself builds with -- so the dependency DLLs link the same CRT as the engine. A toolset can
     only be chosen at configure time via -T, and the correct spelling is not obvious (see
     msvc_toolset_argument below).
  2. Explains WHY when no usable compiler is found, listing what is installed and printing the
     exact override to type, instead of "could not find any instance of Visual Studio".
  3. Detects a build tree that cannot be reused -- generated on another machine, or for a
     different generator, toolset or CMake version -- and reconfigures, saying which it was. This
     is the "I copied the repo and now nothing builds" case.
  4. Prints a per-dependency [ok]/[MISSING] summary at the end, naming the file UnrealBuildTool
     would fail on.

Every option is a thin pass-through to CMake, so nothing here is load-bearing magic. If this
script is ever in your way, use the two cmake commands above.
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Directory containing this script == the Unreal project root (where CMakeLists.txt lives).
PROJECT_DIR = Path(__file__).resolve().parent

IS_WINDOWS = platform.system() == "Windows"

# The toolset Unreal Engine 5.5 builds with. Preferred, not required: the shim's DLL boundary is a
# C ABI, so a mismatch would most likely work anyway, but matching removes an axis of divergence in
# the CRT for free.
PREFERRED_MSVC_TOOLSET = "14.38.33130"

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
    ("ifc", "IFC bridge import lib",
     "Source/ThirdParty/MobiusIfcLibrary/install/lib/MobiusIfcBridge.lib"),
    ("ifc", "IFC bridge DLL",
     "Source/ThirdParty/MobiusIfcLibrary/install/bin/MobiusIfcBridge.dll"),
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
                 running_cmake: tuple[int, ...]) -> str | None:
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

    major = read_cache_entry(cache_path, "CMAKE_CACHE_MAJOR_VERSION")
    minor = read_cache_entry(cache_path, "CMAKE_CACHE_MINOR_VERSION")
    if major and minor and (int(major), int(minor)) != running_cmake[:2]:
        return (f"it was written by CMake {major}.{minor} and you are running "
                f"{running_cmake[0]}.{running_cmake[1]}.")

    return None


def unreal_editor_processes() -> list[str]:
    """Return names of running Unreal Editor processes, so we can refuse to fight one for a file
    lock. The Assimp stage writes into Plugins/UE4_Assimp/Binaries/<Platform>, which a live editor
    holds open, and rebuilding a dependency under a live editor can also trigger a hot-reload."""
    try:
        if IS_WINDOWS:
            output = subprocess.check_output(
                ["tasklist", "/FO", "CSV", "/NH"], text=True, errors="replace")
            return [line.split(",")[0].strip('"') for line in output.splitlines()
                    if line.lower().startswith('"unrealeditor')]
        output = subprocess.check_output(["pgrep", "-fl", "UnrealEditor"], text=True)
        return [line.strip() for line in output.splitlines() if line.strip()]
    except (OSError, subprocess.CalledProcessError):
        # pgrep exits 1 when nothing matches; tasklist may be absent in odd environments. Neither
        # is a reason to stop -- this check is a courtesy, not a gate we can guarantee.
        return []


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
        # Nothing to detect off Windows: CMake's default generator (Ninja or Unix Makefiles) is
        # correct, and -T is a Visual Studio concept. The IFC++ bridge is Win64-only today and the
        # CMakeLists skips it on its own.
        if generator == "auto":
            generator = ""
        note(f"generator {generator or '<CMake default>'!r} ({platform.system()})")

    running_editors = unreal_editor_processes()
    if running_editors and not args.allow_editor_running:
        fail(
            f"Unreal Editor is running ({', '.join(sorted(set(running_editors)))}).\n\n"
            "  The Assimp stage writes into Plugins/UE4_Assimp/Binaries/<Platform>, which the\n"
            "  editor holds open, and rebuilding a dependency under a live editor can trigger a\n"
            "  hot-reload restart.\n\n"
            "  Close the editor, or pass --allow-editor-running if this run cannot touch it."
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
        reason = stale_reason(cache_path, generator, toolset_arg, version)
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
    if not args.skip_ifc and IS_WINDOWS:
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
        # The IFC bridge is Win64-only; a macOS/Linux run is complete without it.
        if dependency == "ifc" and not IS_WINDOWS:
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
