// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryHeatmapCommands.cpp
//
// Console commands for driving and measuring the trajectory heatmap without the UI. These exist so a
// calibration run is reproducible: spawn a heatmap of a known world size, put it in trajectory mode,
// play a fixture, then read the accumulated bytes back out rather than eyeballing colour bands.
//
// Reading BYTES rather than judging colour matters — the palette bands are only ~11 bytes wide at the
// bottom of the range, which is not something an eye can resolve on screen.
//
//   mobius.Heatmap.SpawnTrajectory [SizeMetres=50] [WorldZ=0]
//   mobius.Heatmap.SetTrajectoryMode [0|1]
//   mobius.Heatmap.ReadTexel <X> <Y>
//   mobius.Heatmap.ReadWorld <WorldX> <WorldY>
//   mobius.Heatmap.DumpTrajectoryCsv [Filename]
//   mobius.Heatmap.CaptureFloor [Seconds=30] [FloorID=0] [MaxRows=4000000]
//   mobius.Heatmap.CaptureAbort
//
// Editor/development only; the whole file compiles out of shipping builds.
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "EngineUtils.h" // TActorIterator
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "Diagnostics/TrajectoryCaptureRecorder.h"
#include "DynamicPixelRenderingTexture.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusTrajectoryCmd, Log, All);

namespace TrajectoryHeatmapCommands
{
	/** Unreal units are centimetres; fixtures and arguments are expressed in metres for sanity. */
	static constexpr float CentimetresPerMetre = 100.0f;

	static UHeatmapSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UHeatmapSubsystem>() : nullptr;
	}

	/** The first trajectory-enabled heatmap in the world, which is all a single-floor fixture needs. */
	static AHeatmapPixelTextureVisualizer* FindTrajectoryHeatmap(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
		{
			if (AHeatmapPixelTextureVisualizer* Heatmap = *It; IsValid(Heatmap) && Heatmap->bTrajectoryHeatmap)
			{
				return Heatmap;
			}
		}

		// Fall back to any heatmap so ReadTexel still reports something useful before mode is enabled.
		for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static float ArgAsFloat(const TArray<FString>& Args, int32 Index, float Fallback)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Fallback;
	}

	static int32 ArgAsInt(const TArray<FString>& Args, int32 Index, int32 Fallback)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Fallback;
	}
}

// -------------------------------------------------------------------------------------------------
// Spawn a heatmap of an explicit world size and immediately put it in trajectory mode.
//
// Deliberately does NOT go through UHeatmapSubsystem::CreateHeatmap: that derives its size from
// HeatmapBoundingSize, which is only populated once a building has been imported. Passing the size in
// directly is what lets a calibration run work with no geometry loaded at all.
//
// CentreX/CentreY exist because the size is only half the question. The trajectory field takes the mesh
// component's world location as its grid's MINIMUM corner, so a heatmap left at the origin covers the
// +X/+Y quadrant only - and no imported dataset sits there. The importer negates Y, and the technical
// school for instance runs X [-9.4, 57.8] m, Y [-30.1, 22.9] m. Capture it from the origin and you get a
// truncated slice of the site with most of the path length booked to dropped mass, which looks like data
// rather than like an error. The default stays (0,0) because the no-geometry calibration case this
// command was written for genuinely wants it; the covered box is logged either way so the mismatch is
// visible before a capture is taken rather than after it is analysed.
// -------------------------------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapSpawnTrajectory(
	TEXT("mobius.Heatmap.SpawnTrajectory"),
	TEXT("Spawn a heatmap of [SizeMetres=50] at [WorldZ=0], centred on [CentreX=0] [CentreY=0] (metres), ")
	TEXT("and enable trajectory mode. Needs no building geometry. Centre it on the agents: the grid's ")
	TEXT("minimum corner is the actor location, so the default covers the +X/+Y quadrant only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			using namespace TrajectoryHeatmapCommands;

			UHeatmapSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("No HeatmapSubsystem - run this in a game or PIE world."));
				return;
			}

			const float SizeMetres = ArgAsFloat(Args, 0, 50.0f);
			const float WorldZ = ArgAsFloat(Args, 1, 0.0f);
			const float SizeCm = SizeMetres * CentimetresPerMetre;

			// Args 2/3 are the CENTRE, which is how anyone thinks about placing a heatmap over a
			// building. The actor goes at the minimum corner, half a span away, because that is what
			// FTrajectoryField::Initialise reads as the grid origin. Both are logged below so the
			// relationship is on screen and nobody has to rediscover it.
			const float CentreXCm = ArgAsFloat(Args, 2, 0.0f) * CentimetresPerMetre;
			const float CentreYCm = ArgAsFloat(Args, 3, 0.0f) * CentimetresPerMetre;
			const FVector MinCorner(CentreXCm - SizeCm * 0.5f, CentreYCm - SizeCm * 0.5f, WorldZ);

			AHeatmapPixelTextureVisualizer* Heatmap =
				World->SpawnActor<AHeatmapPixelTextureVisualizer>(MinCorner, FRotator::ZeroRotator);
			if (!Heatmap)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("Failed to spawn AHeatmapPixelTextureVisualizer."));
				return;
			}

			Heatmap->ActorName = TEXT("Heatmap_TrajectoryTest");
			Heatmap->FloorID = 0;
			// Type 2 = standard heatmap, live tracking on, 2D (no height displacement) so the readback
			// is not confounded by the WorldPositionOffset path that RVal also feeds.
			Heatmap->InitializeHeatmap(2, true, FVector2D(SizeCm, SizeCm), 0.0f, false);
			Subsystem->AddHeatmapActor(Heatmap);
			Subsystem->SetTrajectoryHeatmapsEnabled(true);

			const float MetresPerTexel = SizeMetres / 1024.0f;
			UE_LOG(LogMobiusTrajectoryCmd, Display,
				TEXT("Trajectory heatmap ready. %.1f m across, 1024 texels, %.4f m/texel (%.1f cm). Origin Z=%.1f."),
				SizeMetres, MetresPerTexel, MetresPerTexel * CentimetresPerMetre, WorldZ);
			// The line to read before capturing. If the agents are not inside this box the capture is a
			// truncated slice, and nothing downstream will say so - the field just books the rest to
			// dropped mass.
			UE_LOG(LogMobiusTrajectoryCmd, Display,
				TEXT("Covers world X [%.0f, %.0f] Y [%.0f, %.0f] cm (centre %.0f, %.0f; actor at the minimum ")
				TEXT("corner %.0f, %.0f). Re-spawn with 'mobius.Heatmap.SpawnTrajectory %.0f %.0f <centreX_m> ")
				TEXT("<centreY_m>' if the agents are not inside it."),
				MinCorner.X, MinCorner.X + SizeCm, MinCorner.Y, MinCorner.Y + SizeCm,
				CentreXCm, CentreYCm, MinCorner.X, MinCorner.Y, SizeMetres, WorldZ);
		}));

