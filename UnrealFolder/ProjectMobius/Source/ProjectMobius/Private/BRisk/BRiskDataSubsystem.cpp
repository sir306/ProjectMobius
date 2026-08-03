// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskDataSubsystem.h"
#include "BRisk/BRiskHazardVisualizer.h"
#include "BRisk/BRiskSmokeVisualizer.h"
#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "IMobiusErrorReporter.h"                          // A19-b: surface import failures to the user
#include "MassAI/SubSystems/AgentDataSubsystem.h"
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "Algo/Reverse.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

#include <array>
#include <vector>

// Vendored at Source/MobiusCore/ThirdParty; reachable here because MobiusCore exposes that
// directory as a PublicIncludePath and ProjectMobius depends on MobiusCore.
#include <earcut_hpp/earcut.hpp>

DEFINE_LOG_CATEGORY_STATIC(LogBRiskDataSubsystem, Log, All);

namespace
{
	constexpr double BRiskOpticalDensityToExtinctionPerCm = 0.023025850929940457;

	void LogBRiskScenarioData(
		const TCHAR* Prefix,
		const FBRiskScenarioData& Data,
		bool bLogReferencedFiles,
		bool bLogRooms,
		bool bLogFires,
		bool bLogVents,
		bool bLogZoneTables,
		bool bLogSeries,
		bool bLogSamples)
	{
		if (bLogReferencedFiles)
		{
			for (int32 FileIndex = 0; FileIndex < Data.ReferencedFiles.Num(); ++FileIndex)
			{
				UE_LOG(LogBRiskDataSubsystem, Log,
					TEXT("%s referencedFile[%d]=%s"),
					Prefix,
					FileIndex,
					*Data.ReferencedFiles[FileIndex]);
			}
		}

		if (bLogRooms)
		{
			for (int32 RoomIndex = 0; RoomIndex < Data.Rooms.Num(); ++RoomIndex)
			{
				const FBRiskRoomGeometry& Room = Data.Rooms[RoomIndex];
				UE_LOG(LogBRiskDataSubsystem, Log,
					TEXT("%s room[%d]: id=%d label=\"%s\" origin=%s size=%s"),
					Prefix,
					RoomIndex,
					Room.RoomId,
					*Room.Label,
					*Room.Origin.ToString(),
					*Room.Size.ToString());
			}
		}

		if (bLogFires)
		{
			for (int32 FireIndex = 0; FireIndex < Data.Fires.Num(); ++FireIndex)
			{
				const FBRiskFireGeometry& Fire = Data.Fires[FireIndex];
				UE_LOG(LogBRiskDataSubsystem, Log,
					TEXT("%s fire[%d]: roomId=%d location=%s"),
					Prefix,
					FireIndex,
					Fire.RoomId,
					*Fire.Location.ToString());
			}
		}

		if (bLogVents)
		{
			for (int32 VentIndex = 0; VentIndex < Data.Vents.Num(); ++VentIndex)
			{
				const FBRiskVentGeometry& Vent = Data.Vents[VentIndex];
				UE_LOG(LogBRiskDataSubsystem, Log,
					TEXT("%s vent[%d]: fromRoom=%d toRoom=%d face=%d width=%g offset=%g sill=%g height=%g"),
					Prefix,
					VentIndex,
					Vent.FromRoomId,
					Vent.ToRoomId,
					Vent.Face,
					Vent.Width,
					Vent.Offset,
					Vent.SillHeight,
					Vent.Height);
			}
		}

		if (bLogZoneTables)
		{
			for (int32 ZoneIndex = 0; ZoneIndex < Data.ZoneTables.Num(); ++ZoneIndex)
			{
				const FBRiskZoneTable& ZoneTable = Data.ZoneTables[ZoneIndex];
				UE_LOG(LogBRiskDataSubsystem, Log,
					TEXT("%s zone[%d]: csv=%s timeSamples=%d series=%d"),
					Prefix,
					ZoneIndex,
					*ZoneTable.SourceCsvPath,
					ZoneTable.TimeSeconds.Num(),
					ZoneTable.Series.Num());
			}
		}

		if (bLogSeries)
		{
			for (int32 ZoneIndex = 0; ZoneIndex < Data.ZoneTables.Num(); ++ZoneIndex)
			{
				const FBRiskZoneTable& ZoneTable = Data.ZoneTables[ZoneIndex];
				for (int32 SeriesIndex = 0; SeriesIndex < ZoneTable.Series.Num(); ++SeriesIndex)
				{
					const FBRiskSeries& Series = ZoneTable.Series[SeriesIndex];
					UE_LOG(LogBRiskDataSubsystem, Log,
						TEXT("%s zone[%d] series[%d]: name=%s unit=%s samples=%d"),
						Prefix,
						ZoneIndex,
						SeriesIndex,
						*Series.Name,
						*Series.Unit,
						Series.Values.Num());
				}
			}
		}

		if (bLogSamples)
		{
			for (int32 ZoneIndex = 0; ZoneIndex < Data.ZoneTables.Num(); ++ZoneIndex)
			{
				const FBRiskZoneTable& ZoneTable = Data.ZoneTables[ZoneIndex];
				for (int32 SampleIndex = 0; SampleIndex < ZoneTable.TimeSeconds.Num(); ++SampleIndex)
				{
					FString SampleLine = FString::Printf(
						TEXT("%s zone[%d] sample[%d]: Time=%g"),
						Prefix,
						ZoneIndex,
						SampleIndex,
						ZoneTable.TimeSeconds[SampleIndex]);

					for (const FBRiskSeries& Series : ZoneTable.Series)
					{
						if (Series.Values.IsValidIndex(SampleIndex))
						{
							SampleLine += FString::Printf(TEXT(" %s=%g"), *Series.Name, Series.Values[SampleIndex]);
						}
					}

					UE_LOG(LogBRiskDataSubsystem, Log, TEXT("%s"), *SampleLine);
				}
			}
		}
	}
}

void UBRiskDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UProjectMobiusGameInstance* GI =
		Cast<UProjectMobiusGameInstance>(GetWorld()->GetGameInstance()))
	{
		GI->OnBRiskFileChanged.AddDynamic(this, &UBRiskDataSubsystem::OnSmvFileChanged);
		UE_LOG(LogBRiskDataSubsystem, Log,
			TEXT("Bound to UProjectMobiusGameInstance::OnBRiskFileChanged"));
	}
}

void UBRiskDataSubsystem::Deinitialize()
{
	UnbindSmokeTimeDelegate();
	ClearSmokeVolumes();
	ClearHazardVisuals();

	if (UWorld* World = GetWorld())
	{
		if (UProjectMobiusGameInstance* GI =
			Cast<UProjectMobiusGameInstance>(World->GetGameInstance()))
		{
			GI->OnBRiskFileChanged.RemoveDynamic(this, &UBRiskDataSubsystem::OnSmvFileChanged);
		}
	}
	Super::Deinitialize();
}

