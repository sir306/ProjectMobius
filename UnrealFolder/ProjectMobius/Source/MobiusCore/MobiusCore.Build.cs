using System;
using System.IO;
using UnrealBuildTool;

public class MobiusCore : ModuleRules
{
	public MobiusCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// ── Twinmotion packaged-build gate ────────────────────────────────────────────
		// Twinmotion desktop-export content (the /Game/Twinmotion pack and the
		// DatasmithMasterMaterials generated from it) is licensed under Epic's Twinmotion
		// EULA. That EULA permits visualizing the content *within* Unreal Engine / the editor
		// but does NOT grant the right to redistribute it inside a packaged, interactive
		// application. So the editor build may use it for authoring; a packaged build must not
		// ship it (its /Game/Twinmotion + DatasmithMasterMaterials cook dirs are removed in
		// Config/DefaultGame.ini) and must not apply it.
		//
		// When this is 1, the runtime Datasmith importer (RuntimeMeshBuilder) treats any mesh
		// slot that resolves to no material — the signature of a Twinmotion-sourced material
		// whose master was excluded from cook — as unsupported, substitutes M_MobiusUnsupported
		// and shows a one-shot EULA notice, instead of attempting the (now impossible) remap.
		//
		// Default: 0 in editor builds (authoring works), 1 in packaged/non-editor builds.
		// DO NOT set this to 0 for a shipping/packaged build. Doing so re-enables using the
		// Twinmotion masters in the package and reintroduces the EULA breach this gate exists to
		// prevent. Flipping it to 1 in the editor too is the safe direction if that reading ever
		// tightens; flipping it to 0 in a package is not advised under any circumstances.
		PrivateDefinitions.Add("MOBIUS_TWINMOTION_PACKAGED_DISABLED=" + (Target.Type == TargetType.Editor ? "0" : "1"));

		// Public deps: only what your *public headers* require
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"MassEntity",
			"ProceduralMeshComponent",
			"RHI",
			"UE_Assimp",
			"UE_AssimpLibrary",
			"MobiusLogging",
			// Public: Actors/HeatmapPixelTextureVisualizer.h holds an FHeatmapLOSBands UPROPERTY by value.
			"Visualization",
			"MovieSceneCapture", /* For image writing support -> built-in screenshot api causes issues when scene not fully loaded */
		});

		// Private deps: used in your .cpp files
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"ApplicationCore", /* FPlatformApplicationMisc / FDisplayMetrics for the DPI scaling rule + monitor-aware resolution helpers */
			"InputCore", /* FKey::ToString in the click-path diagnostics (Diagnostics/MobiusClickLog.cpp) */
			"HTTP",
			"Json",
			"JsonUtilities",
			"WebSockets",
			"DatasmithRuntime",
			"DatasmithCore",
			"RenderCore",
			"DatasmithContent", "MobiusDataImporter",
		});

		PublicIncludePaths.AddRange(new[]
		{
			"MobiusCore/ThirdParty",
		});

		PrivateIncludePaths.AddRange(new[]
		{
			"MobiusCore/Public",
			"MobiusCore/Private",
			"MobiusCore/ThirdParty",
		});

		// ── Runtime IFC import ────────────────────────────────────────────────────────
		// MobiusIfcLibrary is an external module wrapping MobiusIfcBridge.dll: our own extern "C"
		// shim over IFC++ (IfcPlusPlus, MIT) + Carve (MIT), built from
		// Source\ThirdParty\IfcBridgeSource\ by Build-MobiusIfcBridge.ps1. PRIVATE dependency on
		// purpose — Ifc\MobiusIfcMeshLoader.h (public) exposes only FString/FBox/TArray, and
		// MobiusIfcBridge.h is included by exactly one .cpp, so no other module can start depending
		// on the C ABI by accident. RuntimeDependencies still stage the DLL from a private
		// dependency.
		//
		// Enabled where MobiusIfcLibrary.Build.cs can supply the shim: Win64 (MobiusIfcBridge.dll) and
		// macOS (libMobiusIfcBridge.dylib), both produced by the superbuild. On any other platform
		// MOBIUS_WITH_IFC_BRIDGE=0 lets the loader still compile and return a clear "IFC import is not
		// available in this build" error instead of failing to link.
		if (Target.Platform == UnrealTargetPlatform.Win64 ||
		    Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.Add("MobiusIfcLibrary");
			PrivateDefinitions.Add("MOBIUS_WITH_IFC_BRIDGE=1");
		}
		else
		{
			PrivateDefinitions.Add("MOBIUS_WITH_IFC_BRIDGE=0");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{

		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Add AppKit framework for NSOpenPanel file dialogs
			PublicFrameworks.Add("AppKit");
		}
	}
}
