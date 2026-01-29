using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

public class UeHdf5Library : ModuleRules
{
    public UeHdf5Library(ReadOnlyTargetRules Target) : base(Target)
    {
        // This is an external module that wraps the HDF5 library
        Type = ModuleType.External;
        
        // we manage include paths ourselves
        bAddDefaultIncludePaths = false; 
        
        // HDF5 Start
        
        // DLL path = ThirdParty\hdf5-2.0.0\install\lib
        // bin path = ThirdParty\hdf5-2.0.0\install\bin
        // header include path = ThirdParty\hdf5-2.0.0\install\include
        
        // Root to our vendored hdf5 under this external module
        string Hdf5Root = Path.Combine(ModuleDirectory, "hdf5-2.0.0", "install");
        string IncludeDir = Path.Combine(Hdf5Root, "include");
        
        // check include dir exists
        if (!Directory.Exists(IncludeDir))
        {
            throw new BuildException($"HDF5 include dir not found: {IncludeDir}");
        }
        // Add include path to public include paths for hdf5
        PublicIncludePaths.Add(IncludeDir);
        
        // Per-platform linkage + staging
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LibDir = Path.Combine(Hdf5Root, "lib");;
            string BinDir = Path.Combine(Hdf5Root, "bin");// may not need bin as we link statically TBD
            
            if (!Directory.Exists(LibDir)) throw new BuildException($"HDF5 lib dir not found: {LibDir}");
            if (!Directory.Exists(BinDir)) throw new BuildException($"HDF5 bin dir not found: {BinDir}");
            
            // Static libraries to link (HDF5 core, HL, and zlib for compression)
            string[] Hdf5Libs = new string[]
            {
                "libhdf5.lib",
                "libhdf5_hl.lib",
                "zlib-static.lib"
            };

            foreach (string LibName in Hdf5Libs)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibName));
            }

            // MSVC treats undefined macro in #if as an error; HDF5 doesn't define this for MSVC.
            PublicDefinitions.Add("H5_HAVE_BUILTIN_EXPECT=0");

            // HDF5 uses StrStrIA (shlwapi) on Windows.
            PublicSystemLibraries.Add("Shlwapi.lib");
            
            // dll loading not needed for static link - Maybe in future if we do dynamic link
            //PublicDelayLoadDLLs.Add(DllName); // filename only    
            // Stage next to your module output and packaged output
            //RuntimeDependencies.Add("$(BinaryOutputDir)/" + DllName, DllPath);
            //RuntimeDependencies.Add("$(TargetOutputDir)/" + DllName, DllPath);
            // Critical for Editor: also stage next to UnrealEditor.exe
            //if (Target.bBuildEditor)
            //{
            //    RuntimeDependencies.Add("$(EngineDir)/Binaries/Win64/" + DllName, DllPath);
            //}
        }
        // Mac Support
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string LibDir = Path.Combine(Hdf5Root, "lib");
            
            if (!Directory.Exists(LibDir)) throw new BuildException($"HDF5 lib dir not found: {LibDir}");
            
            // Static libraries to link (HDF5 core, HL, and zlib for compression)
            string[] Hdf5Libs = new string[]
            {
                "libhdf5.a",
                "libhdf5_hl.a",
                "libz.a"
            };

            foreach (string LibName in Hdf5Libs)
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibDir, LibName));
            }

            // Mac specific definitions
            PublicDefinitions.Add("H5_HAVE_BUILTIN_EXPECT=1");
        }
        else
        {
            throw new BuildException($"UeHdf5Library module not set up for platform {Target.Platform}");
        }
    }
}
