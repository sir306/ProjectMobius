// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskDataSubsystem.h"
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
	ScenarioData = FBRiskScenarioData();
	LastError.Reset();
	ActiveSmvPath = SmvFilePath;
	bIsLoading = false;

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

				Self->ConfigurePlaybackFromScenario();

				if (Self->bAutoGenerateRoomGeometryOnLoad)
				{
					Self->GenerateAndLoadRoomGeometry();
				}

				if (Self->bAutoGenerateSmokeVolumesOnLoad)
				{
					Self->GenerateAndLoadSmokeVolumes();
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
	ScenarioData = FBRiskScenarioData();
	LastError.Reset();
	ActiveSmvPath.Reset();
	bIsLoading = false;
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
	}

	UE_LOG(LogBRiskDataSubsystem, Log,
		TEXT("Generated B-Risk smoke volumes: rooms=%d createdVolumes=%d zones=%d series=HGT_1 scale=%g"),
		ScenarioData.Rooms.Num(),
		SmokeVisualizerActor->GetSmokeVolumeCount(),
		ScenarioData.ZoneTables.Num(),
		RoomGeometryScale);

	UpdateSmokeAtTime(0.0f);
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
		float RoomSmoke = 1.0f;
		const bool bHasLayerHeight = SampleSeriesAtTime(RoomIndex, TEXT("HGT_1"), TimeSeconds, LayerHeight);

		if (bHasLayerHeight)
		{
			RoomSmoke = ComputeRoomSmokeScalar(LayerHeight, ScenarioData.Rooms[RoomIndex].Size.Z);
		}
		else if (!bHasWarnedMissingSmokeSeries)
		{
			bHasWarnedMissingSmokeSeries = true;
			UE_LOG(LogBRiskDataSubsystem, Warning,
				TEXT("B-Risk smoke visualizer could not find HGT_1 for one or more zones; affected rooms remain clear."));
		}

		UE_LOG(LogBRiskDataSubsystem, Log,
			TEXT("B-Risk smoke sample: room=%d zone=%d series=HGT_1 time=%g HGT_1=%g roomHeight=%g RoomSmoke=%g"),
			RoomIndex,
			RoomIndex,
			TimeSeconds,
			bHasLayerHeight ? LayerHeight : -1.0,
			ScenarioData.Rooms[RoomIndex].Size.Z,
			RoomSmoke);

		if (SmokeVisualizerActor->SetRoomSmokeScalar(RoomIndex, RoomSmoke))
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

void UBRiskDataSubsystem::HandleNewSimulationTime(float NewCurrentTime)
{
	UpdateSmokeAtTime(NewCurrentTime);
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
	TimeSubsystem->bIsPaused = true;

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
