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
            
            // Pick import lib + dll by pattern
            string ImportLib = Path.Combine(LibDir, "libhdf5.lib");
            //string DllPath   = Path.Combine(BinDir, "hdf5.dll");
            //string DllName   = Path.GetFileName(DllPath);
            
            // Link the import lib
            PublicAdditionalLibraries.Add(ImportLib);
            
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
        // HDF5 End
    }
}