// -------------------------------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapSetTrajectoryMode(
	TEXT("mobius.Heatmap.SetTrajectoryMode"),
	TEXT("Enable (1) or disable (0) trajectory mode on every heatmap. Enabling clears the accumulation buffer."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			using namespace TrajectoryHeatmapCommands;

			UHeatmapSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("No HeatmapSubsystem in this world."));
				return;
			}

			const bool bEnable = ArgAsInt(Args, 0, 1) != 0;
			Subsystem->SetTrajectoryHeatmapsEnabled(bEnable);
			UE_LOG(LogMobiusTrajectoryCmd, Display, TEXT("Trajectory mode %s."), bEnable ? TEXT("ENABLED") : TEXT("disabled"));
		}));

// -------------------------------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapReadTexel(
	TEXT("mobius.Heatmap.ReadTexel"),
	TEXT("Print the raw accumulated red byte at texel <X> <Y>, with its normalised value and LOS band."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			using namespace TrajectoryHeatmapCommands;

			if (Args.Num() < 2)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("Usage: mobius.Heatmap.ReadTexel <X> <Y>"));
				return;
			}

			AHeatmapPixelTextureVisualizer* Heatmap = FindTrajectoryHeatmap(World);
			const UDynamicPixelRenderingTexture* Texture =
				Heatmap ? Heatmap->GetTrajectoryAccumulationTextureForTesting() : nullptr;
			if (!Texture)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("No trajectory accumulation texture - spawn a heatmap first."));
				return;
			}

			const int32 X = ArgAsInt(Args, 0, 0);
			const int32 Y = ArgAsInt(Args, 1, 0);
			const uint8 Red = Texture->GetRawPixelRed(X, Y);
			const float Normalised = static_cast<float>(Red) / 255.0f;

			const TCHAR* Band =
				Normalised < 0.1419f ? TEXT("LOS_A") :
				Normalised < 0.1983f ? TEXT("LOS_B") :
				Normalised < 0.3309f ? TEXT("LOS_C") :
				Normalised < 0.4946f ? TEXT("LOS_D") :
				Normalised < 1.0f    ? TEXT("LOS_E") : TEXT("LOS_F");

			UE_LOG(LogMobiusTrajectoryCmd, Display, TEXT("texel (%d,%d)  red=%u  normalised=%.5f  band=%s"),
				X, Y, Red, Normalised, Band);
		}));

