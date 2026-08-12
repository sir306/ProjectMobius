// SPDX-License-Identifier: MIT
// External wrapper for Assimp inside the plugin. This module exposes include paths,
// link libs, and tells Unreal's stager which runtime binaries to copy for packaging.

using System;
using System.IO;
using UnrealBuildTool;

public class UE_AssimpLibrary : ModuleRules
{
    public UE_AssimpLibrary(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // Root to our vendored assimp under this external module
        string AssimpRoot = Path.Combine(ModuleDirectory, "assimp");
        string IncludeDir = Path.Combine(AssimpRoot, "include");

        // assimp/lib and assimp/bin are CMake install output and are gitignored, so a fresh
        // checkout has the headers but nothing to link against. Say what to run.
        const string SuperbuildRemedy =
            "Run the superbuild first: from UnrealFolder/ProjectMobius, `python superbuild.py`. " +
            "It builds Assimp and installs it into assimp/{include,lib,bin}.";

        if (!Directory.Exists(IncludeDir))
        {
            throw new BuildException($"Assimp include dir not found: {IncludeDir}. {SuperbuildRemedy}");
        }

        PublicIncludePaths.Add(IncludeDir);

        // Per-platform linkage + staging
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Expected layout:
            // assimp/
            //   include/...
            //   lib/Win64/assimp-vc143-mt.lib (or similar)
            //   bin/Win64/assimp-vc143-mt.dll (or similar)

            string LibDir = Path.Combine(AssimpRoot, "lib", "Win64");
            string BinDir = Path.Combine(AssimpRoot, "bin", "Win64");

            if (!Directory.Exists(LibDir)) throw new BuildException($"Assimp lib dir not found: {LibDir}. {SuperbuildRemedy}");
            if (!Directory.Exists(BinDir)) throw new BuildException($"Assimp bin dir not found: {BinDir}. {SuperbuildRemedy}");

            // Pick import lib + dll by pattern.
            //
            // These two globs used to index [0] directly. An interrupted install leaves the
            // directories present but empty, and [0] then threw IndexOutOfRangeException from
            // inside UBT -- a stack trace with no filename in it. Check the match count instead.
            // Note the two globs are INDEPENDENT: if the tree somehow held two toolset variants
            // (assimp-vc143-mt.* and assimp-vc145-mt.*), GetFiles makes no ordering guarantee and
            // the lib and the DLL could come from different builds. The superbuild's prune step
            // keeps the install tree to one variant; this is the second line of defence.
            string[] ImportLibs = Directory.GetFiles(LibDir, "assimp*.lib");
            string[] Dlls       = Directory.GetFiles(BinDir, "assimp*.dll");

            if (ImportLibs.Length == 0) throw new BuildException($"No assimp*.lib in {LibDir}. {SuperbuildRemedy}");
            if (Dlls.Length == 0)       throw new BuildException($"No assimp*.dll in {BinDir}. {SuperbuildRemedy}");
            if (ImportLibs.Length > 1 || Dlls.Length > 1)
            {
                throw new BuildException(
                    $"Multiple Assimp toolset variants found ({string.Join(", ", ImportLibs)} / " +
                    $"{string.Join(", ", Dlls)}). Linking a lib and a DLL from different MSVC " +
                    $"toolsets is a silent ABI mismatch. Re-run the superbuild, which prunes the " +
                    $"install tree to a single variant: `python superbuild.py` from UnrealFolder/ProjectMobius.");
            }

            string ImportLib = ImportLibs[0];
            string DllPath   = Dlls[0];
            string DllName   = Path.GetFileName(DllPath);

            PublicAdditionalLibraries.Add(ImportLib);
            PublicDelayLoadDLLs.Add(DllName); // filename only

            // Stage next to your module output and packaged output
            RuntimeDependencies.Add("$(BinaryOutputDir)/" + DllName, DllPath);
            RuntimeDependencies.Add("$(TargetOutputDir)/" + DllName, DllPath);

            // Critical for Editor: also stage next to UnrealEditor.exe
            if (Target.bBuildEditor)
            {
                RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/" + DllName, DllPath);
            }
        }

        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            // ---- macOS ----
            // NOTE: Your CMake for assimp should set: -DCMAKE_INSTALL_NAME_DIR=@rpath
            // so the produced dylib has an install_name like "@rpath/libassimp.dylib"
            string LibDir   = Path.Combine(AssimpRoot, "lib", "Mac");
            string DylibName = "libassimp.dylib";
            string DylibPath = Path.Combine(LibDir, DylibName);

            if (!File.Exists(DylibPath))
            {
                throw new BuildException($"Missing Assimp dylib: {DylibPath}");
            }

            // Linker input
            PublicAdditionalLibraries.Add(DylibPath);

            // Stage the dylib inside the app bundle’s MacOS folder (loader will find it via @rpath)
            RuntimeDependencies.Add("$(TargetOutputDir)/" + DylibName, DylibPath);

            // Optional: ensure rpaths are present if your project strips them; UE usually adds @executable_path/../Frameworks and @loader_path
            // LinkerFlags.Add("-rpath @executable_path"); // Example if you need to enforce rpath
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            // ---- Linux (optional) ----
            string LibDir   = Path.Combine(AssimpRoot, "lib", "Linux");
            string SoName   = "libassimp.so";
            string SoPath   = Path.Combine(LibDir, SoName);

            if (!File.Exists(SoPath))
            {
                throw new BuildException($"Missing Assimp so: {SoPath}");
            }

            PublicAdditionalLibraries.Add(SoPath);
            RuntimeDependencies.Add("$(TargetOutputDir)/" + SoName, SoPath);
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            // ---- Android (if you’re providing prebuilts) ----
            // Typically you organize by ABI: arm64-v8a, armeabi-v7a, x86_64, etc.
            // You can also use .Build.cs + .UPL to copy per-ABI .so to the APK.
            string Abi = "arm64-v8a"; // adjust or branch per Target.Architecture
            string SoPath = Path.Combine(AssimpRoot, "bin", "Android", Abi, "libassimp.so");

            if (!File.Exists(SoPath))
            {
                throw new BuildException($"Missing Assimp android so: {SoPath}");
            }

            // For Android, use RuntimeDependencies so the .so gets packaged into the APK
            RuntimeDependencies.Add("$(TargetOutputDir)/libassimp.so", SoPath);
            // If you also ship headers to NDK build steps, ensure PublicIncludePaths covers them (already done above).
        }
        else
        {
            // Other platforms not wired yet
        }
    }
}
