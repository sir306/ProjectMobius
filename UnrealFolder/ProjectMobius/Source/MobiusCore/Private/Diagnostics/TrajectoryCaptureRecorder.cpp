// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// TrajectoryCaptureRecorder.cpp — see the header for what each stage records and why.
//
#if !UE_BUILD_SHIPPING

#include "Diagnostics/TrajectoryCaptureRecorder.h"

#include "Engine/World.h"
#include "EngineUtils.h" // TActorIterator
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AssertionMacros.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Actors/HeatmapPixelTextureVisualizer.h"
#include "Subsystems/HeatmapSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "DynamicPixelRenderingTexture.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusTrajectoryCapture, Log, All);

namespace
{
	enum ECollectFlags : uint8
	{
		CF_RenderAgent     = 1 << 0,
		CF_ReadyToDestroy  = 1 << 1,
		CF_HasPrevious     = 1 << 2,
		CF_PositionChanged = 1 << 3,
		CF_SegmentEmitted  = 1 << 4,
	};

	/**
	 * Buffered CSV writer. Rows are appended to a 1 MB string and serialised as UTF-8 when it fills,
	 * so a multi-million-row stream never materialises in memory as one giant FString.
	 */
	class FCsvWriter
	{
	public:
		explicit FCsvWriter(const FString& Path)
			: Ar(IFileManager::Get().CreateFileWriter(*Path))
		{
			Buffer.Reserve(FlushThreshold * 2);
		}

		~FCsvWriter()
		{
			Flush();
			if (Ar)
			{
				Ar->Close();
			}
		}

		bool IsValid() const { return Ar.IsValid(); }

		void Line(const FString& Text)
		{
			Buffer += Text;
			Buffer += TEXT("\n");
			if (Buffer.Len() >= FlushThreshold)
			{
				Flush();
			}
		}

	private:
		void Flush()
		{
			if (!Ar || Buffer.IsEmpty())
			{
				return;
			}
			// Cast through void so this does not depend on whether Get() yields ANSICHAR or UTF8CHAR.
			FTCHARToUTF8 Utf8(*Buffer);
			Ar->Serialize(const_cast<void*>(static_cast<const void*>(Utf8.Get())), Utf8.Length());
			Buffer.Reset();
		}

		static constexpr int32 FlushThreshold = 1 << 20;

		TUniquePtr<FArchive> Ar;
		FString Buffer;
	};