// -------------------------------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapReadWorld(
	TEXT("mobius.Heatmap.ReadWorld"),
	TEXT("Resolve a world XY (centimetres) to its texel and print the accumulated red byte there."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			using namespace TrajectoryHeatmapCommands;

			if (Args.Num() < 2)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("Usage: mobius.Heatmap.ReadWorld <WorldX> <WorldY>"));
				return;
			}

			AHeatmapPixelTextureVisualizer* Heatmap = FindTrajectoryHeatmap(World);
			const UDynamicPixelRenderingTexture* Texture =
				Heatmap ? Heatmap->GetTrajectoryAccumulationTextureForTesting() : nullptr;
			if (!Texture)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("No trajectory accumulation texture - spawn a heatmap first."));
				return;
			}

			const FVector WorldLocation(ArgAsFloat(Args, 0, 0.0f), ArgAsFloat(Args, 1, 0.0f), Heatmap->GetActorLocation().Z);
			const FIntPoint Texel = Heatmap->WorldToTexelForTesting(WorldLocation);

			UE_LOG(LogMobiusTrajectoryCmd, Display, TEXT("world (%.1f,%.1f) -> texel (%d,%d)  red=%u"),
				WorldLocation.X, WorldLocation.Y, Texel.X, Texel.Y, Texture->GetRawPixelRed(Texel.X, Texel.Y));
		}));

// -------------------------------------------------------------------------------------------------
// Dump every touched texel to CSV. Sparse by design — a full 1024x1024 dump is a million rows of
// mostly zeroes, whereas a played fixture usually touches only a few thousand texels.
// -------------------------------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapDumpTrajectoryCsv(
	TEXT("mobius.Heatmap.DumpTrajectoryCsv"),
	TEXT("Write all non-zero trajectory texels to Saved/TrajectoryCalibration/[Filename=trajectory_dump.csv]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			using namespace TrajectoryHeatmapCommands;

			AHeatmapPixelTextureVisualizer* Heatmap = FindTrajectoryHeatmap(World);
			const UDynamicPixelRenderingTexture* Texture =
				Heatmap ? Heatmap->GetTrajectoryAccumulationTextureForTesting() : nullptr;
			if (!Texture)
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("No trajectory accumulation texture - spawn a heatmap first."));
				return;
			}

			const FVector2D Size = Texture->GetDynamicTextureSize();
			const int32 Width = FMath::TruncToInt(Size.X);
			const int32 Height = FMath::TruncToInt(Size.Y);

			FString Csv = TEXT("texel_x,texel_y,red_byte,normalised\n");
			int32 TouchedCount = 0;
			int32 PeakByte = 0;

			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const uint8 Red = Texture->GetRawPixelRed(X, Y);
					if (Red == 0)
					{
						continue;
					}
					++TouchedCount;
					PeakByte = FMath::Max(PeakByte, static_cast<int32>(Red));
					Csv += FString::Printf(TEXT("%d,%d,%u,%.5f\n"), X, Y, Red, static_cast<float>(Red) / 255.0f);
				}
			}

			const FString Filename = Args.IsValidIndex(0) ? Args[0] : TEXT("trajectory_dump.csv");
			const FString OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrajectoryCalibration"), Filename);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), /*Tree*/ true);

			if (FFileHelper::SaveStringToFile(Csv, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogMobiusTrajectoryCmd, Display, TEXT("Wrote %d touched texels (peak red %d) to %s"),
					TouchedCount, PeakByte, *OutPath);
			}
			else
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("Failed writing %s"), *OutPath);
			}
		}));

// -------------------------------------------------------------------------------------------------
// Record the whole data path for one floor over a bounded window, then pause and dump.
//
// Resets the playhead to zero and clears the accumulation first, so the captured window lines up
// exactly with the source movement data and the diff needs no time offset.
// -------------------------------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapCaptureFloor(
	TEXT("mobius.Heatmap.CaptureFloor"),
	TEXT("Reset to t=0, capture every stage of the trajectory path for [Seconds=30] on [FloorID=0], then pause and write CSVs to Saved/TrajectoryCapture/<stamp>/."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			using namespace TrajectoryHeatmapCommands;

			const float Seconds = ArgAsFloat(Args, 0, 30.0f);
			const int32 FloorID = ArgAsInt(Args, 1, 0);
			const int32 MaxRows = ArgAsInt(Args, 2, 4'000'000);

			const FString Error = FTrajectoryCaptureRecorder::Get().Arm(World, FloorID, Seconds, MaxRows);
			if (!Error.IsEmpty())
			{
				UE_LOG(LogMobiusTrajectoryCmd, Error, TEXT("Capture could not start: %s"), *Error);
				return;
			}

			UE_LOG(LogMobiusTrajectoryCmd, Display,
				TEXT("Capturing floor %d for %.1f s of sim time. Playback started from t=0; the sim pauses itself at the end."),
				FloorID, Seconds);
		}));

// -------------------------------------------------------------------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GMobiusHeatmapCaptureAbort(
	TEXT("mobius.Heatmap.CaptureAbort"),
	TEXT("Stop a running capture early and write whatever has been recorded so far."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& /*Args*/, UWorld* /*World*/)
		{
			if (!FTrajectoryCaptureRecorder::Get().IsArmed())
			{
				UE_LOG(LogMobiusTrajectoryCmd, Warning, TEXT("No capture is running."));
				return;
			}
			FTrajectoryCaptureRecorder::Get().Abort();
		}));

#endif // !UE_BUILD_SHIPPING