void UBRiskDataSubsystem::LoadScenarioFromSmv(const FString& SmvFilePath)
{
	const int32 RequestGeneration = ++LoadGeneration;
	ClearSmokeVolumes();
	ClearHazardVisuals();
	ClearRoomGeometry();
	ScenarioData = FBRiskScenarioData();
	LastError.Reset();
	ActiveSmvPath = SmvFilePath;
	bIsLoading = false;
	OnBRiskScenarioCleared.Broadcast();

	if (SmvFilePath.IsEmpty())
	{
		LastError = TEXT("LoadScenarioFromSmv called with an empty path.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);

		// A19-b: Warning with NO prompt, deliberately. An empty path is a caller bug, not something the
		// user can act on, so it belongs in the log window rather than in a window over their work — the
		// same call it gets in BRiskEgressSubsystem's defaulted-endpoints report.
		if (IMobiusErrorReporter* Reporter = IMobiusErrorReporter::Get(this))
		{
			Reporter->ReportError(
				NSLOCTEXT("MobiusBRisk", "BRiskImportTitleBar", "B-Risk Import Error"),
				NSLOCTEXT("MobiusBRisk", "BRiskEmptyPathTitle", "No scenario file supplied"),
				NSLOCTEXT("MobiusBRisk", "BRiskEmptyPathBody",
					"A B-Risk load was requested with an empty file path, so nothing was loaded."),
				NSLOCTEXT("MobiusBRisk", "BRiskImportSource", "BRiskDataSubsystem"),
				EMobiusErrorSeverity::Warning,
				/*bShowPrompt=*/false);
		}

		OnBRiskScenarioLoaded.Broadcast(false);
		return;
	}

	bIsLoading = true;
	UE_LOG(LogBRiskDataSubsystem, Log, TEXT("Starting B-Risk load: %s"), *SmvFilePath);

	TWeakObjectPtr<UBRiskDataSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, SmvFilePath, RequestGeneration]()
	{
		FBRiskScenarioData ParsedData;
		FString ErrorMessage;
		const bool bSuccess =
			FBRiskDataImporter::ImportScenarioFromSmv(SmvFilePath, ParsedData, &ErrorMessage);

		AsyncTask(ENamedThreads::GameThread,
			[WeakThis, RequestGeneration, bSuccess, ParsedData = MoveTemp(ParsedData), ErrorMessage]() mutable
		{
			if (!WeakThis.IsValid())
			{
				return;
			}

			UBRiskDataSubsystem* Self = WeakThis.Get();
			if (RequestGeneration != Self->LoadGeneration)
			{
				UE_LOG(LogBRiskDataSubsystem, Verbose,
					TEXT("Ignoring stale B-Risk load generation %d"), RequestGeneration);
				return;
			}

			Self->bIsLoading = false;

			if (bSuccess)
			{
				Self->ScenarioData = MoveTemp(ParsedData);
				Self->LastError.Reset();
				Self->bHasWarnedMissingSmokeSeries = false;
				Self->bHasWarnedMissingSmokeComponent = false;
				UE_LOG(LogBRiskDataSubsystem, Log,
					TEXT("B-Risk scenario loaded: rooms=%d  fires=%d  vents=%d  zoneTables=%d"),
					Self->ScenarioData.Rooms.Num(),
					Self->ScenarioData.Fires.Num(),
					Self->ScenarioData.Vents.Num(),
					Self->ScenarioData.ZoneTables.Num());

				if (Self->bConfigureSharedPlaybackOnLoad)
				{
					Self->ConfigurePlaybackFromScenario();
				}

				if (Self->bAutoGenerateRoomGeometryOnLoad)
				{
					Self->GenerateAndLoadRoomGeometry();
				}

				if (Self->bAutoGenerateSmokeVolumesOnLoad)
				{
					Self->GenerateAndLoadSmokeVolumes();
				}

				if (Self->bAutoGenerateHazardVisualsOnLoad)
				{
					Self->GenerateAndLoadHazardVisuals();
				}
			}
			else
			{
				Self->ClearSmokeVolumes();
				Self->ScenarioData = FBRiskScenarioData();
				Self->LastError = ErrorMessage;
				UE_LOG(LogBRiskDataSubsystem, Error,
					TEXT("B-Risk scenario load failed: %s"), *ErrorMessage);

				// A19-b: until now this path was LOG-ONLY. Every hard failure in FBRiskDataImporter —
				// missing/unreadable .smv, wrong extension, a zone CSV with no Time column or no data rows,
				// a malformed numeric cell, a series/time length mismatch, no ZONE references — landed here,
				// set LastError, and showed the user absolutely nothing. The scenario simply did not appear.
				// ULoadBRiskDataWidget only validates the file EXTENSION before handing off, so it cannot
				// cover any of these. ErrorMessage is already a specific, path-carrying sentence from the
				// importer, so it is surfaced verbatim rather than re-worded into something vaguer.
				if (IMobiusErrorReporter* Reporter = IMobiusErrorReporter::Get(Self))
				{
					Reporter->ReportError(
						NSLOCTEXT("MobiusBRisk", "BRiskImportTitleBar", "B-Risk Import Error"),
						NSLOCTEXT("MobiusBRisk", "BRiskImportTitle", "Scenario could not be loaded"),
						FText::FromString(ErrorMessage),
						NSLOCTEXT("MobiusBRisk", "BRiskImportSource", "BRiskDataSubsystem"),
						EMobiusErrorSeverity::Error);
				}
			}

			Self->OnBRiskScenarioLoaded.Broadcast(bSuccess);
		});
	});
}

void UBRiskDataSubsystem::ClearScenario()
{
	++LoadGeneration;
	ClearSmokeVolumes();
	ClearHazardVisuals();
	ClearRoomGeometry();
	ScenarioData = FBRiskScenarioData();
	LastError.Reset();
	ActiveSmvPath.Reset();
	bIsLoading = false;
	OnBRiskScenarioCleared.Broadcast();
}

bool UBRiskDataSubsystem::HasScenarioData() const
{
	return ScenarioData.ZoneTables.Num() > 0
		&& ScenarioData.ZoneTables[0].TimeSeconds.Num() > 0;
}

int32 UBRiskDataSubsystem::GetZoneTimeSampleCount(int32 ZoneTableIndex) const
{
	return ScenarioData.ZoneTables.IsValidIndex(ZoneTableIndex)
		? ScenarioData.ZoneTables[ZoneTableIndex].TimeSeconds.Num()
		: 0;
}

int32 UBRiskDataSubsystem::GetZoneSeriesCount(int32 ZoneTableIndex) const
{
	return ScenarioData.ZoneTables.IsValidIndex(ZoneTableIndex)
		? ScenarioData.ZoneTables[ZoneTableIndex].Series.Num()
		: 0;
}

bool UBRiskDataSubsystem::GetRoomGeometry(int32 RoomIndex, int32& RoomId, FVector& Origin, FVector& Size, FString& Label) const
{
	if (!ScenarioData.Rooms.IsValidIndex(RoomIndex))
	{
		RoomId = INDEX_NONE;
		Origin = FVector::ZeroVector;
		Size = FVector::ZeroVector;
		Label.Reset();
		return false;
	}

	const FBRiskRoomGeometry& Room = ScenarioData.Rooms[RoomIndex];
	RoomId = Room.RoomId;
	Origin = Room.Origin;
	Size = Room.Size;
	Label = Room.Label;
	return true;
}

bool UBRiskDataSubsystem::GetFireGeometry(int32 FireIndex, int32& RoomId, FVector& Location) const
{
	if (!ScenarioData.Fires.IsValidIndex(FireIndex))
	{
		RoomId = INDEX_NONE;
		Location = FVector::ZeroVector;
		return false;
	}

	const FBRiskFireGeometry& Fire = ScenarioData.Fires[FireIndex];
	RoomId = Fire.RoomId;
	Location = Fire.Location;
	return true;
}

bool UBRiskDataSubsystem::GetVentGeometry(
	int32 VentIndex,
	int32& FromRoomId,
	int32& ToRoomId,
	int32& Face,
	float& Width,
	float& Offset,
	float& SillHeight,
	float& Height) const
{
	if (!ScenarioData.Vents.IsValidIndex(VentIndex))
	{
		FromRoomId = INDEX_NONE;
		ToRoomId = INDEX_NONE;
		Face = INDEX_NONE;
		Width = 0.0f;
		Offset = 0.0f;
		SillHeight = 0.0f;
		Height = 0.0f;
		return false;
	}

	const FBRiskVentGeometry& Vent = ScenarioData.Vents[VentIndex];
	FromRoomId = Vent.FromRoomId;
	ToRoomId = Vent.ToRoomId;
	Face = Vent.Face;
	Width = static_cast<float>(Vent.Width);
	Offset = static_cast<float>(Vent.Offset);
	SillHeight = static_cast<float>(Vent.SillHeight);
	Height = static_cast<float>(Vent.Height);
	return true;
}

TArray<FString> UBRiskDataSubsystem::GetZoneSeriesNames(int32 ZoneTableIndex) const
{
	TArray<FString> SeriesNames;
	if (!ScenarioData.ZoneTables.IsValidIndex(ZoneTableIndex))
	{
		return SeriesNames;
	}

	const FBRiskZoneTable& ZoneTable = ScenarioData.ZoneTables[ZoneTableIndex];
	SeriesNames.Reserve(ZoneTable.Series.Num());
	for (const FBRiskSeries& Series : ZoneTable.Series)
	{
		SeriesNames.Add(Series.Name);
	}
	return SeriesNames;
}

bool UBRiskDataSubsystem::GetZoneTimeRange(int32 ZoneTableIndex, double& StartTimeSeconds, double& EndTimeSeconds) const
{
	if (!ScenarioData.ZoneTables.IsValidIndex(ZoneTableIndex)
		|| ScenarioData.ZoneTables[ZoneTableIndex].TimeSeconds.Num() == 0)
	{
		StartTimeSeconds = 0.0;
		EndTimeSeconds = 0.0;
		return false;
	}

	const TArray<double>& Times = ScenarioData.ZoneTables[ZoneTableIndex].TimeSeconds;
	StartTimeSeconds = Times[0];
	EndTimeSeconds = Times.Last();
	return true;
}

bool UBRiskDataSubsystem::SampleSeriesValue(int32 ZoneTableIndex, const FString& SeriesName, double TimeSeconds, double& OutValue) const
{
	return SampleSeriesAtTime(ZoneTableIndex, SeriesName, TimeSeconds, OutValue);
}

void UBRiskDataSubsystem::SetRoomGeometryScale(float InScale)
{
	RoomGeometryScale = FMath::Max(InScale, UE_KINDA_SMALL_NUMBER);
}

bool UBRiskDataSubsystem::BuildRoomGeometryMeshData(
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals) const
{
	FString ErrorMessage;
	const bool bBuilt = BuildRoomMeshDataFromRooms(
		ScenarioData.Rooms,
		ScenarioData.Vents,
		RoomGeometryScale,
		ScenarioData.RoomFrame,
		OutVertices,
		OutTriangles,
		OutNormals,
		&ErrorMessage);

	if (!bBuilt)
	{
		UE_LOG(LogBRiskDataSubsystem, Warning,
			TEXT("B-Risk room geometry build failed: %s"), *ErrorMessage);
	}

	return bBuilt;
}