	FORCEINLINE FVector3f ToFloat(const FVector& V)
	{
		return FVector3f(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}
}

FTrajectoryCaptureRecorder& FTrajectoryCaptureRecorder::Get()
{
	static FTrajectoryCaptureRecorder Instance;
	return Instance;
}

FString FTrajectoryCaptureRecorder::Arm(UWorld* World, int32 InFloorID, float InDurationSeconds, int32 InMaxRowsPerStream,
                                       AHeatmapPixelTextureVisualizer* InTarget)
{
	// Every hook site is game-thread-only, which is what lets the armed flag be a plain bool.
	check(IsInGameThread());

	if (bArmed)
	{
		return TEXT("a capture is already running - stop it with mobius.Heatmap.CaptureAbort");
	}
	if (!World)
	{
		return TEXT("no world - run this in PIE or a -game session");
	}

	UHeatmapSubsystem* Subsystem = World->GetSubsystem<UHeatmapSubsystem>();
	if (!Subsystem)
	{
		return TEXT("no UHeatmapSubsystem in this world");
	}
	UTimeDilationSubSystem* Time = World->GetSubsystem<UTimeDilationSubSystem>();
	if (!Time)
	{
		return TEXT("no UTimeDilationSubSystem in this world");
	}

	// An explicit target settles it. The FloorID search below is a heuristic, and a heuristic is only
	// sound while the world holds one heatmap per floor. An automation session holds several, all on
	// FloorID 0 and all with trajectory mode on, so the search returned whichever came first out of
	// TActorIterator - and the capture then described the wrong heatmap without saying so. The summary
	// still records the actor location and texture size, which is how that was eventually caught.
	AHeatmapPixelTextureVisualizer* Target = InTarget;

	if (!IsValid(Target))
	{
		// Prefer an exact FloorID match; otherwise fall back to the physically lowest heatmap, which is
		// what "ground floor" means when the IDs do not start at zero.
		AHeatmapPixelTextureVisualizer* Exact = nullptr;
		AHeatmapPixelTextureVisualizer* Lowest = nullptr;
		int32 HeatmapCount = 0;
		for (TActorIterator<AHeatmapPixelTextureVisualizer> It(World); It; ++It)
		{
			AHeatmapPixelTextureVisualizer* Heatmap = *It;
			if (!IsValid(Heatmap))
			{
				continue;
			}
			++HeatmapCount;
			if (Heatmap->FloorID == InFloorID && !Exact)
			{
				Exact = Heatmap;
			}
			if (!Lowest || Heatmap->MeshOriginLocation.Z < Lowest->MeshOriginLocation.Z)
			{
				Lowest = Heatmap;
			}
		}

		Target = Exact ? Exact : Lowest;
		if (!Target)
		{
			return FString::Printf(TEXT("no heatmap actors in this world (searched %d)"), HeatmapCount);
		}
		if (!Exact)
		{
			UE_LOG(LogMobiusTrajectoryCapture, Warning,
				TEXT("No heatmap with FloorID %d; falling back to the lowest one (FloorID %d, origin Z %.2f)."),
				InFloorID, Target->FloorID, Target->MeshOriginLocation.Z);
		}
		else if (HeatmapCount > 1)
		{
			UE_LOG(LogMobiusTrajectoryCapture, Warning,
				TEXT("%d heatmap actors in this world and no explicit target; captured the first with ")
				TEXT("FloorID %d (at %s). Pass a target if that is not the one you meant."),
				HeatmapCount, InFloorID, *Target->GetActorLocation().ToCompactString());
		}
	}

	// Trajectory mode is the thing being measured, so turn it on rather than failing. Enabling also
	// clears the accumulation, which is exactly the clean slate this capture wants.
	if (!Target->bTrajectoryHeatmap)
	{
		Subsystem->SetTrajectoryHeatmapsEnabled(true);
		UE_LOG(LogMobiusTrajectoryCapture, Display, TEXT("Trajectory mode was off; enabled it for the capture."));
	}

	// PreviouslyPaused = 0 keeps the sim paused through the scrub; we unpause deliberately below so
	// the first captured frame is the first frame of playback and not a partial one mid-seek.
	Time->OverrideCurrentTime(0.0f, /*PreviouslyPaused*/ 0);
	Subsystem->ClearTrajectoryHeatmaps();

	TargetHeatmap = Target;
	TimeDilation = Time;
	TargetFloorID = Target->FloorID;
	TargetOriginZ = static_cast<float>(Target->MeshOriginLocation.Z);
	DurationSeconds = FMath::Max(InDurationSeconds, 0.1f);
	MaxRowsPerStream = FMath::Max(InMaxRowsPerStream, 1000);
	StartSimTime = Time->GetCurrentSimTime();
	FrameCounter = 0;
	FlushSequence = 0;
	DroppedCollect = DroppedFilter = DroppedRaster = 0;

	CollectRows.Reset();
	FlushRows.Reset();
	FilterRows.Reset();
	RasterRows.Reset();
	CollectRows.Reserve(FMath::Min(MaxRowsPerStream, 1'500'000));
	FilterRows.Reserve(FMath::Min(MaxRowsPerStream, 400'000));
	RasterRows.Reserve(FMath::Min(MaxRowsPerStream, 400'000));
	FlushRows.Reserve(8'192);

	bArmed = true; // last, so nothing above can be recorded as part of the window
	Time->SetSimulationPaused(false);

	UE_LOG(LogMobiusTrajectoryCapture, Display,
		TEXT("Capture armed: floor %d, origin Z %.2f, band [%.2f, %.2f], %.1f s of sim time from t=%.3f."),
		TargetFloorID, TargetOriginZ, TargetOriginZ, TargetOriginZ + Target->MaxAddHeight,
		DurationSeconds, StartSimTime);

	return FString();
}

void FTrajectoryCaptureRecorder::Tick(float SimTime)
{
	if (!bArmed)
	{
		return;
	}
	++FrameCounter;

	if (!TargetHeatmap.IsValid() || !TimeDilation.IsValid())
	{
		Finish(TEXT("target heatmap or time subsystem was destroyed"));
		return;
	}
	if (SimTime - StartSimTime >= DurationSeconds)
	{
		Finish(TEXT("window elapsed"));
	}
}

void FTrajectoryCaptureRecorder::Abort()
{
	if (bArmed)
	{
		Finish(TEXT("aborted by console command"));
	}
}

void FTrajectoryCaptureRecorder::RecordCollect(float SimTime, int32 EntityID, const FVector& Location,
                                               bool bRenderAgent, bool bReadyToDestroy, bool bHasPrevious,
                                               bool bPositionChanged, bool bSegmentEmitted)
{
	if (CollectRows.Num() >= MaxRowsPerStream)
	{
		++DroppedCollect;
		return;
	}
	uint8 Flags = 0;
	Flags |= bRenderAgent ? CF_RenderAgent : 0;
	Flags |= bReadyToDestroy ? CF_ReadyToDestroy : 0;
	Flags |= bHasPrevious ? CF_HasPrevious : 0;
	Flags |= bPositionChanged ? CF_PositionChanged : 0;
	Flags |= bSegmentEmitted ? CF_SegmentEmitted : 0;
	CollectRows.Add({ SimTime, FrameCounter, EntityID, ToFloat(Location), Flags });
}

void FTrajectoryCaptureRecorder::RecordFlush(float SimTime, int32 SegmentCount, bool bTrajectoryActive)
{
	FlushRows.Add({ SimTime, FrameCounter, ++FlushSequence, SegmentCount, static_cast<uint8>(bTrajectoryActive ? 1 : 0) });
}

void FTrajectoryCaptureRecorder::RecordFilter(float SimTime, int32 FloorID, float OriginZ, float MaxAddHeight,
                                              const FVector& Start, const FVector& End, bool bKept)
{
	if (FloorID != TargetFloorID)
	{
		return;
	}
	if (FilterRows.Num() >= MaxRowsPerStream)
	{
		++DroppedFilter;
		return;
	}
	// Origin/band are constant per floor and reported in summary.txt, so they stay out of the rows.
	FilterRows.Add({ SimTime, FrameCounter, ToFloat(Start), ToFloat(End), -1, -1, -1, -1,
	                 static_cast<uint8>(bKept ? 1 : 0) });
}

void FTrajectoryCaptureRecorder::RecordRaster(float SimTime, int32 FloorID, const FVector& Start, const FVector& End,
                                              FIntPoint StartTexel, FIntPoint EndTexel, bool bDrawn)
{
	if (FloorID != TargetFloorID)
	{
		return;
	}
	if (RasterRows.Num() >= MaxRowsPerStream)
	{
		++DroppedRaster;
		return;
	}
	RasterRows.Add({ SimTime, FrameCounter, ToFloat(Start), ToFloat(End),
	                 StartTexel.X, StartTexel.Y, EndTexel.X, EndTexel.Y,
	                 static_cast<uint8>(bDrawn ? 1 : 0) });
}

void FTrajectoryCaptureRecorder::Finish(const TCHAR* Reason)
{
	// Disarm before anything else: the texture readback below must not be recorded as part of the run.
	bArmed = false;

	const float EndSimTime = TimeDilation.IsValid() ? TimeDilation->GetCurrentSimTime() : StartSimTime;
	if (UTimeDilationSubSystem* Time = TimeDilation.Get())
	{
		Time->SetSimulationPaused(true);
	}

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString RelativeDir = FString::Printf(TEXT("TrajectoryCapture/%s"), *Stamp);
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TrajectoryCapture"), Stamp);
	IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);

	int32 Touched = 0;
	int32 PeakByte = 0;
	const bool bTextureOk = WriteTextureDump(FPaths::Combine(Directory, TEXT("texture.csv")), Touched, PeakByte);

	WriteTexturePngs(Directory, RelativeDir, PeakByte);
	WriteAllStreams(Directory);

	// summary.txt carries everything needed to interpret the CSVs without re-reading the source.
	FString Summary;
	Summary += FString::Printf(TEXT("reason            : %s\n"), Reason);
	Summary += FString::Printf(TEXT("floor id          : %d\n"), TargetFloorID);
	Summary += FString::Printf(TEXT("floor origin Z    : %.4f\n"), TargetOriginZ);
	if (const AHeatmapPixelTextureVisualizer* Heatmap = TargetHeatmap.Get())
	{
		Summary += FString::Printf(TEXT("z band            : [%.4f, %.4f]  (MaxAddHeight %.2f)\n"),
			TargetOriginZ, TargetOriginZ + Heatmap->MaxAddHeight, Heatmap->MaxAddHeight);
		Summary += FString::Printf(TEXT("actor location    : %s\n"), *Heatmap->GetActorLocation().ToString());
		Summary += FString::Printf(TEXT("trajectory mode   : %s\n"), Heatmap->bTrajectoryHeatmap ? TEXT("on") : TEXT("OFF"));
		if (const UDynamicPixelRenderingTexture* Texture = Heatmap->GetTrajectoryAccumulationTextureForTesting())
		{
			const FVector2D Size = Texture->GetDynamicTextureSize();
			Summary += FString::Printf(TEXT("texture           : %dx%d\n"),
				FMath::TruncToInt(Size.X), FMath::TruncToInt(Size.Y));
		}
	}
	Summary += FString::Printf(TEXT("sim time          : %.4f -> %.4f  (%.4f s)\n"),
		StartSimTime, EndSimTime, EndSimTime - StartSimTime);
	Summary += FString::Printf(TEXT("frames            : %d\n"), FrameCounter);
	Summary += FString::Printf(TEXT("collect rows      : %d  (dropped %d)\n"), CollectRows.Num(), DroppedCollect);
	Summary += FString::Printf(TEXT("flush rows        : %d\n"), FlushRows.Num());
	Summary += FString::Printf(TEXT("filter rows       : %d  (dropped %d)\n"), FilterRows.Num(), DroppedFilter);
	Summary += FString::Printf(TEXT("raster rows       : %d  (dropped %d)\n"), RasterRows.Num(), DroppedRaster);
	Summary += FString::Printf(TEXT("texels touched    : %d\n"), Touched);
	Summary += FString::Printf(TEXT("peak red byte     : %d %s\n"), PeakByte,
		PeakByte >= 255 ? TEXT("(SATURATED - counts above this are lost)") : TEXT(""));
	if (!bTextureOk)
	{
		Summary += TEXT("texture.csv       : FAILED (no accumulation texture)\n");
	}
	Summary += TEXT("\nimages\n");
	Summary += TEXT("  accumulation_los.png    : the app's own LOS colourisation of the accumulation buffer\n");
	Summary += FString::Printf(
		TEXT("  accumulation_scaled.png : greyscale, auto-normalised so 255 == peak byte %d.\n"
		     "                            grey g  =>  raw byte = g * %d / 255\n"), PeakByte, PeakByte);

	FFileHelper::SaveStringToFile(Summary, *FPaths::Combine(Directory, TEXT("summary.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogMobiusTrajectoryCapture, Display,
		TEXT("Capture finished (%s). %d collect / %d flush / %d filter / %d raster rows, %d texels, peak %d. -> %s"),
		Reason, CollectRows.Num(), FlushRows.Num(), FilterRows.Num(), RasterRows.Num(), Touched, PeakByte, *Directory);

	if (DroppedCollect || DroppedFilter || DroppedRaster)
	{
		UE_LOG(LogMobiusTrajectoryCapture, Warning,
			TEXT("Row cap hit - dropped %d collect, %d filter, %d raster rows. Re-run with a larger MaxRows."),
			DroppedCollect, DroppedFilter, DroppedRaster);
	}

	// Free the resident rows; a 30 s capture can hold tens of MB.
	CollectRows.Empty();
	FlushRows.Empty();
	FilterRows.Empty();
	RasterRows.Empty();
	TargetHeatmap.Reset();
	TimeDilation.Reset();
}

void FTrajectoryCaptureRecorder::WriteAllStreams(const FString& Directory)
{
	{
		FCsvWriter W(FPaths::Combine(Directory, TEXT("collect.csv")));
		if (W.IsValid())
		{
			W.Line(TEXT("sim_time,frame,entity_id,x,y,z,render_agent,ready_to_destroy,has_previous,position_changed,segment_emitted"));
			for (const FCollectRow& R : CollectRows)
			{
				W.Line(FString::Printf(TEXT("%.4f,%d,%d,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d"),
					R.SimTime, R.Frame, R.EntityID, R.Location.X, R.Location.Y, R.Location.Z,
					(R.Flags & CF_RenderAgent) ? 1 : 0,
					(R.Flags & CF_ReadyToDestroy) ? 1 : 0,
					(R.Flags & CF_HasPrevious) ? 1 : 0,
					(R.Flags & CF_PositionChanged) ? 1 : 0,
					(R.Flags & CF_SegmentEmitted) ? 1 : 0));
			}
		}
	}
	{
		FCsvWriter W(FPaths::Combine(Directory, TEXT("flush.csv")));
		if (W.IsValid())
		{
			W.Line(TEXT("sim_time,frame,sequence,segment_count,trajectory_active"));
			for (const FFlushRow& R : FlushRows)
			{
				W.Line(FString::Printf(TEXT("%.4f,%d,%d,%d,%d"),
					R.SimTime, R.Frame, R.Sequence, R.SegmentCount, R.bTrajectoryActive));
			}
		}
	}
	{
		FCsvWriter W(FPaths::Combine(Directory, TEXT("filter.csv")));
		if (W.IsValid())
		{
			W.Line(TEXT("sim_time,frame,start_x,start_y,start_z,end_x,end_y,end_z,kept"));
			for (const FSegmentRow& R : FilterRows)
			{
				W.Line(FString::Printf(TEXT("%.4f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d"),
					R.SimTime, R.Frame, R.Start.X, R.Start.Y, R.Start.Z, R.End.X, R.End.Y, R.End.Z, R.bAccepted));
			}
		}
	}
	{
		FCsvWriter W(FPaths::Combine(Directory, TEXT("raster.csv")));
		if (W.IsValid())
		{
			W.Line(TEXT("sim_time,frame,start_x,start_y,start_z,end_x,end_y,end_z,start_texel_x,start_texel_y,end_texel_x,end_texel_y,drawn"));
			for (const FSegmentRow& R : RasterRows)
			{
				W.Line(FString::Printf(TEXT("%.4f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d"),
					R.SimTime, R.Frame, R.Start.X, R.Start.Y, R.Start.Z, R.End.X, R.End.Y, R.End.Z,
					R.StartX, R.StartY, R.EndX, R.EndY, R.bAccepted));
			}
		}
	}
}

void FTrajectoryCaptureRecorder::WriteTexturePngs(const FString& Directory, const FString& RelativeDir, int32 PeakByte) const
{
	const AHeatmapPixelTextureVisualizer* Heatmap = TargetHeatmap.Get();
	if (!Heatmap)
	{
		return;
	}
	UDynamicPixelRenderingTexture* Texture = Heatmap->GetTrajectoryAccumulationTextureMutableForTesting();
	if (!Texture)
	{
		return;
	}

	// 1. The app's own LOS colourisation, so this is directly comparable to what is on screen.
	//    It works on a copy of the buffer, so the accumulation is not disturbed.
	Texture->SaveDynamicTextureToPNG(RelativeDir / TEXT("accumulation_los.png"));

	// 2. Auto-normalised greyscale. Raw bytes in a short capture sit around 25-60 out of 255, which is
	//    almost black and useless to eyeball; scaling by the peak makes the actual structure - and any
	//    real hole in it - visible. The scale factor is recorded in summary.txt so it stays quantitative.
	const FVector2D Size = Texture->GetDynamicTextureSize();
	const int32 Width = FMath::TruncToInt(Size.X);
	const int32 Height = FMath::TruncToInt(Size.Y);
	if (Width <= 0 || Height <= 0 || PeakByte <= 0)
	{
		return;
	}

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const uint8 Red = Texture->GetRawPixelRed(X, Y);
			const uint8 Scaled = static_cast<uint8>(FMath::Clamp((Red * 255) / PeakByte, 0, 255));
			Pixels[Y * Width + X] = FColor(Scaled, Scaled, Scaled, 255);
		}
	}

	TArray64<uint8> PngData;
	FImageUtils::PNGCompressImageArray(Width, Height, Pixels, PngData);
	FFileHelper::SaveArrayToFile(PngData, *FPaths::Combine(Directory, TEXT("accumulation_scaled.png")));
}

bool FTrajectoryCaptureRecorder::WriteTextureDump(const FString& Path, int32& OutTouched, int32& OutPeak) const
{
	OutTouched = 0;
	OutPeak = 0;

	const AHeatmapPixelTextureVisualizer* Heatmap = TargetHeatmap.Get();
	const UDynamicPixelRenderingTexture* Texture =
		Heatmap ? Heatmap->GetTrajectoryAccumulationTextureForTesting() : nullptr;
	if (!Texture)
	{
		return false;
	}

	const FVector2D Size = Texture->GetDynamicTextureSize();
	const int32 Width = FMath::TruncToInt(Size.X);
	const int32 Height = FMath::TruncToInt(Size.Y);

	FCsvWriter W(Path);
	if (!W.IsValid())
	{
		return false;
	}
	W.Line(TEXT("texel_x,texel_y,red_byte,normalised"));
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const uint8 Red = Texture->GetRawPixelRed(X, Y);
			if (Red == 0)
			{
				continue; // sparse: a full dump is a million rows of mostly zeroes
			}
			++OutTouched;
			OutPeak = FMath::Max(OutPeak, static_cast<int32>(Red));
			W.Line(FString::Printf(TEXT("%d,%d,%u,%.5f"), X, Y, Red, static_cast<float>(Red) / 255.0f));
		}
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
