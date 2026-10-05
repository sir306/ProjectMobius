// SPDX-License-Identifier: MIT
//
// LANDED 2026-08-11 (session 2) at:
//   ProjectMobius\UnrealFolder\ProjectMobius\Source\ThirdParty\MobiusIfcLibrary\MobiusIfcLibrary.Build.cs
// Session 1 staged this file with a MobiusDataImporter plugin path in its header. It lives under the
// game Source tree instead, deliberately: the only consumer is MobiusCore (RuntimeMeshBuilder ->
// FAssimpMeshLoaderRunnable -> the IFC loader), the plugin has no IFC code in it, and keeping the
// 23 MB of vendored IFC++ source out of a plugin that may be extracted separately is cheaper than
// coupling them. The vendored upstream source and the shim's CMakeLists live in the SIBLING folder
// Source\ThirdParty\IfcBridgeSource\ rather than under this module directory, so UBT never
// enumerates 23 MB of third-party source when checking whether this external module changed.
//
// install/{include,lib,bin} is produced by Source\ThirdParty\IfcBridgeSource\Build-MobiusIfcBridge.ps1.
// The DLL and import lib are NOT committed (.gitignore excludes *.dll/*.lib repo-wide; zero .dll or
// .lib files are tracked anywhere in this repo, assimp and HDF5 included), so a fresh checkout MUST
// run that script once before the first build.
//
// MobiusIfcLibrary wraps a self-built DLL (IFC++ [MIT] + our own extern "C" shim,
// MobiusIfcBridge.h/.cpp). Modeled on UE_AssimpLibrary.Build.cs (DLL + delay-load shape) with
// HDF5's stricter platform/include discipline (UeHdf5Library.Build.cs) where the two precedents
// disagree. See decision notes at the bottom of this file.

using System.IO;
using UnrealBuildTool;

public class MobiusIfcLibrary : ModuleRules
{
	public MobiusIfcLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		// This is an external module: we supply prebuilt include/lib/dll, UBT does not compile
		// any IFC++ or shim source here.
		Type = ModuleType.External;

		// The IFC++ / Carve headers must never reach UE compilation units — the whole point of the
		// extern "C" shim (MobiusIfcBridge.h) is that no std:: or Carve type crosses the module
		// boundary. bAddDefaultIncludePaths off means we own the include path exactly: only
		// install/include gets added below, nothing else from this module directory leaks in.
		bAddDefaultIncludePaths = false;

		// IFC++ is built with C++ exceptions and RTTI internally; that is contained *inside* the
		// DLL. This module must not turn either on for UE compilation units that consume the C
		// shim header — the header is plain C-linkage/POD, so there is nothing here that needs
		// exceptions or RTTI, and enabling them here would be a false signal that C++ exception
		// state can cross the DLL boundary. It cannot: every throw is caught inside
		// MobiusIfcBridge.cpp and converted to a C error code before it can unwind into UE.
		// (No bEnableExceptions/bUseRTTI assignment needed — ModuleRules defaults are already off
		// for an External module; recorded here so the reasoning survives if that ever changes.)

		// Root to our vendored IFC++ bridge build under this external module.
		// Expected layout (produced by the CMake install step):
		//   MobiusIfcLibrary/
		//     install/
		//       include/MobiusIfcBridge.h
		//       lib/MobiusIfcBridge.lib
		//       bin/MobiusIfcBridge.dll
		// Platform check FIRST, before any path probing. Otherwise a fresh checkout on an unsupported
		// platform (where install/ has not been populated because the CMake step was never run) reports
		// "include dir not found, run the CMake install step" and sends a porter chasing the wrong
		// problem, when the real answer is "this platform is not wired".
		//
		// Win64 and macOS are wired: the superbuild produces MobiusIfcBridge.dll (+ import lib) on
		// Windows and libMobiusIfcBridge.dylib on macOS. Linux/others remain a deliberate TODO -- the
		// shim is portable extern "C" with no STL in the interface, so porting is a CMake toolchain plus
		// a branch here. Hard-fail rather than assimp's silent no-op, which would defer the failure to an
		// inscrutable link error far from this file (UeHdf5Library's discipline).
		if (Target.Platform != UnrealTargetPlatform.Win64 &&
		    Target.Platform != UnrealTargetPlatform.Mac)
		{
			throw new BuildException($"MobiusIfcLibrary module not set up for platform {Target.Platform}");
		}

		string IfcRoot = Path.Combine(ModuleDirectory, "install");
		string IncludeDir = Path.Combine(IfcRoot, "include");

		// One remedy string for every check below. The superbuild is the supported entry point --
		// it drives Build-MobiusIfcBridge.ps1 itself and resolves the Visual Studio generator and
		// MSVC toolset, which running the script by hand does not require you to think about but a
		// raw `cmake -G "Visual Studio 17 2022"` very much does.
		const string SuperbuildRemedy =
			"Run the superbuild first: from UnrealFolder/ProjectMobius, `python superbuild.py`. " +
			"It populates install/include, install/lib and install/bin. The whole install tree is " +
			"gitignored, so a fresh checkout never has it. To rebuild only this dependency: " +
			"`python superbuild.py --skip-assimp --skip-hdf5 --force-ifc`.";

		if (!Directory.Exists(IncludeDir))
		{
			throw new BuildException($"MobiusIfcLibrary include dir not found: {IncludeDir}. {SuperbuildRemedy}");
		}

		// Only the pure-C public header goes on the include path. IFC++/Carve headers stay inside
		// the DLL build and are never exposed to UE modules.
		PublicIncludePaths.Add(IncludeDir);