bool UBRiskDataSubsystem::GenerateAndLoadRoomGeometry()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		LastError = TEXT("Cannot generate B-Risk room geometry without a valid world.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	if (!BuildRoomGeometryMeshData(Vertices, Triangles, Normals))
	{
		LastError = TEXT("No valid B-Risk room geometry is available to generate.");
		return false;
	}

	ARuntimeMeshBuilder* MeshBuilder = Cast<ARuntimeMeshBuilder>(
		UGameplayStatics::GetActorOfClass(World, ARuntimeMeshBuilder::StaticClass()));

	if (!MeshBuilder)
	{
		LastError = TEXT("No RuntimeMeshBuilder actor was found for B-Risk room geometry.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	MeshBuilder->GenerateMobiusMesh(Vertices, Triangles, Normals);
	bRoomGeometryActive = true;
	UE_LOG(LogBRiskDataSubsystem, Log,
		TEXT("Generated B-Risk room geometry: rooms=%d vertices=%d triangles=%d scale=%g"),
		ScenarioData.Rooms.Num(),
		Vertices.Num(),
		Triangles.Num() / 3,
		RoomGeometryScale);

	OnBRiskGeometryReady.Broadcast();
	return true;
}

void UBRiskDataSubsystem::ClearRoomGeometry()
{
	// Only tear down geometry B-Risk itself generated; never touch a separately imported building.
	if (!bRoomGeometryActive)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (ARuntimeMeshBuilder* MeshBuilder = Cast<ARuntimeMeshBuilder>(
			UGameplayStatics::GetActorOfClass(World, ARuntimeMeshBuilder::StaticClass())))
		{
			MeshBuilder->ClearMobiusProceduralMesh();
		}
	}

	bRoomGeometryActive = false;
}

void UBRiskDataSubsystem::SetRoomGeometryEnabled(bool bEnabled)
{
	bAutoGenerateRoomGeometryOnLoad = bEnabled;

	// With no scenario loaded the flag simply applies on the next load. With one loaded, act now.
	if (!HasScenarioData())
	{
		return;
	}

	// TODO(b-risk geometry toggle / mobius builder): the OFF path below reloads the imported building
	// model, which can be expensive for large fbx/datasmith files. Before proceeding, prompt the user
	// with a confirmation warning (e.g. "Switching B-Risk room geometry off reloads the building model
	// '<name>' — for large models this may take a while. Proceed?") via UMobiusUserFeedbackSubsystem,
	// and only act on confirmation; on cancel, revert bAutoGenerateRoomGeometryOnLoad + the checkbox.

	if (bEnabled)
	{
		// Show B-Risk room geometry. GenerateMobiusMesh replaces whatever the shared RuntimeMeshBuilder
		// holds (including an imported building) with the B-Risk rooms regenerated from the loaded .smv.
		GenerateAndLoadRoomGeometry();
		return;
	}

	// OFF: the shared RuntimeMeshBuilder holds EITHER B-Risk rooms OR an imported building, so simply
	// clearing the rooms would leave nothing on screen. Instead RELOAD the previously selected building
	// mesh file (fbx/datasmith/ifc) so the model the room generation tore off the builder is rebuilt
	// from disk. Fall back to clearing the rooms only when no building file was ever selected.
	UWorld* World = GetWorld();
	const UProjectMobiusGameInstance* GameInst =
		World ? Cast<UProjectMobiusGameInstance>(World->GetGameInstance()) : nullptr;
	static const FString MeshPathPlaceholder(TEXT("Click Browse to choose file"));
	const FString MeshFilePath = GameInst ? GameInst->GetSimulationMeshFilePath() : FString();

	if (World && !MeshFilePath.IsEmpty() && MeshFilePath != MeshPathPlaceholder)
	{
		if (ARuntimeMeshBuilder* MeshBuilder = Cast<ARuntimeMeshBuilder>(
			UGameplayStatics::GetActorOfClass(World, ARuntimeMeshBuilder::StaticClass())))
		{
			// Rebuilds the building from its file (the same path UpdateMeshFileName reads from the
			// game instance). The builder now holds the building, not B-Risk rooms.
			MeshBuilder->UpdateMeshFileName();
			bRoomGeometryActive = false;
			return;
		}
	}

	// No building model to restore — just tear down the B-Risk rooms.
	ClearRoomGeometry();
}

void UBRiskDataSubsystem::SetUseBRiskTiming(bool bEnabled)
{
	bConfigureSharedPlaybackOnLoad = bEnabled;

	// Switch the active clock source live, keeping the current position + play/pause state.
	ApplyActiveTimeline(/*bResetToStart=*/false);
}

bool UBRiskDataSubsystem::GenerateAndLoadSmokeVolumes()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		LastError = TEXT("Cannot generate B-Risk smoke volumes without a valid world.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	if (ScenarioData.Rooms.Num() == 0)
	{
		LastError = TEXT("No B-Risk rooms are available for smoke volume generation.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	if (!SmokeVisualizerActor || SmokeVisualizerActor->IsActorBeingDestroyed())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("BRiskSmokeVisualizer");
		// Swapping the .smv mid-session destroys the previous visualizer, but Destroy()
		// only marks it Garbage — the actor keeps this name (and the Level as its outer)
		// until GC, which won't run inside the async-import window. SpawnActor's default
		// Required_Fatal name mode would then hit a name collision and crash via
		// UE_LOG(LogSpawn, Fatal). Requested makes the name unique on collision instead.
		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SmokeVisualizerActor = World->SpawnActor<ABRiskSmokeVisualizer>(
			ABRiskSmokeVisualizer::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	}

	if (!SmokeVisualizerActor)
	{
		LastError = TEXT("Failed to spawn B-Risk smoke visualizer actor.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	if (!SmokeVisualizerActor->ConfigureFromRooms(
		ScenarioData.Rooms, RoomGeometryScale, ScenarioData.RoomFrame))
	{
		LastError = TEXT("Failed to configure B-Risk smoke visualizer volumes.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	// Report the scenario's soot yield beside the albedo actually in use. The two are NOT connected
	// in code on purpose (see Mobius.BRisk.SmokeAlbedo); logging them together is what lets the
	// numbers be compared by eye, and is the groundwork for a calibrated mapping if one ever exists.
	{
		static const IConsoleVariable* AlbedoCVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("Mobius.BRisk.SmokeAlbedo"));
		UE_LOG(LogBRiskDataSubsystem, Log,
			TEXT("B-Risk smoke appearance: albedo=%.3f (Mobius.BRisk.SmokeAlbedo)  sootYield=%s g/g."),
			AlbedoCVar ? AlbedoCVar->GetFloat() : 0.5f,
			ScenarioData.bHasSootYield
				? *FString::SanitizeFloat(ScenarioData.SootYieldGPerG)
				: TEXT("absent from input1.xml"));
	}

	if (UTimeDilationSubSystem* TimeSubsystem = GetTimeDilationSubsystem())
	{
		TimeSubsystem->OnNewCurrentTime.RemoveDynamic(this, &UBRiskDataSubsystem::HandleNewSimulationTime);
		TimeSubsystem->OnNewCurrentTime.AddDynamic(this, &UBRiskDataSubsystem::HandleNewSimulationTime);
		TimeSubsystem->OnSimulationPauseChanged.RemoveDynamic(this, &UBRiskDataSubsystem::HandleSimulationPauseChanged);
		TimeSubsystem->OnSimulationPauseChanged.AddDynamic(this, &UBRiskDataSubsystem::HandleSimulationPauseChanged);
		SmokeVisualizerActor->SetSmokeSimulationPaused(TimeSubsystem->bIsPaused);
	}

	UE_LOG(LogBRiskDataSubsystem, Log,
		TEXT("Generated B-Risk smoke volumes: rooms=%d createdVolumes=%d zones=%d series=HGT_<roomId>,ULOD_<roomId>,LLOD_<roomId>,ULT_<roomId>,LLT_<roomId> scale=%g"),
		ScenarioData.Rooms.Num(),
		SmokeVisualizerActor->GetSmokeVolumeCount(),
		ScenarioData.ZoneTables.Num(),
		RoomGeometryScale);

	UpdateSmokeAtTime(0.0f);
	return true;
}

bool UBRiskDataSubsystem::GenerateAndLoadHazardVisuals()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		LastError = TEXT("Cannot generate B-Risk hazard visuals without a valid world.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	if (ScenarioData.Fires.Num() == 0 && ScenarioData.Sprinklers.Num() == 0 && ScenarioData.Vents.Num() == 0)
	{
		return false;
	}

	if (!HazardVisualizerActor || HazardVisualizerActor->IsActorBeingDestroyed())
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("BRiskHazardVisualizer");
		// See the smoke visualizer spawn above: a mid-session .smv reload leaves the
		// previous (Garbage, pre-GC) actor holding this name, so the default
		// Required_Fatal name mode would crash on the collision. Requested avoids it.
		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HazardVisualizerActor = World->SpawnActor<ABRiskHazardVisualizer>(
			ABRiskHazardVisualizer::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	}

	if (!HazardVisualizerActor)
	{
		LastError = TEXT("Failed to spawn B-Risk hazard visualizer actor.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	if (!HazardVisualizerActor->ConfigureFromScenario(
		ScenarioData.Rooms,
		ScenarioData.Fires,
		ScenarioData.Sprinklers,
		ScenarioData.Vents,
		RoomGeometryScale,
		ScenarioData.RoomFrame))
	{
		LastError = TEXT("Failed to configure B-Risk hazard visuals.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

	// Auto-scale the vent-flow colourbar to the scenario's actual upper-layer temperature range
	// (like Smokeview), so the hottest flows read red instead of everything mapping to teal.
	double MaxUpperTempC = 60.0; // floor so the colour range is never degenerate
	for (const FBRiskZoneTable& ZoneTable : ScenarioData.ZoneTables)
	{
		for (const FBRiskSeries& Series : ZoneTable.Series)
		{
			if (Series.Name.StartsWith(TEXT("ULT")))
			{
				for (const double Value : Series.Values)
				{
					MaxUpperTempC = FMath::Max(MaxUpperTempC, Value);
				}
			}
		}
	}
	HazardVisualizerActor->SetFlowTemperatureRange(20.0f, static_cast<float>(MaxUpperTempC));

	if (UTimeDilationSubSystem* TimeSubsystem = GetTimeDilationSubsystem())
	{
		TimeSubsystem->OnNewCurrentTime.RemoveDynamic(this, &UBRiskDataSubsystem::HandleNewSimulationTime);
		TimeSubsystem->OnNewCurrentTime.AddDynamic(this, &UBRiskDataSubsystem::HandleNewSimulationTime);
	}

	UE_LOG(LogBRiskDataSubsystem, Log,
		TEXT("Generated B-Risk hazard visuals: fires=%d sprinklers=%d createdComponents=%d scale=%g"),
		ScenarioData.Fires.Num(),
		ScenarioData.Sprinklers.Num(),
		HazardVisualizerActor->GetHazardVisualCount(),
		RoomGeometryScale);

	UpdateHazardVisualsAtTime(0.0f);
	return true;
}

bool UBRiskDataSubsystem::UpdateSmokeAtTime(float TimeSeconds)
{
	if (!SmokeVisualizerActor)
	{
		return false;
	}

	bool bUpdatedAny = false;
	for (int32 RoomIndex = 0; RoomIndex < ScenarioData.Rooms.Num(); ++RoomIndex)
	{
		double LayerHeight = 0.0;
		double UpperOpticalDensity = 0.0;
		double LowerOpticalDensity = 0.0;
		double UpperTemperatureC = 24.0;
		double LowerTemperatureC = 24.0;
		// B-Risk suffixes each room's layer channels with the room id (HGT_1, HGT_2, ...).
		const int32 RoomId = ScenarioData.Rooms[RoomIndex].RoomId;
		const bool bHasLayerHeight = SampleRoomChannelAtTime(RoomId, TEXT("HGT"), TimeSeconds, LayerHeight);
		const bool bHasUpperOpticalDensity = SampleRoomChannelAtTime(RoomId, TEXT("ULOD"), TimeSeconds, UpperOpticalDensity);
		const bool bHasLowerOpticalDensity = SampleRoomChannelAtTime(RoomId, TEXT("LLOD"), TimeSeconds, LowerOpticalDensity);
		const bool bHasUpperTemperature = SampleRoomChannelAtTime(RoomId, TEXT("ULT"), TimeSeconds, UpperTemperatureC);
		const bool bHasLowerTemperature = SampleRoomChannelAtTime(RoomId, TEXT("LLT"), TimeSeconds, LowerTemperatureC);

		if ((!bHasLayerHeight || !bHasUpperOpticalDensity || !bHasLowerOpticalDensity || !bHasUpperTemperature) && !bHasWarnedMissingSmokeSeries)
		{
			bHasWarnedMissingSmokeSeries = true;
			UE_LOG(LogBRiskDataSubsystem, Warning,
				TEXT("B-Risk smoke visualizer could not find one or more visual channels (HGT_<roomId>, ULOD_<roomId>, LLOD_<roomId>, ULT_<roomId>); missing channels use clear/ambient defaults."));
		}

		const FBRiskSmokeVisualState SmokeState = bHasLayerHeight
			? ComputeSmokeVisualState(
				LayerHeight,
				ScenarioData.Rooms[RoomIndex].Size.Z,
				ScenarioData.Rooms[RoomIndex].Origin.Z,
				RoomGeometryScale,
				bHasUpperOpticalDensity ? UpperOpticalDensity : 0.0,
				bHasLowerOpticalDensity ? LowerOpticalDensity : 0.0,
				bHasUpperTemperature ? UpperTemperatureC : 24.0,
				bHasLowerTemperature ? LowerTemperatureC : 24.0)
			: FBRiskSmokeVisualState();

		UE_LOG(LogBRiskDataSubsystem, Log,
			TEXT("B-Risk smoke sample: room=%d roomId=%d time=%g HGT=%g ULOD=%g LLOD=%g ULT=%g LLT=%g roomHeight=%g RoomSmoke=%g UpperExtinctionPerCm=%g LowerExtinctionPerCm=%g SmokeDensity=%g SmokeHeat=%g"),
			RoomIndex,
			RoomId,
			TimeSeconds,
			bHasLayerHeight ? LayerHeight : -1.0,
			bHasUpperOpticalDensity ? UpperOpticalDensity : -1.0,
			bHasLowerOpticalDensity ? LowerOpticalDensity : -1.0,
			bHasUpperTemperature ? UpperTemperatureC : -1.0,
			bHasLowerTemperature ? LowerTemperatureC : -1.0,
			ScenarioData.Rooms[RoomIndex].Size.Z,
			SmokeState.RoomSmoke,
			SmokeState.UpperExtinctionPerCm,
			SmokeState.LowerExtinctionPerCm,
			SmokeState.SmokeDensity,
			SmokeState.SmokeHeat);

		if (SmokeVisualizerActor->SetRoomSmokeState(RoomIndex, SmokeState))
		{
			bUpdatedAny = true;
		}
		else if (!bHasWarnedMissingSmokeComponent)
		{
			bHasWarnedMissingSmokeComponent = true;
			UE_LOG(LogBRiskDataSubsystem, Warning,
				TEXT("B-Risk smoke visualizer has no smoke component for one or more rooms; first missing room=%d zone=%d time=%g."),
				RoomIndex,
				RoomIndex,
				TimeSeconds);
		}
	}

	return bUpdatedAny;
}

bool UBRiskDataSubsystem::UpdateHazardVisualsAtTime(float TimeSeconds)
{
	if (!HazardVisualizerActor)
	{
		return false;
	}

	bool bUpdatedAny = false;
	for (int32 FireIndex = 0; FireIndex < ScenarioData.Fires.Num(); ++FireIndex)
	{
		double HeatReleaseRateKw = 0.0;
		double FlameHeightM = 0.0;
		double FireBaseM = 0.3;
		// B-Risk suffixes fire channels with the 1-based fire-object number (HRR_1 for
		// the first fire object), independent of which room contains the fire.
		const int32 FireObjectId = FireIndex + 1;
		const bool bHasHrr = SampleRoomChannelAtTime(FireObjectId, TEXT("HRR"), TimeSeconds, HeatReleaseRateKw);
		const bool bHasFlameHeight = SampleRoomChannelAtTime(FireObjectId, TEXT("FLHGT"), TimeSeconds, FlameHeightM);
		const bool bHasFireBase = SampleRoomChannelAtTime(FireObjectId, TEXT("FBASE"), TimeSeconds, FireBaseM);

		if ((!bHasHrr || !bHasFlameHeight) && !bHasWarnedMissingHazardSeries)
		{
			bHasWarnedMissingHazardSeries = true;
			UE_LOG(LogBRiskDataSubsystem, Warning,
				TEXT("B-Risk hazard visualizer could not find one or more fire channels (HRR_<fireId>, FLHGT_<fireId>, FBASE_<fireId>); missing channels use hidden/default fire visuals."));
		}

		FBRiskFireVisualState FireState;
		FireState.HeatReleaseRateKw = bHasHrr ? static_cast<float>(HeatReleaseRateKw) : 0.0f;
		FireState.FlameHeightM = bHasFlameHeight ? static_cast<float>(FlameHeightM) : 0.0f;
		FireState.FireBaseM = bHasFireBase ? static_cast<float>(FireBaseM) : 0.3f;

		bUpdatedAny |= HazardVisualizerActor->SetFireState(FireIndex, FireState);
	}

	// Derived Smokeview-style vent flow (B-Risk exports none): compute per vent from the two
	// rooms' sampled layer state and push to the visualizer's in/out flow indicators.
	if (ScenarioData.Vents.Num() > 0)
	{
		auto BuildVentSide = [this, TimeSeconds](int32 RoomId) -> FBRiskVentSideState
		{
			FBRiskVentSideState Side;
			const FBRiskRoomGeometry* Room = ScenarioData.Rooms.FindByPredicate(
				[RoomId](const FBRiskRoomGeometry& R) { return R.RoomId == RoomId; });
			if (!Room)
			{
				Side.bIsExterior = true;
				return Side;
			}
			Side.FloorZM = Room->Origin.Z;
			double Value = 0.0;
			if (SampleRoomChannelAtTime(RoomId, TEXT("ULT"), TimeSeconds, Value)) { Side.UpperTempC = Value; }
			if (SampleRoomChannelAtTime(RoomId, TEXT("LLT"), TimeSeconds, Value)) { Side.LowerTempC = Value; }
			if (SampleRoomChannelAtTime(RoomId, TEXT("HGT"), TimeSeconds, Value)) { Side.LayerHeightM = Value; }
			if (SampleRoomChannelAtTime(RoomId, TEXT("PRS"), TimeSeconds, Value)) { Side.PressurePa = Value; }
			return Side;
		};

		TArray<FBRiskVentFlow> VentFlows;
		VentFlows.SetNum(ScenarioData.Vents.Num());
		for (int32 VentIndex = 0; VentIndex < ScenarioData.Vents.Num(); ++VentIndex)
		{
			const FBRiskVentGeometry& Vent = ScenarioData.Vents[VentIndex];
			FBRiskVentSideState From = BuildVentSide(Vent.FromRoomId);
			FBRiskVentSideState To = BuildVentSide(Vent.ToRoomId);
			if (To.bIsExterior)
			{
				To.FloorZM = From.FloorZM; // exterior uses the From-room floor as the common datum
			}
			VentFlows[VentIndex] = ComputeWallVentFlow(From, To, Vent);
		}
		HazardVisualizerActor->SetVentFlows(VentFlows);
	}

	HazardVisualizerActor->SetSimulationTime(TimeSeconds);
	return bUpdatedAny || ScenarioData.Sprinklers.Num() > 0 || ScenarioData.Vents.Num() > 0;
}

void UBRiskDataSubsystem::ClearSmokeVolumes()
{
	UnbindSmokeTimeDelegate();

	if (SmokeVisualizerActor && !SmokeVisualizerActor->IsActorBeingDestroyed())
	{
		SmokeVisualizerActor->Destroy();
	}
	SmokeVisualizerActor = nullptr;
	bHasWarnedMissingSmokeSeries = false;
	bHasWarnedMissingSmokeComponent = false;
}

void UBRiskDataSubsystem::ClearHazardVisuals()
{
	if (HazardVisualizerActor && !HazardVisualizerActor->IsActorBeingDestroyed())
	{
		HazardVisualizerActor->Destroy();
	}
	HazardVisualizerActor = nullptr;
	bHasWarnedMissingHazardSeries = false;
}

void UBRiskDataSubsystem::LogScenarioSummary(
	bool bLogReferencedFiles,
	bool bLogRooms,
	bool bLogFires,
	bool bLogVents,
	bool bLogZoneTables,
	bool bLogSeries,
	bool bLogSamples) const
{
	UE_LOG(LogBRiskDataSubsystem, Log, TEXT("B-Risk active SMV: %s"), *ActiveSmvPath);
	UE_LOG(LogBRiskDataSubsystem, Log, TEXT("B-Risk loaded=%s loading=%s rooms=%d fires=%d vents=%d zoneTables=%d lastError=%s"),
		HasScenarioData() ? TEXT("true") : TEXT("false"),
		bIsLoading ? TEXT("true") : TEXT("false"),
		ScenarioData.Rooms.Num(),
		ScenarioData.Fires.Num(),
		ScenarioData.Vents.Num(),
		ScenarioData.ZoneTables.Num(),
		*LastError);

	LogBRiskScenarioData(
		TEXT("B-Risk"),
		ScenarioData,
		bLogReferencedFiles,
		bLogRooms,
		bLogFires,
		bLogVents,
		bLogZoneTables,
		bLogSeries,
		bLogSamples);
}

bool UBRiskDataSubsystem::DebugImportScenarioFromSmv(
	const FString& SmvFilePath,
	bool bLogReferencedFiles,
	bool bLogRooms,
	bool bLogFires,
	bool bLogVents,
	bool bLogZoneTables,
	bool bLogSeries,
	bool bLogSamples)
{
	FBRiskScenarioData ParsedData;
	FString ErrorMessage;
	const bool bSuccess = FBRiskDataImporter::ImportScenarioFromSmv(SmvFilePath, ParsedData, &ErrorMessage);

	if (!bSuccess)
	{
		UE_LOG(LogBRiskDataSubsystem, Error, TEXT("B-Risk debug import failed: %s"), *ErrorMessage);
		return false;
	}

	UE_LOG(LogBRiskDataSubsystem, Log, TEXT("B-Risk debug import SMV: %s"), *ParsedData.SourceSmvPath);
	UE_LOG(LogBRiskDataSubsystem, Log, TEXT("B-Risk debug import rooms=%d fires=%d vents=%d zoneTables=%d"),
		ParsedData.Rooms.Num(),
		ParsedData.Fires.Num(),
		ParsedData.Vents.Num(),
		ParsedData.ZoneTables.Num());

	LogBRiskScenarioData(
		TEXT("B-Risk debug"),
		ParsedData,
		bLogReferencedFiles,
		bLogRooms,
		bLogFires,
		bLogVents,
		bLogZoneTables,
		bLogSeries,
		bLogSamples);

	return true;
}

bool UBRiskDataSubsystem::GetSeriesByName(int32 ZoneTableIndex, const FString& SeriesName,
                                           const FBRiskSeries*& OutSeries) const
{
	OutSeries = nullptr;
	if (!ScenarioData.ZoneTables.IsValidIndex(ZoneTableIndex))
	{
		return false;
	}

	const FBRiskZoneTable& Table = ScenarioData.ZoneTables[ZoneTableIndex];
	for (const FBRiskSeries& Series : Table.Series)
	{
		if (Series.Name.Equals(SeriesName, ESearchCase::IgnoreCase))
		{
			OutSeries = &Series;
			return true;
		}
	}
	return false;
}

bool UBRiskDataSubsystem::SampleSeriesAtTime(int32 ZoneTableIndex, const FString& SeriesName,
                                              double TimeSeconds, double& OutValue) const
{
	const FBRiskSeries* FoundSeries = nullptr;
	if (!GetSeriesByName(ZoneTableIndex, SeriesName, FoundSeries) || !FoundSeries)
	{
		return false;
	}

	const TArray<double>& Times = ScenarioData.ZoneTables[ZoneTableIndex].TimeSeconds;
	const TArray<double>& Values = FoundSeries->Values;

	if (Times.Num() == 0 || Values.Num() != Times.Num())
	{
		return false;
	}

	if (TimeSeconds <= Times[0])
	{
		OutValue = Values[0];
		return true;
	}
	if (TimeSeconds >= Times.Last())
	{
		OutValue = Values.Last();
		return true;
	}

	int32 Lo = 0;
	int32 Hi = Times.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Times[Mid] <= TimeSeconds) { Lo = Mid; }
		else                           { Hi = Mid; }
	}

	const double Alpha = (TimeSeconds - Times[Lo]) / (Times[Hi] - Times[Lo]);
	OutValue = FMath::Lerp(Values[Lo], Values[Hi], Alpha);
	return true;
}

bool UBRiskDataSubsystem::SampleRoomChannelAtTime(int32 OneBasedIndex, const TCHAR* BaseName,
                                                  double TimeSeconds, double& OutValue) const
{
	const FString FullName = FString::Printf(TEXT("%s_%d"), BaseName, OneBasedIndex);
	for (int32 TableIndex = 0; TableIndex < ScenarioData.ZoneTables.Num(); ++TableIndex)
	{
		if (SampleSeriesAtTime(TableIndex, FullName, TimeSeconds, OutValue))
		{
			return true;
		}
	}
	return false;
}

namespace
{
	/** Tolerance, in metres, for cross-checking Zones-data.json elevations against the .smv. */
	constexpr double FootprintElevationToleranceM = 0.01;
}

bool UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
	const TArray<FBRiskRoomGeometry>& Rooms,
	const TArray<FBRiskVentGeometry>& Vents,
	float Scale,
	BRiskCoord::ERoomFrame Frame,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	FString* OutError)
{
	// Generates solid, single-sided, outward-facing room shells in Unreal space. Currently
	// DORMANT (bAutoGenerateRoomGeometryOnLoad defaults false) — intended for a future "load
	// extra geometry" toggle.
	//
	// Two paths, chosen per room and never mixed within a room:
	//
	//  * Room HAS a Zones-data.json footprint -> extruded polygon prism, converted with
	//    BRiskCoord::FootprintToUnreal (Y negated). This is the real floor plan. Door/window
	//    openings are NOT cut — see the comment on the polygon path below.
	//  * Room has NO footprint -> the legacy equivalent-rectangle box, converted with
	//    BRiskCoord::ToUnreal (X<->Y swap), with one door opening per wall.
	//
	// The two conversions differ by a 90 degree rotation about the world origin, so a scenario
	// containing both kinds of room produces geometry in two frames. That is a data-authoring
	// fault, not a supported mode, and is warned about below.
	OutVertices.Reset();
	OutTriangles.Reset();
	OutNormals.Reset();

	if (Rooms.Num() == 0)
	{
		if (OutError)
		{
			*OutError = TEXT("No ROOM entries were parsed from the B-Risk scenario.");
		}
		return false;
	}

	if (Scale <= 0.0f)
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Invalid B-Risk room geometry scale: %g"), Scale);
		}
		return false;
	}

	OutVertices.Reserve(Rooms.Num() * 24 + Vents.Num() * 12);
	OutNormals.Reserve(Rooms.Num() * 24 + Vents.Num() * 12);
	OutTriangles.Reserve(Rooms.Num() * 36 + Vents.Num() * 18);

	const auto AddQuad = [&OutVertices, &OutTriangles, &OutNormals](
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D)
	{
		if (A.Equals(B) || B.Equals(C) || C.Equals(D) || D.Equals(A))
		{
			return;
		}

		const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			return;
		}

		const int32 FrontBase = OutVertices.Num();
		OutVertices.Append({ A, B, C, D });
		OutNormals.Append({ Normal, Normal, Normal, Normal });
		OutTriangles.Append({
			FrontBase, FrontBase + 1, FrontBase + 2,
			FrontBase, FrontBase + 2, FrontBase + 3
		});
	};

	// Emits one triangle wound so that it faces DesiredNormal. Deriving the winding from the
	// requested facing rather than from the input ordering keeps the result independent of the
	// triangulator's own winding convention.
	const auto AddTriangleFacing = [&OutVertices, &OutTriangles, &OutNormals](
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& DesiredNormal)
	{
		FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			return;
		}

		const bool bFlipWinding = FVector::DotProduct(Normal, DesiredNormal) < 0.0;
		if (bFlipWinding)
		{
			Normal = -Normal;
		}

		const int32 Base = OutVertices.Num();
		OutVertices.Append({ A, bFlipWinding ? C : B, bFlipWinding ? B : C });
		OutNormals.Append({ Normal, Normal, Normal });
		OutTriangles.Append({ Base, Base + 1, Base + 2 });
	};

	// Builds the true floor plan for one room: triangulated floor and ceiling caps plus one
	// extruded quad per polygon edge.
	const auto AppendFootprintRoom = [&AddTriangleFacing, Scale, Frame](
		const FBRiskRoomGeometry& Room,
		FString& OutRoomError) -> bool
	{
		// Conversion, vertex welding and winding normalisation all live in MakeRoomFootprint so
		// that the mesh, the smoke volume and the egress bounds cannot drift apart.
		const BRiskCoord::FRoomFootprintCm Footprint = BRiskCoord::MakeRoomFootprint(Room, Scale, Frame);
		if (!Footprint.bFromPolygon)
		{
			OutRoomError = FString::Printf(
				TEXT("Room %d ('%s') has a degenerate Zones-data.json footprint (%d source vertices)."),
				Room.RoomId, *Room.Label, Room.FootprintPolygon.Num());
			return false;
		}

		const TArray<FVector2D>& Ring = Footprint.Polygon;

		if (Room.Size.Z <= 0.0)
		{
			OutRoomError = FString::Printf(
				TEXT("Room %d ('%s') has a footprint but an invalid height %g m."),
				Room.RoomId, *Room.Label, Room.Size.Z);
			return false;
		}

		// Z comes from the .smv, which is also what the smoke and hazard visualizers read;
		// taking it from the JSON instead would create a second source of truth. The JSON pair
		// is only cross-checked, because §10.3 of the findings leaves that mapping unverified
		// for anything other than the flat, elevation-zero datasets shipped so far.
		if (Room.bHasFootprintExtents
			&& (!FMath::IsNearlyEqual(Room.FootprintFloorElevationM, Room.Origin.Z, FootprintElevationToleranceM)
				|| !FMath::IsNearlyEqual(Room.FootprintHeightM, Room.Size.Z, FootprintElevationToleranceM)))
		{
			UE_LOG(LogBRiskDataSubsystem, Warning,
				TEXT("Room %d ('%s') Zones-data.json elevation/height (%g / %g m) disagrees with the .smv ")
				TEXT("(%g / %g m); the .smv values are used so the mesh matches the smoke volume."),
				Room.RoomId, *Room.Label,
				Room.FootprintFloorElevationM, Room.FootprintHeightM,
				Room.Origin.Z, Room.Size.Z);
		}

		const double FloorZ = Room.Origin.Z * Scale;
		const double CeilingZ = (Room.Origin.Z + Room.Size.Z) * Scale;

		std::vector<std::vector<std::array<double, 2>>> EarcutRings(1);
		EarcutRings[0].reserve(static_cast<size_t>(Ring.Num()));
		for (const FVector2D& Point : Ring)
		{
			EarcutRings[0].push_back({ Point.X, Point.Y });
		}

		const std::vector<size_t> CapIndices = mapbox::earcut<size_t>(EarcutRings);
		if (CapIndices.size() < 3 || CapIndices.size() % 3 != 0)
		{
			OutRoomError = FString::Printf(
				TEXT("Room %d ('%s') footprint could not be triangulated (%d vertices produced %d indices)."),
				Room.RoomId, *Room.Label, Ring.Num(), static_cast<int32>(CapIndices.size()));
			return false;
		}

		for (size_t Index = 0; Index + 2 < CapIndices.size(); Index += 3)
		{
			const FVector2D& P0 = Ring[static_cast<int32>(CapIndices[Index])];
			const FVector2D& P1 = Ring[static_cast<int32>(CapIndices[Index + 1])];
			const FVector2D& P2 = Ring[static_cast<int32>(CapIndices[Index + 2])];

			// Outward-facing shell, matching the box path: floor looks down, ceiling looks up.
			AddTriangleFacing(
				FVector(P0.X, P0.Y, FloorZ),
				FVector(P1.X, P1.Y, FloorZ),
				FVector(P2.X, P2.Y, FloorZ),
				FVector::DownVector);
			AddTriangleFacing(
				FVector(P0.X, P0.Y, CeilingZ),
				FVector(P1.X, P1.Y, CeilingZ),
				FVector(P2.X, P2.Y, CeilingZ),
				FVector::UpVector);
		}

		// Walls are solid: no door or window openings are cut on the polygon path. A B-Risk vent
		// is located by (face, offset) along the perimeter of the EQUIVALENT RECTANGLE, which has
		// neither the edge count nor the perimeter length of the real footprint, so an offset
		// cannot be mapped onto a polygon edge. Punching a hole in a guessed wall is worse than
		// leaving the shell closed. Vent XY in Zones-data.json would fix this upstream.
		for (int32 Index = 0; Index < Ring.Num(); ++Index)
		{
			const FVector2D& Start = Ring[Index];
			const FVector2D& End = Ring[(Index + 1) % Ring.Num()];

			// Ring is counter-clockwise, so (dy, -dx) points out of the room.
			const FVector2D Edge = End - Start;
			const FVector Outward = FVector(Edge.Y, -Edge.X, 0.0).GetSafeNormal();
			if (Outward.IsNearlyZero())
			{
				continue;
			}

			const FVector BottomStart(Start.X, Start.Y, FloorZ);
			const FVector BottomEnd(End.X, End.Y, FloorZ);
			const FVector TopEnd(End.X, End.Y, CeilingZ);
			const FVector TopStart(Start.X, Start.Y, CeilingZ);

			AddTriangleFacing(BottomStart, BottomEnd, TopEnd, Outward);
			AddTriangleFacing(BottomStart, TopEnd, TopStart, Outward);
		}

		return true;
	};

	// A scenario that mixes the two conversions places some rooms 90 degrees away from the
	// others, which looks plausible in isolation. The importer already warns per unmatched room,
	// but this is a geometry-coherence failure rather than an import gap, so say it here too.
	int32 FootprintRoomCount = 0;
	for (const FBRiskRoomGeometry& Room : Rooms)
	{
		FootprintRoomCount += (Room.FootprintPolygon.Num() >= 3) ? 1 : 0;
	}

	if (FootprintRoomCount > 0 && FootprintRoomCount < Rooms.Num())
	{
		TArray<FString> RectangleRoomIds;
		for (const FBRiskRoomGeometry& Room : Rooms)
		{
			if (Room.FootprintPolygon.Num() < 3)
			{
				RectangleRoomIds.Add(FString::FromInt(Room.RoomId));
			}
		}

		UE_LOG(LogBRiskDataSubsystem, Warning,
			TEXT("B-Risk room geometry mixes coordinate frames: %d of %d rooms have a Zones-data.json ")
			TEXT("footprint. Room(s) %s fall back to the equivalent rectangle, which uses the legacy ")
			TEXT("X<->Y swap instead of the Revit frame — those rooms will be rotated 90 degrees about ")
			TEXT("the world origin relative to the rest."),
			FootprintRoomCount,
			Rooms.Num(),
			*FString::Join(RectangleRoomIds, TEXT(", ")));
	}

	for (const FBRiskRoomGeometry& Room : Rooms)
	{
		if (Room.FootprintPolygon.Num() >= 3)
		{
			FString RoomError;
			if (!AppendFootprintRoom(Room, RoomError))
			{
				if (OutError)
				{
					*OutError = MoveTemp(RoomError);
				}
				OutVertices.Reset();
				OutTriangles.Reset();
				OutNormals.Reset();
				return false;
			}
			continue;
		}

		if (Room.Size.X <= 0.0 || Room.Size.Y <= 0.0 || Room.Size.Z <= 0.0)
		{
			if (OutError)
			{
				*OutError = FString::Printf(
					TEXT("Room %d has invalid size %s."),
					Room.RoomId,
					*Room.Size.ToString());
			}
			OutVertices.Reset();
			OutTriangles.Reset();
			OutNormals.Reset();
			return false;
		}

		// B-Risk metres -> Unreal cm with the X<->Y swap (see BRiskCoord), matching the
		// smoke/hazard visualizers and egress bounds so generated walls aren't mirrored.
		const FBox RoomBoxCm = BRiskCoord::ToUnrealBox(Room.Origin, Room.Size, Scale);
		const FVector Min = RoomBoxCm.Min;
		const FVector Max = RoomBoxCm.Max;

		const auto AddWallWithOptionalOpening = [&](
			int32 WallBRiskFace,
			const FVector& A,
			const FVector& B,
			const FVector& C,
			const FVector& D,
			bool bAxisIsX,
			bool bReverseOpeningWinding,
			double WallStart,
			double WallEnd)
		{
			// Cut the opening for the vent on THIS wall: the first of this room's vents whose
			// B-Risk face id matches. (One opening per wall; multiple vents sharing a wall is
			// not yet supported.) The caller passes the B-Risk face for this UE wall under the
			// X<->Y swap.
			const FBRiskVentGeometry* WallVent = nullptr;
			for (const FBRiskVentGeometry& Vent : Vents)
			{
				if (Vent.FromRoomId == Room.RoomId
					&& Vent.Face == WallBRiskFace
					&& Vent.Width > 0.0
					&& Vent.Height > 0.0)
				{
					WallVent = &Vent;
					break;
				}
			}

			if (!WallVent)
			{
				AddQuad(A, B, C, D);
				return;
			}

			// Offset is room-local along the wall, so measure it from the wall start (fixes
			// offset rooms where WallStart != 0).
			const double OpeningStart = FMath::Clamp(WallStart + WallVent->Offset * Scale, WallStart, WallEnd);
			const double OpeningEnd = FMath::Clamp(WallStart + (WallVent->Offset + WallVent->Width) * Scale, WallStart, WallEnd);
			const double Sill = FMath::Clamp((Room.Origin.Z + WallVent->SillHeight) * Scale, static_cast<double>(Min.Z), static_cast<double>(Max.Z));
			const double Head = FMath::Clamp((Room.Origin.Z + WallVent->SillHeight + WallVent->Height) * Scale, static_cast<double>(Min.Z), static_cast<double>(Max.Z));

			if (OpeningEnd <= OpeningStart || Head <= Sill)
			{
				AddQuad(A, B, C, D);
				return;
			}

			const double FixedX = A.X;
			const double FixedY = A.Y;
			const auto AddOpeningQuad = [&](const FVector& Q0, const FVector& Q1, const FVector& Q2, const FVector& Q3)
			{
				if (bReverseOpeningWinding)
				{
					AddQuad(Q3, Q2, Q1, Q0);
				}
				else
				{
					AddQuad(Q0, Q1, Q2, Q3);
				}
			};

			if (bAxisIsX)
			{
				AddOpeningQuad(
					FVector(WallStart, FixedY, Min.Z),
					FVector(OpeningStart, FixedY, Min.Z),
					FVector(OpeningStart, FixedY, Max.Z),
					FVector(WallStart, FixedY, Max.Z));
				AddOpeningQuad(
					FVector(OpeningEnd, FixedY, Min.Z),
					FVector(WallEnd, FixedY, Min.Z),
					FVector(WallEnd, FixedY, Max.Z),
					FVector(OpeningEnd, FixedY, Max.Z));
				AddOpeningQuad(
					FVector(OpeningStart, FixedY, Min.Z),
					FVector(OpeningEnd, FixedY, Min.Z),
					FVector(OpeningEnd, FixedY, Sill),
					FVector(OpeningStart, FixedY, Sill));
				AddOpeningQuad(
					FVector(OpeningStart, FixedY, Head),
					FVector(OpeningEnd, FixedY, Head),
					FVector(OpeningEnd, FixedY, Max.Z),
					FVector(OpeningStart, FixedY, Max.Z));
				return;
			}

			AddOpeningQuad(
				FVector(FixedX, WallStart, Min.Z),
				FVector(FixedX, OpeningStart, Min.Z),
				FVector(FixedX, OpeningStart, Max.Z),
				FVector(FixedX, WallStart, Max.Z));
			AddOpeningQuad(
				FVector(FixedX, OpeningEnd, Min.Z),
				FVector(FixedX, WallEnd, Min.Z),
				FVector(FixedX, WallEnd, Max.Z),
				FVector(FixedX, OpeningEnd, Max.Z));
			AddOpeningQuad(
				FVector(FixedX, OpeningStart, Min.Z),
				FVector(FixedX, OpeningEnd, Min.Z),
				FVector(FixedX, OpeningEnd, Sill),
				FVector(FixedX, OpeningStart, Sill));
			AddOpeningQuad(
				FVector(FixedX, OpeningStart, Head),
				FVector(FixedX, OpeningEnd, Head),
				FVector(FixedX, OpeningEnd, Max.Z),
				FVector(FixedX, OpeningStart, Max.Z));
		};

		AddQuad(FVector(Min.X, Min.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z));
		AddQuad(FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z));

		// Each UE wall maps to a B-Risk vent face under the BRiskCoord X<->Y swap:
		// UE -Y wall = B-Risk face 4 (-X), UE +Y = face 2 (+X), UE -X = face 1 (-Y), UE +X = face 3 (+Y).
		AddWallWithOptionalOpening(4, FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z), true, false, Min.X, Max.X);
		AddWallWithOptionalOpening(2, FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), true, true, Min.X, Max.X);
		AddWallWithOptionalOpening(1, FVector(Min.X, Max.Y, Min.Z), FVector(Min.X, Min.Y, Min.Z), FVector(Min.X, Min.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z), false, true, Min.Y, Max.Y);
		AddWallWithOptionalOpening(3, FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z), false, false, Min.Y, Max.Y);
	}

	if (FootprintRoomCount > 0)
	{
		int32 UncutVents = 0;
		for (const FBRiskVentGeometry& Vent : Vents)
		{
			const FBRiskRoomGeometry* Owner = Rooms.FindByPredicate(
				[&Vent](const FBRiskRoomGeometry& Candidate) { return Candidate.RoomId == Vent.FromRoomId; });
			UncutVents += (Owner && Owner->FootprintPolygon.Num() >= 3) ? 1 : 0;
		}

		UE_LOG(LogBRiskDataSubsystem, Log,
			TEXT("B-Risk room geometry: %d of %d rooms built from Zones-data.json footprints; ")
			TEXT("%d vent opening(s) left uncut because B-Risk locates vents on the equivalent ")
			TEXT("rectangle, not on the real polygon."),
			FootprintRoomCount,
			Rooms.Num(),
			UncutVents);
	}

	return true;
}

