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

        if (!Directory.Exists(IncludeDir))
        {
            throw new BuildException($"Assimp include dir not found: {IncludeDir}");
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

            if (!Directory.Exists(LibDir)) throw new BuildException($"Assimp lib dir not found: {LibDir}");
            if (!Directory.Exists(BinDir)) throw new BuildException($"Assimp bin dir not found: {BinDir}");

            // Pick import lib + dll by pattern
            string ImportLib = Directory.GetFiles(LibDir, "assimp*.lib")[0];
            string DllPath   = Directory.GetFiles(BinDir, "assimp*.dll")[0];
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
