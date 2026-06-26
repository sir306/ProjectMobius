// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskDataSubsystem.h"
#include "BRisk/BRiskHazardVisualizer.h"
#include "BRisk/BRiskSmokeVisualizer.h"
#include "BuildingGenerator/RuntimeMeshBuilder.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

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
	ScenarioData = FBRiskScenarioData();
	LastError.Reset();
	ActiveSmvPath = SmvFilePath;
	bIsLoading = false;
	OnBRiskScenarioCleared.Broadcast();

	if (SmvFilePath.IsEmpty())
	{
		LastError = TEXT("LoadScenarioFromSmv called with an empty path.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
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
	UE_LOG(LogBRiskDataSubsystem, Log,
		TEXT("Generated B-Risk room geometry: rooms=%d vertices=%d triangles=%d scale=%g"),
		ScenarioData.Rooms.Num(),
		Vertices.Num(),
		Triangles.Num() / 3,
		RoomGeometryScale);

	OnBRiskGeometryReady.Broadcast();
	return true;
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

	if (!SmokeVisualizerActor->ConfigureFromRooms(ScenarioData.Rooms, RoomGeometryScale))
	{
		LastError = TEXT("Failed to configure B-Risk smoke visualizer volumes.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
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
		RoomGeometryScale))
	{
		LastError = TEXT("Failed to configure B-Risk hazard visuals.");
		UE_LOG(LogBRiskDataSubsystem, Warning, TEXT("%s"), *LastError);
		return false;
	}

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

	HazardVisualizerActor->SetSimulationTime(TimeSeconds);
	return bUpdatedAny || ScenarioData.Sprinklers.Num() > 0;
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

bool UBRiskDataSubsystem::BuildRoomMeshDataFromRooms(
	const TArray<FBRiskRoomGeometry>& Rooms,
	const TArray<FBRiskVentGeometry>& Vents,
	float Scale,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	FString* OutError)
{
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

	for (const FBRiskRoomGeometry& Room : Rooms)
	{
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

		const FVector Min = Room.Origin * Scale;
		const FVector Max = (Room.Origin + Room.Size) * Scale;

		const FBRiskVentGeometry* RoomVent = nullptr;
		for (const FBRiskVentGeometry& Vent : Vents)
		{
			if (Vent.FromRoomId == Room.RoomId
				&& Vent.Width > 0.0
				&& Vent.Height > 0.0)
			{
				RoomVent = &Vent;
				break;
			}
		}

		const auto AddWallWithOptionalOpening = [&](
			int32 Face,
			const FVector& A,
			const FVector& B,
			const FVector& C,
			const FVector& D,
			bool bAxisIsX,
			bool bReverseOpeningWinding,
			double WallStart,
			double WallEnd)
		{
			if (!RoomVent || RoomVent->Face != Face)
			{
				AddQuad(A, B, C, D);
				return;
			}

			const double OpeningStart = FMath::Clamp(RoomVent->Offset * Scale, WallStart, WallEnd);
			const double OpeningEnd = FMath::Clamp((RoomVent->Offset + RoomVent->Width) * Scale, WallStart, WallEnd);
			const double Sill = FMath::Clamp((Room.Origin.Z + RoomVent->SillHeight) * Scale, static_cast<double>(Min.Z), static_cast<double>(Max.Z));
			const double Head = FMath::Clamp((Room.Origin.Z + RoomVent->SillHeight + RoomVent->Height) * Scale, static_cast<double>(Min.Z), static_cast<double>(Max.Z));

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

		AddWallWithOptionalOpening(1, FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z), true, false, Min.X, Max.X);
		AddWallWithOptionalOpening(2, FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), true, true, Min.X, Max.X);
		AddWallWithOptionalOpening(3, FVector(Min.X, Max.Y, Min.Z), FVector(Min.X, Min.Y, Min.Z), FVector(Min.X, Min.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z), false, true, Min.Y, Max.Y);
		AddWallWithOptionalOpening(4, FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z), false, false, Min.Y, Max.Y);
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
	if (ScenarioData.ZoneTables.Num() == 0 || ScenarioData.ZoneTables[0].TimeSeconds.Num() == 0)
	{
		return;
	}

	UTimeDilationSubSystem* TimeSubsystem = GetTimeDilationSubsystem();
	if (!TimeSubsystem)
	{
		UE_LOG(LogBRiskDataSubsystem, Warning,
			TEXT("B-Risk scenario loaded but TimeDilationSubSystem is unavailable; play bar timing was not configured."));
		return;
	}

	const TArray<double>& Times = ScenarioData.ZoneTables[0].TimeSeconds;
	const double FirstTime = Times[0];
	const float TotalTime = static_cast<float>(Times.Last());
	const float TimeBetweenSteps = Times.Num() > 1
		? static_cast<float>(Times[1] - Times[0])
		: TimeSubsystem->TimeBetweenSteps;

	if (TimeBetweenSteps > UE_KINDA_SMALL_NUMBER)
	{
		TimeSubsystem->UpdateTimeBetweenData(TimeBetweenSteps);
	}
	TimeSubsystem->UpdateTotalTime(TotalTime);
	TimeSubsystem->OverrideCurrentTime(0.0f, 1);
	TimeSubsystem->SetSimulationPaused(true);

	UE_LOG(LogBRiskDataSubsystem, Log,
		TEXT("Configured B-Risk playback from zone Time column: first=%g last=%g samples=%d TotalTime=%g TimeBetweenSteps=%g CurrentTime=0 paused=true"),
		FirstTime,
		Times.Last(),
		Times.Num(),
		TimeSubsystem->TotalTime,
		TimeSubsystem->TimeBetweenSteps);
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