float UBRiskDataSubsystem::ComputeRoomSmokeScalar(double LayerHeight, double RoomHeight)
{
	if (RoomHeight <= 0.0)
	{
		return 1.0f;
	}

	return FMath::Clamp(static_cast<float>(LayerHeight / RoomHeight), 0.0f, 1.0f);
}

FBRiskSmokeVisualState UBRiskDataSubsystem::ComputeSmokeVisualState(
	double LayerHeight,
	double RoomHeight,
	double RoomOriginZMeters,
	float GeometryScaleCmPerMeter,
	double UpperOpticalDensity,
	double LowerOpticalDensity,
	double UpperTemperatureC,
	double LowerTemperatureC)
{
	FBRiskSmokeVisualState SmokeState;
	const double ClampedUpperOpticalDensity = FMath::Max(UpperOpticalDensity, 0.0);
	const double ClampedLowerOpticalDensity = FMath::Max(LowerOpticalDensity, 0.0);

	SmokeState.RoomSmoke = ComputeRoomSmokeScalar(LayerHeight, RoomHeight);
	SmokeState.UpperOpticalDensity = static_cast<float>(ClampedUpperOpticalDensity);
	SmokeState.LowerOpticalDensity = static_cast<float>(ClampedLowerOpticalDensity);
	SmokeState.UpperExtinctionPerCm = static_cast<float>(ClampedUpperOpticalDensity * BRiskOpticalDensityToExtinctionPerCm);
	SmokeState.LowerExtinctionPerCm = static_cast<float>(ClampedLowerOpticalDensity * BRiskOpticalDensityToExtinctionPerCm);
	// SmokeDensity is a 0-1 UI/activation proxy (used for Niagara enable threshold + fallback cube),
	// not physical extinction. The material should use UpperExtinctionPerCm/LowerExtinctionPerCm.
	SmokeState.SmokeDensity = FMath::Clamp(
		static_cast<float>(1.0 - FMath::Exp(-0.35 * ClampedUpperOpticalDensity)),
		0.0f,
		1.0f);
	SmokeState.UpperTemperatureC = static_cast<float>(UpperTemperatureC);
	SmokeState.LowerTemperatureC = static_cast<float>(LowerTemperatureC);
	SmokeState.SmokeHeat = FMath::Clamp(
		static_cast<float>((UpperTemperatureC - 24.0) / 200.0),
		0.0f,
		1.0f);
	SmokeState.LayerHeightWorldCm = static_cast<float>((RoomOriginZMeters + LayerHeight) * static_cast<double>(GeometryScaleCmPerMeter));
	SmokeState.LayerSoftnessCm = 5.0f;
	return SmokeState;
}

