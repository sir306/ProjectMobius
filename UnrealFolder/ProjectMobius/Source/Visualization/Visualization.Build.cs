using UnrealBuildTool;

public class Visualization : ModuleRules
{
    public Visualization(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core", 
                //"MassEntity",
                "OpenCV",
                "UMG",
                "OpenCVHelper",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore", 
                "MobiusWidgets",
                "ProjectMobius",// TODO: extract out the mesh gen so we dont reference the whole project and also causes a circular dependency
                "RHI",
                "RenderCore",
                
                //"MobiusCore" // -> as we look for the runtimesh comp it requires the assimp loader in this module this is a circular dependency
                // if we move the pixel generator out this module should drop the dependency on MobiusCore
            }
        );
        
        PublicIncludePaths.AddRange(new string[] {"Visualization/Public"});
        PrivateIncludePaths.AddRange(new string[] { "Visualization/Private" });
        
        //TODO: Sort different build versions for different platforms and use the dll and libs from the ThirdParty folder not manually added
        // Add OpenCV dll and lib files
        string opencvProjectDir = System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "../../"));
        
        // Windows
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string platformString = "Win64";
            string librariesPath = System.IO.Path.Combine(opencvProjectDir, "Binaries", platformString);
            PublicAdditionalLibraries.Add(System.IO.Path.Combine(librariesPath, "opencv_world455.lib"));
            string dllPath = System.IO.Path.Combine(librariesPath, "opencv_world455.dll");
            PublicDelayLoadDLLs.Add(dllPath);
            RuntimeDependencies.Add(dllPath);
        }
        // Mac
        else if(Target.Platform == UnrealTargetPlatform.Mac)
        {
            // TODO: Add Mac OpenCV Libraries
        }
        
        
    }
}