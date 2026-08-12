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

				// Geometry-only load. The building is on screen and the generators above all ran -
				// they gate on rooms/fires/vents, none of which need a time series - but every
				// results-driven feature (smoke density, tenability bars, agent dose) stays blank,
				// and nothing else on screen says why. Name the files: the fix is to run B-Risk.
				//
				// Warning, not Error: a model imported before it has been simulated is a normal
				// step in the authoring loop, not a fault. The import genuinely succeeded.
				if (!Self->ScenarioData.bHasResultsData)
				{
					UE_LOG(LogBRiskDataSubsystem, Warning,
						TEXT("B-Risk scenario loaded WITHOUT results: %d file(s) missing."),
						Self->ScenarioData.MissingResultFiles.Num());

					if (IMobiusErrorReporter* Reporter = IMobiusErrorReporter::Get(Self))
					{
						Reporter->ReportError(
							NSLOCTEXT("MobiusBRisk", "BRiskNoResultsTitleBar", "B-Risk Results Missing"),
							NSLOCTEXT("MobiusBRisk", "BRiskNoResultsTitle",
								"Scenario loaded, but it has no simulation results"),
							FText::Format(
								NSLOCTEXT("MobiusBRisk", "BRiskNoResultsBody",
									"The building geometry imported successfully, but this model has not been run "
									"through B-Risk yet, so there is no smoke, tenability or dose data to show.\n\n"
									"The following simulation output is missing:\n{0}"),
								FText::FromString(FString::Join(
									Self->ScenarioData.MissingResultFiles, LINE_TERMINATOR))),
							NSLOCTEXT("MobiusBRisk", "BRiskImportSource", "BRiskDataSubsystem"),
							EMobiusErrorSeverity::Warning);
					}
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

void UBRiskDataSubsystem::SetShowClosedOpeningPanels(bool bEnabled)
{
	bShowClosedOpeningPanels = bEnabled;

	// Pure visibility on components that already exist - no rebuild, and nothing to confirm with the
	// user first, unlike the room-geometry toggle above. With no scenario loaded the flag simply
	// applies when GenerateHazardVisuals next pushes it.
	if (HazardVisualizerActor && !HazardVisualizerActor->IsActorBeingDestroyed())
	{
		HazardVisualizerActor->SetClosedOpeningPanelsEnabled(bEnabled);
	}
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

	// The visualizer is respawned per load and starts with its own default, so a user who ticked the
	// box before loading a second scenario would silently lose it. Push the kept flag onto the new
	// actor rather than relying on the two defaults agreeing.
	HazardVisualizerActor->SetClosedOpeningPanelsEnabled(bShowClosedOpeningPanels);

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

			// A shut opening carries no flow. Without this the flow bands were drawn from the vent's
			// STATIC geometry at every timestep, so every door in the model kept streaming after
			// B-Risk closed it - measured against B-Risk's own wallventflows.txt, up to 2.9 kg/s
			// through openings it reports as absent (i.e. exactly zero). Leaving the default-
			// constructed entry gives bHasFlow=false, which is what SetVentFlows hides on.
			if (!Vent.IsOpenAtTime(static_cast<double>(TimeSeconds)))
			{
				continue;
			}

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

	/** Grid-line weld distance, and the smallest wall panel worth emitting, in centimetres. */
	constexpr double WallBandWeldCm = 0.01;

	/** One opening reduced to a rectangle in a wall's own (distance along, height) frame. */
	struct FWallOpeningRect
	{
		double StartCm = 0.0;
		double EndCm = 0.0;
		double SillZ = 0.0;
		double HeadZ = 0.0;
	};

	/**
	 * The lining of one opening: a tunnel running from the wall's inner face outward through the
	 * wall body, so the opening reads as a hole with depth rather than a gap in a paper shell.
	 */
	struct FOpeningRevealCm
	{
		FVector2D InnerStart = FVector2D::ZeroVector;
		FVector2D InnerEnd = FVector2D::ZeroVector;

		/** Outward wall normal times the host wall thickness - the vector from inner face to outer. */
		FVector2D OutwardCm = FVector2D::ZeroVector;

		double SillZ = 0.0;
		double HeadZ = 0.0;

		/** Set when this came from the opening's own roomA, which wins over the far side of the wall. */
		bool bFromPrimaryRoom = false;

		bool bValid = false;
	};
}

bool UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
	const TArray<FBRiskRoomGeometry>& Rooms,
	const TArray<FBRiskVentGeometry>& Vents,
	float Scale,
	BRiskCoord::ERoomFrame Frame,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	FString* OutError,
	bool bCutLeakageOpenings)
{
	// Generates single-sided, outward-facing room shells in Unreal space.
	//
	// This is USER-FACING, not dormant: bAutoGenerateRoomGeometryOnLoad defaults false, but
	// LoadRoomGeometryCheckBox in SimulationSettingsWidget drives it through
	// SetRoomGeometryEnabled, which rebuilds immediately when a scenario is already loaded. So a
	// change here is visible the moment someone ticks that box - owner-verified on screen
	// 2026-08-06 with the openings cut.
	//
	// Two paths, chosen per room and never mixed within a room:
	//
	//  * Room HAS a Zones-data.json footprint -> extruded polygon prism, converted with
	//    BRiskCoord::FootprintToUnreal (Y negated). This is the real floor plan, with the real
	//    door/window openings cut out of its walls and lined to the host wall's thickness.
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

	// One reveal per opening rather than one per room that cuts it, so a shared wall - cut from
	// both sides, both tunnels occupying the same wall body - does not get two coincident,
	// co-facing sets of quads. Indexed by vent so the emission order does not depend on room order.
	TArray<FOpeningRevealCm> RevealsByVent;
	RevealsByVent.SetNum(Vents.Num());

	int32 CutOpenings = 0;
	int32 RejectedFarOpenings = 0;
	int32 ClampedOpenings = 0;

	// Emits one wall panel, wound to face outward. Deliberately the same two triangles in the same
	// order as the pre-hole code so a room with no openings produces byte-identical geometry.
	const auto AppendWallPanel = [&AddTriangleFacing](
		const FVector2D& From,
		const FVector2D& To,
		const FVector& Outward,
		double BottomZ,
		double TopZ)
	{
		AddTriangleFacing(
			FVector(From.X, From.Y, BottomZ),
			FVector(To.X, To.Y, BottomZ),
			FVector(To.X, To.Y, TopZ),
			Outward);
		AddTriangleFacing(
			FVector(From.X, From.Y, BottomZ),
			FVector(To.X, To.Y, TopZ),
			FVector(From.X, From.Y, TopZ),
			Outward);
	};

	// Builds one wall with its openings removed.
	//
	// Grid decomposition rather than a triangulator with holes, because the two shapes this
	// actually has to produce are the ones an ear-clipper handles worst. Measured in the 12-room
	// model: a door can span its wall corner to corner (the corridor's 100 cm doorway stubs, where
	// the opening runs 0.00..100.00 of a 100 cm edge) - that is a notch, and a "hole" whose vertices
	// lie on the outer ring is degenerate. And 15 of the 18 leakage openings sit wholly inside a
	// door's span - a hole inside a hole. Splitting the wall at every opening boundary and dropping
	// the covered cells is exact for both, unions overlaps for free, and needs no triangulator.
	const auto AppendWallWithOpenings = [&AppendWallPanel](
		const FVector2D& Start,
		const FVector2D& End,
		const FVector& Outward,
		double FloorZ,
		double CeilingZ,
		const TArray<FWallOpeningRect>& Openings)
	{
		const double EdgeLengthCm = FVector2D::Distance(Start, End);
		if (EdgeLengthCm <= WallBandWeldCm)
		{
			return;
		}

		// Exact at both ends: reconstructing the far corner as Start + Unit * Length would move it
		// by an ulp or two, which is enough to change the mesh of a room that has no openings.
		const FVector2D AlongUnit = (End - Start) / EdgeLengthCm;
		const auto PointAt = [&Start, &End, &AlongUnit, EdgeLengthCm](double AlongCm) -> FVector2D
		{
			if (AlongCm <= 0.0) { return Start; }
			if (AlongCm >= EdgeLengthCm) { return End; }
			return Start + AlongUnit * AlongCm;
		};

		const auto SortAndWeld = [](TArray<double>& Values)
		{
			Values.Sort();
			for (int32 Index = Values.Num() - 1; Index > 0; --Index)
			{
				if (Values[Index] - Values[Index - 1] < WallBandWeldCm)
				{
					Values.RemoveAt(Index);
				}
			}
		};

		TArray<double> AlongBands = { 0.0, EdgeLengthCm };
		TArray<double> HeightBands = { FloorZ, CeilingZ };
		for (const FWallOpeningRect& Opening : Openings)
		{
			AlongBands.Add(Opening.StartCm);
			AlongBands.Add(Opening.EndCm);
			HeightBands.Add(Opening.SillZ);
			HeightBands.Add(Opening.HeadZ);
		}
		SortAndWeld(AlongBands);
		SortAndWeld(HeightBands);

		for (int32 HeightIndex = 0; HeightIndex + 1 < HeightBands.Num(); ++HeightIndex)
		{
			const double BottomZ = HeightBands[HeightIndex];
			const double TopZ = HeightBands[HeightIndex + 1];
			const double MidZ = (BottomZ + TopZ) * 0.5;

			// Merge neighbouring surviving cells along the wall, so a continuous strip - the band
			// under a row of door sills, say - stays one panel instead of one per opening boundary.
			int32 RunStart = INDEX_NONE;
			for (int32 AlongIndex = 0; AlongIndex + 1 < AlongBands.Num(); ++AlongIndex)
			{
				const double MidAlong = (AlongBands[AlongIndex] + AlongBands[AlongIndex + 1]) * 0.5;

				bool bCovered = false;
				for (const FWallOpeningRect& Opening : Openings)
				{
					if (MidAlong > Opening.StartCm && MidAlong < Opening.EndCm
						&& MidZ > Opening.SillZ && MidZ < Opening.HeadZ)
					{
						bCovered = true;
						break;
					}
				}

				if (!bCovered && RunStart == INDEX_NONE)
				{
					RunStart = AlongIndex;
				}

				const bool bLastCell = (AlongIndex + 2 >= AlongBands.Num());
				if (RunStart != INDEX_NONE && (bCovered || bLastCell))
				{
					const int32 RunEnd = bCovered ? AlongIndex : AlongIndex + 1;
					AppendWallPanel(
						PointAt(AlongBands[RunStart]), PointAt(AlongBands[RunEnd]),
						Outward, BottomZ, TopZ);
					RunStart = INDEX_NONE;
				}
			}
		}
	};

	// Builds the true floor plan for one room: triangulated floor and ceiling caps plus the walls,
	// with the room's real openings cut out of them.
	const auto AppendFootprintRoom = [&AddTriangleFacing, &AppendWallWithOpenings, &Vents,
		&RevealsByVent, &CutOpenings, &RejectedFarOpenings, &ClampedOpenings,
		Scale, Frame, bCutLeakageOpenings](
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

		// Resolve every opening this room owns onto one of its walls, ONCE, before building any
		// wall - one nearest-edge search per opening rather than one per opening per edge.
		//
		// A B-Risk vent's own (face, offset) is not usable here and is not a fallback: both are
		// coordinates in the area/perimeter-equivalent RECTANGLE (SR282 eq. 1-2), which has neither
		// the edge count nor the perimeter length of the real footprint. What makes the cut possible
		// is the Zones-data.json opening CENTRE - a real position in the same frame as the polygon.
		TMap<int32, TArray<FWallOpeningRect>> OpeningsByEdge;
		for (int32 VentIndex = 0; VentIndex < Vents.Num(); ++VentIndex)
		{
			const FBRiskVentGeometry& Vent = Vents[VentIndex];

			// Both sides: one opening record describes a shared wall, so the far room needs the hole
			// too or a door reads as an opening from one side and a solid wall from the other.
			if (!Vent.bHasPlacement
				|| (Vent.FromRoomId != Room.RoomId && Vent.ToRoomId != Room.RoomId)
				|| Vent.Width <= 0.0 || Vent.Height <= 0.0)
			{
				continue;
			}

			if (Vent.Kind == EBRiskVentKind::Leakage && !bCutLeakageOpenings)
			{
				continue;
			}

			// The TRUE opening size, exactly as ComputeVentSlab resolves it, so the hazard marker
			// and the hole it sits in are the same rectangle. Vent.Width/Height are the MODELLED
			// figures B-Risk simulated - commonly half a door leaf - and cutting those would leave
			// the marker overhanging its own hole.
			const double WidthCm = (Vent.PhysicalWidth > 0.0 ? Vent.PhysicalWidth : Vent.Width) * Scale;
			const double DrawHeight = (Vent.PhysicalHeight > 0.0) ? Vent.PhysicalHeight : Vent.Height;

			BRiskCoord::FOpeningEdgePlacement Placement;
			const FVector CentreCm = BRiskCoord::FootprintToUnreal(Vent.CentreMetres, Scale);
			if (!BRiskCoord::ResolveOpeningEdge(Ring, FVector2D(CentreCm.X, CentreCm.Y), WidthCm, Placement))
			{
				continue;
			}

			// Nearest is not on. Without this bound an opening belonging to a wall this room does
			// not have is still cut into whichever of its walls happens to be closest.
			if (Placement.DistanceCm > BRiskCoord::MaxOpeningStandoffCm(Vent.HostThicknessMetres, Scale))
			{
				++RejectedFarOpenings;
				continue;
			}

			FWallOpeningRect Rect;
			Rect.StartCm = FMath::Max(0.0, Placement.AlongCm - WidthCm * 0.5);
			Rect.EndCm = FMath::Min(Placement.EdgeLengthCm, Placement.AlongCm + WidthCm * 0.5);
			Rect.SillZ = FMath::Clamp((Room.Origin.Z + Vent.SillHeight) * Scale, FloorZ, CeilingZ);
			Rect.HeadZ = FMath::Clamp((Room.Origin.Z + Vent.SillHeight + DrawHeight) * Scale, FloorZ, CeilingZ);

			if (Rect.EndCm - Rect.StartCm <= WallBandWeldCm || Rect.HeadZ - Rect.SillZ <= WallBandWeldCm)
			{
				continue;
			}

			// bFitsOnEdge is a tie-break inside the resolver, not a rejection, so a corner-straddling
			// opening arrives wider than its wall and has just been clipped to it. Say so rather than
			// let a half-cut door look intentional.
			if (!Placement.bFitsOnEdge)
			{
				++ClampedOpenings;
			}

			OpeningsByEdge.FindOrAdd(Placement.EdgeIndex).Add(Rect);
			++CutOpenings;

			// Record the lining. hostThickness is what makes an opening a hole with depth rather
			// than a gap in a paper shell; without it (pre-v2 export) the hole is still cut, just
			// unlined, because there is no wall thickness anywhere in that data to invent one from.
			const bool bPrimaryRoom = (Vent.FromRoomId == Room.RoomId);
			const double DepthCm = Vent.HostThicknessMetres * Scale;
			if (DepthCm > WallBandWeldCm
				&& RevealsByVent.IsValidIndex(VentIndex)
				&& (!RevealsByVent[VentIndex].bValid || (bPrimaryRoom && !RevealsByVent[VentIndex].bFromPrimaryRoom)))
			{
				const FVector2D& EdgeStart = Ring[Placement.EdgeIndex];
				const FVector2D& EdgeEnd = Ring[(Placement.EdgeIndex + 1) % Ring.Num()];
				const FVector2D AlongUnit = (EdgeEnd - EdgeStart).GetSafeNormal();

				FOpeningRevealCm& Reveal = RevealsByVent[VentIndex];
				Reveal.InnerStart = EdgeStart + AlongUnit * Rect.StartCm;
				Reveal.InnerEnd = EdgeStart + AlongUnit * Rect.EndCm;
				// Ring is counter-clockwise, so (dy, -dx) points out of the room - the direction the
				// wall body lies in, since the polygon is the room's inner face.
				Reveal.OutwardCm = FVector2D(AlongUnit.Y, -AlongUnit.X) * DepthCm;
				Reveal.SillZ = Rect.SillZ;
				Reveal.HeadZ = Rect.HeadZ;
				Reveal.bFromPrimaryRoom = bPrimaryRoom;
				Reveal.bValid = true;
			}
		}

		const TArray<FWallOpeningRect> NoOpenings;
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

			const TArray<FWallOpeningRect>* EdgeOpenings = OpeningsByEdge.Find(Index);
			AppendWallWithOpenings(
				Start, End, Outward, FloorZ, CeilingZ,
				EdgeOpenings ? *EdgeOpenings : NoOpenings);
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

		// Same source of truth as the smoke volumes, the hazard markers and the egress bounds, so a
		// room without a footprint lands in the SAME frame as the rooms that have one.
		//
		// This used to call ToUnrealBox directly, which is the legacy X<->Y swap regardless of the
		// Frame this function was handed. A scenario where any room has a Zones-data.json footprint
		// resolves to Frame::Revit, so a polygon-less room in that scenario came out rotated 90
		// degrees about the world origin from every other room - the exact fault the warning above
		// this loop describes but did not prevent. Under Frame::SmokeviewSwap MakeRoomFootprint
		// returns byte-identical output to ToUnrealBox, so the no-JSON case is unchanged.
		//
		// Still only a placement fix: the rectangle is area/perimeter-equivalent (SR282 eq. 1-2) and
		// L is by definition the larger root, so a room longer in Y arrives transposed. That is a
		// B-Risk limitation and needs a footprint to solve, not a frame.
		const FBox RoomBoxCm = BRiskCoord::MakeRoomFootprint(Room, Scale, Frame).Bounds;
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
			//
			// Matches NOTHING in a scenario that has Zones-data.json openings[], and that is the
			// intended degrade rather than a gap to close. Those vents carry a real centre and their
			// Face is deliberately cleared to INDEX_NONE, because the .smv face column does not
			// identify a wall (measured: face 2 and face 3 each map to three different wall normals
			// across the 12-room model). A room reaching this path has no polygon, so there is no
			// real wall to put the centre against - leaving it solid is the honest result. Rooms that
			// DO have a polygon get their real openings cut on the path above.
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

	// Line every cut opening, once. Separate from the wall build on purpose: the hole is the thing
	// that had to happen and it stands alone, whereas the lining is the only part that depends on
	// hostThickness, so it can be dropped without disturbing the cut.
	{
		const auto AddRevealQuad = [&AddTriangleFacing](
			const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Facing)
		{
			AddTriangleFacing(A, B, C, Facing);
			AddTriangleFacing(A, C, D, Facing);
		};

		for (const FOpeningRevealCm& Reveal : RevealsByVent)
		{
			if (!Reveal.bValid)
			{
				continue;
			}

			const FVector2D OuterStart = Reveal.InnerStart + Reveal.OutwardCm;
			const FVector2D OuterEnd = Reveal.InnerEnd + Reveal.OutwardCm;
			const FVector2D AlongPlan = (Reveal.InnerEnd - Reveal.InnerStart).GetSafeNormal();
			const FVector Along(AlongPlan.X, AlongPlan.Y, 0.0);

			const FVector InnerStartSill(Reveal.InnerStart.X, Reveal.InnerStart.Y, Reveal.SillZ);
			const FVector InnerStartHead(Reveal.InnerStart.X, Reveal.InnerStart.Y, Reveal.HeadZ);
			const FVector InnerEndSill(Reveal.InnerEnd.X, Reveal.InnerEnd.Y, Reveal.SillZ);
			const FVector InnerEndHead(Reveal.InnerEnd.X, Reveal.InnerEnd.Y, Reveal.HeadZ);
			const FVector OuterStartSill(OuterStart.X, OuterStart.Y, Reveal.SillZ);
			const FVector OuterStartHead(OuterStart.X, OuterStart.Y, Reveal.HeadZ);
			const FVector OuterEndSill(OuterEnd.X, OuterEnd.Y, Reveal.SillZ);
			const FVector OuterEndHead(OuterEnd.X, OuterEnd.Y, Reveal.HeadZ);

			// All four face INTO the opening, so the lining is what you see looking through it.
			AddRevealQuad(InnerStartSill, OuterStartSill, OuterStartHead, InnerStartHead, Along);
			AddRevealQuad(InnerEndSill, OuterEndSill, OuterEndHead, InnerEndHead, -Along);
			AddRevealQuad(InnerStartSill, InnerEndSill, OuterEndSill, OuterStartSill, FVector::UpVector);
			AddRevealQuad(InnerStartHead, InnerEndHead, OuterEndHead, OuterStartHead, FVector::DownVector);
		}
	}

	if (FootprintRoomCount > 0)
	{
		int32 LeakageSkipped = 0;
		int32 OnRectangleRooms = 0;
		for (const FBRiskVentGeometry& Vent : Vents)
		{
			if (!Vent.bHasPlacement)
			{
				continue;
			}

			const FBRiskRoomGeometry* Owner = Rooms.FindByPredicate(
				[&Vent](const FBRiskRoomGeometry& Candidate) { return Candidate.RoomId == Vent.FromRoomId; });
			if (!Owner || Owner->FootprintPolygon.Num() < 3)
			{
				++OnRectangleRooms;
				continue;
			}

			LeakageSkipped += (!bCutLeakageOpenings && Vent.Kind == EBRiskVentKind::Leakage) ? 1 : 0;
		}

		UE_LOG(LogBRiskDataSubsystem, Log,
			TEXT("B-Risk room geometry: %d of %d rooms built from Zones-data.json footprints; ")
			TEXT("%d opening(s) cut (a shared wall counts once per side). %d leakage opening(s) left ")
			TEXT("solid, %d too far from any wall of their own room to cut, %d clipped to fit their ")
			TEXT("wall, %d on rooms with no footprint."),
			FootprintRoomCount,
			Rooms.Num(),
			CutOpenings,
			LeakageSkipped,
			RejectedFarOpenings,
			ClampedOpenings,
			OnRectangleRooms);
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