FBRiskVentFlow UBRiskDataSubsystem::ComputeWallVentFlow(
	const FBRiskVentSideState& From,
	const FBRiskVentSideState& To,
	const FBRiskVentGeometry& Vent)
{
	// Cross-vent hydrostatic flow per CCFM.VENTS/CFAST (SR282 §7.11.1): build each side's
	// two-layer pressure profile over the opening, integrate the Bernoulli slab flux, and
	// split by sign about the neutral plane into bidirectional out/in streams. Qualitative
	// fallback only (B-Risk exports no per-vent flow; Smokeview computes it the same way).
	constexpr double Cd = 0.68;             // discharge coefficient (SR282 §7.6 / vents.xml)
	constexpr double DensityConstant = 353.0; // rho = 353 / T[K] zone-model approximation
	constexpr double Gravity = 9.81;
	constexpr double AmbientTempC = 20.0;
	constexpr int32 NumSlabs = 24;
	constexpr double FlowEpsilonKgs = 1.0e-6;

	FBRiskVentFlow Out;
	if (Vent.Width <= 0.0 || Vent.Height <= 0.0)
	{
		return Out; // closed / degenerate opening
	}

	auto DensityAtTempC = [](double TempC) -> double
	{
		return DensityConstant / (TempC + 273.15); // channels are Celsius (the +273.15 is mandatory)
	};
	const double RhoAmb = DensityAtTempC(AmbientTempC);

	const double ZBottom = From.FloorZM + Vent.SillHeight;
	const double ZTop = ZBottom + Vent.Height;

	auto SideIfaceZ = [](const FBRiskVentSideState& S) -> double
	{
		return S.bIsExterior ? TNumericLimits<double>::Lowest() : (S.FloorZM + S.LayerHeightM);
	};
	auto SideRhoLower = [&](const FBRiskVentSideState& S) { return S.bIsExterior ? RhoAmb : DensityAtTempC(S.LowerTempC); };
	auto SideRhoUpper = [&](const FBRiskVentSideState& S) { return S.bIsExterior ? RhoAmb : DensityAtTempC(S.UpperTempC); };
	auto SideRhoAtZ = [&](const FBRiskVentSideState& S, double z) { return z >= SideIfaceZ(S) ? SideRhoUpper(S) : SideRhoLower(S); };
	auto SideTempC = [&](const FBRiskVentSideState& S, double z) -> double
	{
		return S.bIsExterior ? AmbientTempC : (z >= SideIfaceZ(S) ? S.UpperTempC : S.LowerTempC);
	};

	// Gauge pressure on one side at height z, referenced to the From-room floor (common datum).
	// p(z) = PRS - g * integral_(floor..z)(rho - rhoAmb) dz'. The (rho - rhoAmb) form keeps
	// magnitudes ~O(Pa) and makes an exterior side collapse to ~0 automatically.
	const double DatumZ = From.FloorZM;
	auto SidePressureAtZ = [&](const FBRiskVentSideState& S, double z) -> double
	{
		const double Iface = SideIfaceZ(S);
		const double RhoLo = SideRhoLower(S);
		const double RhoUp = SideRhoUpper(S);
		double Integral = 0.0;
		double zCursor = DatumZ;
		const double LowerTop = FMath::Min(z, Iface);
		if (LowerTop > zCursor) { Integral += (RhoLo - RhoAmb) * (LowerTop - zCursor); zCursor = LowerTop; }
		if (z > zCursor) { Integral += (RhoUp - RhoAmb) * (z - zCursor); }
		return S.PressurePa - Gravity * Integral;
	};

	const double dz = (ZTop - ZBottom) / static_cast<double>(NumSlabs);
	double SumTempOut = 0.0;
	double SumTempIn = 0.0;
	double PrevDeltaP = 0.0;
	bool bHavePrev = false;

	for (int32 SlabIndex = 0; SlabIndex < NumSlabs; ++SlabIndex)
	{
		const double z = ZBottom + (static_cast<double>(SlabIndex) + 0.5) * dz; // slab midpoint
		const double DeltaP = SidePressureAtZ(From, z) - SidePressureAtZ(To, z);

		// Neutral plane = first sign change of DeltaP across slabs (flux -> 0 there).
		if (bHavePrev && Out.NeutralPlaneHeightM < 0.0
			&& (PrevDeltaP == 0.0 || (PrevDeltaP < 0.0) != (DeltaP < 0.0)))
		{
			Out.NeutralPlaneHeightM = (z - 0.5 * dz) - From.FloorZM;
		}
		PrevDeltaP = DeltaP;
		bHavePrev = true;

		if (FMath::Abs(DeltaP) < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// Upwind side donates density + temperature for this slab.
		const bool bFromIsUpwind = DeltaP > 0.0;
		const FBRiskVentSideState& Donor = bFromIsUpwind ? From : To;
		const double RhoDonor = SideRhoAtZ(Donor, z);
		const double TempDonor = SideTempC(Donor, z);
		const double SlabMass = Cd * Vent.Width * FMath::Sqrt(2.0 * RhoDonor * FMath::Abs(DeltaP)) * dz;

		if (bFromIsUpwind)
		{
			Out.MassFlowOutKgs += SlabMass;
			SumTempOut += SlabMass * TempDonor;
		}
		else
		{
			Out.MassFlowInKgs += SlabMass;
			SumTempIn += SlabMass * TempDonor;
		}
	}

	Out.OutTemperatureC = (Out.MassFlowOutKgs > FlowEpsilonKgs) ? (SumTempOut / Out.MassFlowOutKgs) : From.UpperTempC;
	Out.InTemperatureC = (Out.MassFlowInKgs > FlowEpsilonKgs) ? (SumTempIn / Out.MassFlowInKgs) : (To.bIsExterior ? AmbientTempC : To.LowerTempC);
	Out.bHasFlow = (Out.MassFlowOutKgs + Out.MassFlowInKgs) > FlowEpsilonKgs;
	return Out;
}

void UBRiskDataSubsystem::HandleNewSimulationTime(float NewCurrentTime)
{
	UpdateSmokeAtTime(NewCurrentTime);
	UpdateHazardVisualsAtTime(NewCurrentTime);
}

void UBRiskDataSubsystem::HandleSimulationPauseChanged(bool bPaused)
{
	if (SmokeVisualizerActor)
	{
		SmokeVisualizerActor->SetSmokeSimulationPaused(bPaused);
	}
}

void UBRiskDataSubsystem::ConfigurePlaybackFromScenario()
{
	// Called on a successful .smv load (only when B-Risk timing is enabled). Restart the shared
	// timeline at t=0, paused, using whichever source currently owns the clock.
	ApplyActiveTimeline(/*bResetToStart=*/true);
}

void UBRiskDataSubsystem::ApplyActiveTimeline(bool bResetToStart)
{
	UTimeDilationSubSystem* Clock = GetTimeDilationSubsystem();
	if (!Clock)
	{
		UE_LOG(LogBRiskDataSubsystem, Warning,
			TEXT("ApplyActiveTimeline: TimeDilationSubSystem unavailable; clock not configured."));
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// --- Determine source availability -------------------------------------------------------
	// B-Risk: has parseable zone-time data (HasScenarioData guards ZoneTables[0].TimeSeconds).
	const bool bBRiskLoaded = HasScenarioData();

	// Agent: the spawn subsystem caches the agent trajectory's duration + sample interval at build
	// time. These persist for the life of the loaded file, unlike UAgentDataSubsystem::bIsDataLoaded
	// (a transient one-shot edge reset right after the load-complete broadcast).
	float AgentTotal = 0.0f;
	float AgentInterval = 0.0f;
	if (const UMassEntitySpawnSubsystem* SpawnSubsystem = World->GetSubsystem<UMassEntitySpawnSubsystem>())
	{
		AgentTotal = SpawnSubsystem->GetAgentTotalTime();
		AgentInterval = SpawnSubsystem->GetAgentTimeBetweenSteps();
	}
	const bool bAgentLoaded = (AgentTotal > 0.0f && AgentInterval > UE_KINDA_SMALL_NUMBER);

	// --- Choose the clock source -------------------------------------------------------------
	// B-Risk owns the clock only when its timing is enabled AND it has data; otherwise the agent
	// trajectory owns it; with neither available there is nothing to drive the clock, so leave it
	// untouched (safety gate). Agent data is still parsed/loaded on its own grid either way.
	enum class ETimelineSource : uint8 { None, BRisk, Agent };
	ETimelineSource Source = ETimelineSource::None;
	if (bConfigureSharedPlaybackOnLoad && bBRiskLoaded)
	{
		Source = ETimelineSource::BRisk;
	}
	else if (bAgentLoaded)
	{
		Source = ETimelineSource::Agent;
	}

	if (Source == ETimelineSource::None)
	{
		UE_LOG(LogBRiskDataSubsystem, Verbose,
			TEXT("ApplyActiveTimeline: no active timeline source (bRiskTiming=%d bRiskLoaded=%d agentLoaded=%d); clock left as-is."),
			bConfigureSharedPlaybackOnLoad ? 1 : 0, bBRiskLoaded ? 1 : 0, bAgentLoaded ? 1 : 0);
		return;
	}

	// --- Resolve the new (total, interval) for the chosen source -----------------------------
	float NewTotal = 0.0f;
	float NewInterval = 0.0f;
	if (Source == ETimelineSource::BRisk)
	{
		const TArray<double>& Times = ScenarioData.ZoneTables[0].TimeSeconds;
		NewTotal = static_cast<float>(Times.Last());
		NewInterval = Times.Num() > 1
			? static_cast<float>(Times[1] - Times[0])
			: Clock->TimeBetweenSteps;
	}
	else // Agent
	{
		NewTotal = AgentTotal;
		NewInterval = AgentInterval;
	}

	// Safety gate: never push a degenerate clock configuration.
	if (NewTotal <= 0.0f || NewInterval <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	// --- Apply, preserving the live position + play/pause state ------------------------------
	const bool bWasPaused = Clock->bIsPaused;
	const float PrevSec = Clock->GetCurrentSimTime();

	Clock->UpdateTimeBetweenData(NewInterval);
	Clock->UpdateTotalTime(NewTotal);

	const float NewSec = bResetToStart ? 0.0f : FMath::Clamp(PrevSec, 0.0f, NewTotal);

	// A fresh load always restarts paused; a live toggle preserves the prior play/pause state.
	// OverrideCurrentTime force-pauses, then resumes iff PreviouslyPaused == 0, so passing the
	// desired paused-ness as that argument sets the final state exactly.
	const bool bTargetPaused = bResetToStart ? true : bWasPaused;
	Clock->OverrideCurrentTime(NewSec, bTargetPaused ? 1 : 0);

	UE_LOG(LogBRiskDataSubsystem, Log,
		TEXT("ApplyActiveTimeline: source=%s reset=%d TotalTime=%g TimeBetweenSteps=%g CurrentTime=%g paused=%d"),
		Source == ETimelineSource::BRisk ? TEXT("b-risk") : TEXT("agent"),
		bResetToStart ? 1 : 0,
		NewTotal,
		NewInterval,
		NewSec,
		bTargetPaused ? 1 : 0);
}

UTimeDilationSubSystem* UBRiskDataSubsystem::GetTimeDilationSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UTimeDilationSubSystem>() : nullptr;
}

void UBRiskDataSubsystem::UnbindSmokeTimeDelegate()
{
	if (UTimeDilationSubSystem* TimeSubsystem = GetTimeDilationSubsystem())
	{
		TimeSubsystem->OnNewCurrentTime.RemoveDynamic(this, &UBRiskDataSubsystem::HandleNewSimulationTime);
		TimeSubsystem->OnSimulationPauseChanged.RemoveDynamic(this, &UBRiskDataSubsystem::HandleSimulationPauseChanged);
	}
}

void UBRiskDataSubsystem::OnSmvFileChanged()
{
	if (const UWorld* World = GetWorld())
	{
		if (const UProjectMobiusGameInstance* GI =
			Cast<UProjectMobiusGameInstance>(World->GetGameInstance()))
		{
			const FString& NewPath = GI->GetBRiskSmvFilePath();

			static const FString Placeholder(TEXT("Click Browse to choose file"));
			if (!NewPath.IsEmpty() && NewPath != Placeholder)
			{
				LoadScenarioFromSmv(NewPath);
			}
		}
	}
}