		// Consumers must NOT define MOBIUSIFC_BUILD_DLL — that switch is for the DLL-side build
		// only (it flips MOBIUSIFC_API to __declspec(dllexport) in MobiusIfcBridge.h). Leaving it
		// undefined here means UE modules that #include MobiusIfcBridge.h resolve MOBIUSIFC_API to
		// __declspec(dllimport), matching the import lib linked below. Do not add a
		// PublicDefinitions.Add("MOBIUSIFC_BUILD_DLL") here.
		// VERIFIED 2026-08-11: MobiusIfcBridge.h has landed and its `#if defined(_WIN32)` /
		// `#if defined(MOBIUSIFC_BUILD_DLL)` branch does resolve MOBIUSIFC_API to __declspec(dllimport)
		// when the macro is undefined, matching the import lib added below. The DLL was built and its
		// export table confirmed to contain exactly the 7 expected undecorated C symbols.
		//
		// DELAY-LOAD, read before writing the consumer module. PublicDelayLoadDLLs + a linked import
		// lib means MSVC's delay-load thunk calls LoadLibrary/GetProcAddress itself on the first call
		// to any MobiusIfc_* symbol — so an explicit load is not architecturally required. What IS
		// required is that the DLL be findable at that moment: if it is not, the thunk raises a Win32
		// SEH exception (ERROR_MOD_NOT_FOUND) at the call site, which is not a C++ exception and will
		// not be caught by anything in a module built with bEnableExceptions = false. The
		// RuntimeDependencies below stage it next to the binary, which covers the normal case; the
		// consumer's StartupModule should still call FPlatformProcess::GetDllHandle on the staged path
		// (this is what the existing UE4_Assimp integration does) so a missing DLL becomes one clean
		// log line instead of a crash on first use.

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibDir = Path.Combine(IfcRoot, "lib");
			string BinDir = Path.Combine(IfcRoot, "bin");

			if (!Directory.Exists(LibDir)) throw new BuildException($"MobiusIfcLibrary lib dir not found: {LibDir}. {SuperbuildRemedy}");
			if (!Directory.Exists(BinDir)) throw new BuildException($"MobiusIfcLibrary bin dir not found: {BinDir}. {SuperbuildRemedy}");

			// Named explicitly (not globbed) — we build this DLL ourselves, so the filenames are
			// fixed and known ahead of time. A glob like assimp's Directory.GetFiles(...)[0] would
			// silently pick up a stray file instead of failing loudly on a rename/typo.
			string ImportLib = Path.Combine(LibDir, "MobiusIfcBridge.lib");
			string DllPath = Path.Combine(BinDir, "MobiusIfcBridge.dll");
			string DllName = Path.GetFileName(DllPath);

			if (!File.Exists(ImportLib)) throw new BuildException($"MobiusIfcLibrary import lib not found: {ImportLib}. {SuperbuildRemedy}");
			if (!File.Exists(DllPath)) throw new BuildException($"MobiusIfcLibrary DLL not found: {DllPath}. {SuperbuildRemedy}");

			PublicAdditionalLibraries.Add(ImportLib);
			PublicDelayLoadDLLs.Add(DllName); // filename only

			// Stage next to this module's output and the packaged target output.
			RuntimeDependencies.Add("$(BinaryOutputDir)/" + DllName, DllPath);
			RuntimeDependencies.Add("$(TargetOutputDir)/" + DllName, DllPath);

			// Critical for Editor builds: also stage next to UnrealEditor.exe.
			if (Target.bBuildEditor)
			{
				RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/" + DllName, DllPath);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// macOS: link the shared library directly. There is no import lib and no delay-load on Mac;
			// the dylib's install_name is @rpath/libMobiusIfcBridge.dylib (Build-MobiusIfcBridge.sh sets
			// CMAKE_INSTALL_NAME_DIR=@rpath), so staging it next to the consuming binary lets dyld
			// resolve it via @rpath. Mirrors UE_AssimpLibrary's macOS branch (assimp loads the same way).
			string BinDir    = Path.Combine(IfcRoot, "bin");
			string DylibName = "libMobiusIfcBridge.dylib";
			string DylibPath = Path.Combine(BinDir, DylibName);

			if (!Directory.Exists(BinDir)) throw new BuildException($"MobiusIfcLibrary bin dir not found: {BinDir}. {SuperbuildRemedy}");
			if (!File.Exists(DylibPath)) throw new BuildException($"MobiusIfcLibrary dylib not found: {DylibPath}. {SuperbuildRemedy}");

			PublicAdditionalLibraries.Add(DylibPath);

			// Stage next to this module's binary and the packaged/editor target output so dyld resolves
			// it via @rpath at run time.
			RuntimeDependencies.Add("$(BinaryOutputDir)/" + DylibName, DylibPath);
			RuntimeDependencies.Add("$(TargetOutputDir)/" + DylibName, DylibPath);
		}
		else
		{
			// Linux/others: not wired yet. The early platform guard already rejects these, so this
			// branch is defensive -- it keeps the failure legible if that guard is later relaxed
			// without adding the corresponding case here.
			throw new BuildException($"MobiusIfcLibrary module not set up for platform {Target.Platform}");
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Decision notes where the two precedents conflict (see final report for the full version):
//   - glob vs explicit lib/dll names -> explicit (we own the build, name is fixed; assimp's glob
//     is fragile and unnecessary here).
//   - silent no-op vs hard-fail on unsupported platform -> hard-fail (HDF5's approach), because
//     Mobius is Win64-only and a silent no-op would defer the failure to an unreadable link error.
//   - bAddDefaultIncludePaths -> false (HDF5's approach), and load-bearing here: it is part of how
//     we guarantee only install/include (the C shim header) is visible, never IFC++/Carve headers.
// ---------------------------------------------------------------------------------------------
