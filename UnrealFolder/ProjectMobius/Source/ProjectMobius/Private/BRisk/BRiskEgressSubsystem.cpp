// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskEgressSubsystem.h"

#include "BRisk/BRiskDataSubsystem.h"
#include "BRiskDataImporter.h"
#include "Subsystems/TimeDilationSubSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogBRiskEgressSubsystem, Log, All);

namespace
{
	constexpr float AirMolecularWeight = 28.97f;
	constexpr float CarbonMonoxideMolecularWeight = 28.01f;
	constexpr float CarbonDioxideMolecularWeight = 44.01f;
	constexpr float HydrogenCyanideMolecularWeight = 27.03f;
	constexpr float OxygenMolecularWeight = 32.00f;

	bool UnitContains(const FString& Unit, const TCHAR* Text)
	{
		return Unit.Contains(Text, ESearchCase::IgnoreCase);
	}
}

void UBRiskEgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	BRiskDataSubsystem = Collection.InitializeDependency<UBRiskDataSubsystem>();
	TimeDilationSubsystem = Collection.InitializeDependency<UTimeDilationSubSystem>();
	Super::Initialize(Collection);

	if (BRiskDataSubsystem)
	{
		BRiskDataSubsystem->OnBRiskScenarioLoaded.AddDynamic(
			this,
			&UBRiskEgressSubsystem::HandleScenarioLoaded);
		BRiskDataSubsystem->OnBRiskScenarioCleared.AddDynamic(
			this,
			&UBRiskEgressSubsystem::HandleScenarioCleared);
	}

	if (TimeDilationSubsystem)
	{
		TimeDilationSubsystem->OnNewCurrentTime.AddDynamic(
			this,
			&UBRiskEgressSubsystem::HandleSimulationTimeChanged);
	}

	if (BRiskDataSubsystem && BRiskDataSubsystem->HasScenarioData())
	{
		RebuildRoomCache();
		RefreshAtTime(TimeDilationSubsystem ? TimeDilationSubsystem->GetCurrentSimTime() : 0.0f);
	}
}

void UBRiskEgressSubsystem::Deinitialize()
{
	if (BRiskDataSubsystem)
	{
		BRiskDataSubsystem->OnBRiskScenarioLoaded.RemoveDynamic(
			this,
			&UBRiskEgressSubsystem::HandleScenarioLoaded);
		BRiskDataSubsystem->OnBRiskScenarioCleared.RemoveDynamic(
			this,
			&UBRiskEgressSubsystem::HandleScenarioCleared);
	}

	if (TimeDilationSubsystem)
	{
		TimeDilationSubsystem->OnNewCurrentTime.RemoveDynamic(
			this,
			&UBRiskEgressSubsystem::HandleSimulationTimeChanged);
	}

	ClearCachedData();
	BRiskDataSubsystem = nullptr;
	TimeDilationSubsystem = nullptr;
	Super::Deinitialize();
}

bool UBRiskEgressSubsystem::SampleAgentEnvironment(
	const FVector& AgentFeetWorldLocation,
	const float BreathingHeightCm,
	FAgentBRiskHazardSample& OutSample,
	const int32 PreferredRoomIndex) const
{
	const FVector BreathingLocation =
		AgentFeetWorldLocation + FVector(0.0, 0.0, FMath::Max(BreathingHeightCm, 0.0f));
	const FBRiskEgressRoomState* RoomState =
		FindRoomStateAtLocation(BreathingLocation, PreferredRoomIndex);
	if (!RoomState)
	{
		OutSample = FAgentBRiskHazardSample();
		return false;
	}

	const bool bUpperLayer = BreathingLocation.Z >= RoomState->LayerHeightWorldCm;
	const FBRiskEgressLayerState& Layer =
		bUpperLayer ? RoomState->UpperLayer : RoomState->LowerLayer;

	OutSample.RoomIndex = RoomState->RoomIndex;
	OutSample.RoomId = RoomState->RoomId;
	OutSample.SampleTimeSeconds = RoomState->SampleTimeSeconds;
	OutSample.LayerHeightWorldCm = RoomState->LayerHeightWorldCm;
	OutSample.BreathingWorldZCm = BreathingLocation.Z;
	OutSample.TemperatureC = Layer.TemperatureC;
	OutSample.OpticalDensityPerMeter = Layer.OpticalDensityPerMeter;
	OutSample.CarbonMonoxidePpm = Layer.CarbonMonoxidePpm;
	OutSample.CarbonDioxidePercent = Layer.CarbonDioxidePercent;
	OutSample.HydrogenCyanidePpm = Layer.HydrogenCyanidePpm;
	OutSample.OxygenPercent = Layer.OxygenPercent;
	OutSample.SootKgPerCubicMeter = Layer.SootKgPerCubicMeter;
	OutSample.DirectGasFed = RoomState->DirectGasFed;
	OutSample.DirectThermalFed = RoomState->DirectThermalFed;
	OutSample.AvailableChannels = RoomState->AvailableChannels | Layer.AvailableChannels;
	OutSample.bUpperLayer = bUpperLayer;
	return true;
}

const FBRiskEgressRoomState* UBRiskEgressSubsystem::FindRoomStateAtLocation(
	const FVector& WorldLocation,
	const int32 PreferredRoomIndex) const
{
	if (RoomStates.IsValidIndex(PreferredRoomIndex)
		&& RoomStates[PreferredRoomIndex].WorldBounds.IsInsideOrOn(WorldLocation))
	{
		return &RoomStates[PreferredRoomIndex];
	}

	const FBRiskEgressRoomState* BestMatch = nullptr;
	double BestVolume = TNumericLimits<double>::Max();

	for (const FBRiskEgressRoomState& RoomState : RoomStates)
	{
		if (RoomState.RoomIndex == PreferredRoomIndex)
		{
			continue;
		}

		if (!RoomState.WorldBounds.IsInsideOrOn(WorldLocation))
		{
			continue;
		}

		const double Volume = RoomState.WorldBounds.GetVolume();
		if (Volume < BestVolume)
		{
			BestVolume = Volume;
			BestMatch = &RoomState;
		}
	}

	return BestMatch;
}

void UBRiskEgressSubsystem::RecordAgentHealth(
	const int32 AgentId,
	const float TimeSeconds,
	const FAgentEgressHealthFragment& Health)
{
	if (AgentId < 0 || TimeSeconds < 0.0f)
	{
		return;
	}

	TArray<FAgentEgressHealthHistorySample>& History = AgentHealthHistory.FindOrAdd(AgentId);
	FAgentEgressHealthHistorySample NewSample;
	NewSample.TimeSeconds = TimeSeconds;
	NewSample.Health = Health.Health;
	NewSample.CombinedHazardDose = Health.CombinedHazardDose;

	if (!History.IsEmpty())
	{
		FAgentEgressHealthHistorySample& LastSample = History.Last();
		if (FMath::IsNearlyEqual(LastSample.TimeSeconds, TimeSeconds, UE_KINDA_SMALL_NUMBER))
		{
			LastSample = NewSample;
			return;
		}

		if (TimeSeconds < LastSample.TimeSeconds)
		{
			return;
		}
	}

	if (History.Num() >= 2)
	{
		const FAgentEgressHealthHistorySample& PreviousSample = History[History.Num() - 2];
		const FAgentEgressHealthHistorySample& LastSample = History.Last();
		const float PreviousDuration = LastSample.TimeSeconds - PreviousSample.TimeSeconds;
		const float NewDuration = NewSample.TimeSeconds - LastSample.TimeSeconds;
		if (PreviousDuration > UE_KINDA_SMALL_NUMBER && NewDuration > UE_KINDA_SMALL_NUMBER)
		{
			const float PreviousDoseRate =
				(LastSample.CombinedHazardDose - PreviousSample.CombinedHazardDose)
				/ PreviousDuration;
			const float NewDoseRate =
				(NewSample.CombinedHazardDose - LastSample.CombinedHazardDose)
				/ NewDuration;
			if (FMath::IsNearlyEqual(PreviousDoseRate, NewDoseRate, 0.0001f))
			{
				History.Last() = NewSample;
				return;
			}
		}
	}

	History.Add(NewSample);
}

bool UBRiskEgressSubsystem::RestoreAgentHealth(
	const int32 AgentId,
	const float TimeSeconds,
	FAgentEgressHealthFragment& InOutHealth) const
{
	const TArray<FAgentEgressHealthHistorySample>* History = AgentHealthHistory.Find(AgentId);
	if (!History || History->IsEmpty())
	{
		return false;
	}

	const FAgentEgressHealthHistorySample* LowerSample = &(*History)[0];
	const FAgentEgressHealthHistorySample* UpperSample = LowerSample;

	if (TimeSeconds <= (*History)[0].TimeSeconds)
	{
		UpperSample = LowerSample;
	}
	else if (TimeSeconds >= History->Last().TimeSeconds)
	{
		LowerSample = &History->Last();
		UpperSample = LowerSample;
	}
	else
	{
		int32 LowerIndex = 0;
		int32 UpperIndex = History->Num() - 1;
		while (UpperIndex - LowerIndex > 1)
		{
			const int32 MidIndex = (LowerIndex + UpperIndex) / 2;
			if ((*History)[MidIndex].TimeSeconds <= TimeSeconds)
			{
				LowerIndex = MidIndex;
			}
			else
			{
				UpperIndex = MidIndex;
			}
		}

		LowerSample = &(*History)[LowerIndex];
		UpperSample = &(*History)[UpperIndex];
	}

	const float Duration = UpperSample->TimeSeconds - LowerSample->TimeSeconds;
	const float Alpha = Duration > UE_KINDA_SMALL_NUMBER
		? FMath::Clamp((TimeSeconds - LowerSample->TimeSeconds) / Duration, 0.0f, 1.0f)
		: 0.0f;
	InOutHealth.Health = FMath::Lerp(LowerSample->Health, UpperSample->Health, Alpha);
	InOutHealth.CombinedHazardDose = FMath::Lerp(
		LowerSample->CombinedHazardDose,
		UpperSample->CombinedHazardDose,
		Alpha);
	InOutHealth.bIsDead = InOutHealth.DeathTimeSeconds >= 0.0f
		&& TimeSeconds + UE_KINDA_SMALL_NUMBER >= InOutHealth.DeathTimeSeconds;
	if (InOutHealth.bIsDead)
	{
		InOutHealth.Health = 0.0f;
	}
	return true;
}

void UBRiskEgressSubsystem::HandleScenarioLoaded(const bool bSuccess)
{
	if (!bSuccess)
	{
		ClearCachedData();
		return;
	}

	RebuildRoomCache();
	RefreshAtTime(TimeDilationSubsystem ? TimeDilationSubsystem->GetCurrentSimTime() : 0.0f);
}

void UBRiskEgressSubsystem::HandleScenarioCleared()
{
	ClearCachedData();
}

void UBRiskEgressSubsystem::HandleSimulationTimeChanged(const float NewSimulationTime)
{
	RefreshAtTime(NewSimulationTime);
}

void UBRiskEgressSubsystem::RebuildRoomCache()
{
	ClearCachedData();
	if (!BRiskDataSubsystem || !BRiskDataSubsystem->HasScenarioData())
	{
		return;
	}

	const TArray<FBRiskRoomGeometry>& Rooms = BRiskDataSubsystem->GetRooms();
	const TArray<FBRiskZoneTable>& ZoneTables = BRiskDataSubsystem->GetZoneTables();
	const float Scale = BRiskDataSubsystem->GetRoomGeometryScale();

	RoomStates.SetNum(Rooms.Num());
	RoomSeriesCaches.SetNum(Rooms.Num());

	for (int32 RoomIndex = 0; RoomIndex < Rooms.Num(); ++RoomIndex)
	{
		const FBRiskRoomGeometry& Room = Rooms[RoomIndex];
		FBRiskEgressRoomState& RoomState = RoomStates[RoomIndex];
		RoomState.RoomIndex = RoomIndex;
		RoomState.RoomId = Room.RoomId;
		RoomState.WorldBounds = FBox(Room.Origin * Scale, (Room.Origin + Room.Size) * Scale);
		RoomState.LayerHeightWorldCm = RoomState.WorldBounds.Max.Z;

		if (!ZoneTables.IsValidIndex(RoomIndex))
		{
			continue;
		}

		const FBRiskZoneTable& ZoneTable = ZoneTables[RoomIndex];
		FRoomSeriesCache& SeriesCache = RoomSeriesCaches[RoomIndex];
		SeriesCache.SeriesNames.Reserve(ZoneTable.Series.Num());
		SeriesCache.SeriesUnits.Reserve(ZoneTable.Series.Num());
		SeriesCache.SeriesValues.Init(0.0f, ZoneTable.Series.Num());

		for (int32 SeriesIndex = 0; SeriesIndex < ZoneTable.Series.Num(); ++SeriesIndex)
		{
			const FBRiskSeries& Series = ZoneTable.Series[SeriesIndex];
			const FName NormalizedName = NormalizeSeriesName(Series.Name);
			SeriesCache.SeriesNames.Add(NormalizedName);
			SeriesCache.SeriesUnits.Add(Series.Unit);
			SeriesCache.SeriesLookup.FindOrAdd(NormalizedName, SeriesIndex);
		}
	}

	if (Rooms.Num() != ZoneTables.Num())
	{
		UE_LOG(
			LogBRiskEgressSubsystem,
			Warning,
			TEXT("B-Risk room/zone count differs (rooms=%d zones=%d). Egress data uses matching indices."),
			Rooms.Num(),
			ZoneTables.Num());
	}

	++Revision;
}

void UBRiskEgressSubsystem::RefreshAtTime(const float NewSimulationTime)
{
	if (!BRiskDataSubsystem || !BRiskDataSubsystem->HasScenarioData())
	{
		if (!RoomStates.IsEmpty())
		{
			ClearCachedData();
		}
		return;
	}

	if (RoomStates.Num() != BRiskDataSubsystem->GetRooms().Num())
	{
		RebuildRoomCache();
	}

	const TArray<FBRiskZoneTable>& ZoneTables = BRiskDataSubsystem->GetZoneTables();
	SampleTimeSeconds = NewSimulationTime;

	for (int32 RoomIndex = 0; RoomIndex < RoomSeriesCaches.Num(); ++RoomIndex)
	{
		if (!ZoneTables.IsValidIndex(RoomIndex))
		{
			continue;
		}

		const FBRiskZoneTable& ZoneTable = ZoneTables[RoomIndex];
		FRoomSeriesCache& SeriesCache = RoomSeriesCaches[RoomIndex];
		const int32 SeriesCount = FMath::Min(ZoneTable.Series.Num(), SeriesCache.SeriesValues.Num());

		for (int32 SeriesIndex = 0; SeriesIndex < SeriesCount; ++SeriesIndex)
		{
			float SampledValue = 0.0f;
			if (SampleAlignedSeries(
				ZoneTable.TimeSeconds,
				ZoneTable.Series[SeriesIndex].Values,
				NewSimulationTime,
				SampledValue))
			{
				SeriesCache.SeriesValues[SeriesIndex] = SampledValue;
			}
		}

		ResolveTypedRoomState(RoomIndex);
	}

	++Revision;
}

void UBRiskEgressSubsystem::ClearCachedData()
{
	RoomStates.Reset();
	RoomSeriesCaches.Reset();
	AgentHealthHistory.Reset();
	SampleTimeSeconds = 0.0f;
	++Revision;
	++ScenarioGeneration;
}

void UBRiskEgressSubsystem::ResolveTypedRoomState(const int32 RoomIndex)
{
	if (!RoomStates.IsValidIndex(RoomIndex))
	{
		return;
	}

	FBRiskEgressRoomState& RoomState = RoomStates[RoomIndex];
	RoomState.SampleTimeSeconds = SampleTimeSeconds;
	RoomState.DirectGasFed = 0.0f;
	RoomState.DirectThermalFed = 0.0f;
	RoomState.AvailableChannels = 0;
	RoomState.UpperLayer = FBRiskEgressLayerState();
	RoomState.LowerLayer = FBRiskEgressLayerState();

	float Value = 0.0f;
	const FString* Unit = nullptr;
	if (TryGetRawSeries(RoomIndex, {TEXT("HGT"), TEXT("LAYERHEIGHT")}, Value, Unit))
	{
		RoomState.LayerHeightWorldCm = RoomState.WorldBounds.Min.Z
			+ FMath::Max(Value, 0.0f) * BRiskDataSubsystem->GetRoomGeometryScale();
		RoomState.LayerHeightWorldCm = FMath::Clamp(
			RoomState.LayerHeightWorldCm,
			static_cast<float>(RoomState.WorldBounds.Min.Z),
			static_cast<float>(RoomState.WorldBounds.Max.Z));
		RoomState.AvailableChannels |= UE::Mobius::BRiskHazardChannels::LayerHeight;
	}
	else
	{
		RoomState.LayerHeightWorldCm = RoomState.WorldBounds.Max.Z;
	}

	if (TryGetRawSeries(RoomIndex, {TEXT("ULT"), TEXT("UPPERTEMPERATURE")}, Value, Unit))
	{
		RoomState.UpperLayer.TemperatureC = Value;
		RoomState.UpperLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::Temperature;
	}
	if (TryGetRawSeries(RoomIndex, {TEXT("LLT"), TEXT("LOWERTEMPERATURE")}, Value, Unit))
	{
		RoomState.LowerLayer.TemperatureC = Value;
		RoomState.LowerLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::Temperature;
	}

	if (TryGetRawSeries(RoomIndex, {TEXT("ULOD"), TEXT("UOD"), TEXT("UPPEROPTICALDENSITY")}, Value, Unit))
	{
		RoomState.UpperLayer.OpticalDensityPerMeter = FMath::Max(Value, 0.0f);
		RoomState.UpperLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::OpticalDensity;
	}
	if (TryGetRawSeries(RoomIndex, {TEXT("LLOD"), TEXT("LOD"), TEXT("LOWEROPTICALDENSITY")}, Value, Unit))
	{
		RoomState.LowerLayer.OpticalDensityPerMeter = FMath::Max(Value, 0.0f);
		RoomState.LowerLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::OpticalDensity;
	}

	if (TryGetConcentrationPpm(
		RoomIndex,
		{TEXT("UCO"), TEXT("COU"), TEXT("UPPERCO")},
		CarbonMonoxideMolecularWeight,
		Value))
	{
		RoomState.UpperLayer.CarbonMonoxidePpm = FMath::Max(Value, 0.0f);
		RoomState.UpperLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::CarbonMonoxide;
	}
	if (TryGetConcentrationPpm(
		RoomIndex,
		{TEXT("LCO"), TEXT("COL"), TEXT("LOWERCO")},
		CarbonMonoxideMolecularWeight,
		Value))
	{
		RoomState.LowerLayer.CarbonMonoxidePpm = FMath::Max(Value, 0.0f);
		RoomState.LowerLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::CarbonMonoxide;
	}

	if (TryGetConcentrationPercent(
		RoomIndex,
		{TEXT("UCO2"), TEXT("CO2U"), TEXT("UPPERCO2")},
		CarbonDioxideMolecularWeight,
		Value))
	{
		RoomState.UpperLayer.CarbonDioxidePercent = FMath::Max(Value, 0.0f);
		RoomState.UpperLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::CarbonDioxide;
	}
	if (TryGetConcentrationPercent(
		RoomIndex,
		{TEXT("LCO2"), TEXT("CO2L"), TEXT("LOWERCO2")},
		CarbonDioxideMolecularWeight,
		Value))
	{
		RoomState.LowerLayer.CarbonDioxidePercent = FMath::Max(Value, 0.0f);
		RoomState.LowerLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::CarbonDioxide;
	}

	if (TryGetConcentrationPpm(
		RoomIndex,
		{TEXT("UHCN"), TEXT("HCNU"), TEXT("UPPERHCN")},
		HydrogenCyanideMolecularWeight,
		Value))
	{
		RoomState.UpperLayer.HydrogenCyanidePpm = FMath::Max(Value, 0.0f);
		RoomState.UpperLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::HydrogenCyanide;
	}
	if (TryGetConcentrationPpm(
		RoomIndex,
		{TEXT("LHCN"), TEXT("HCNL"), TEXT("LOWERHCN")},
		HydrogenCyanideMolecularWeight,
		Value))
	{
		RoomState.LowerLayer.HydrogenCyanidePpm = FMath::Max(Value, 0.0f);
		RoomState.LowerLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::HydrogenCyanide;
	}

	if (TryGetConcentrationPercent(
		RoomIndex,
		{TEXT("UO2"), TEXT("O2U"), TEXT("UPPERO2")},
		OxygenMolecularWeight,
		Value))
	{
		RoomState.UpperLayer.OxygenPercent = FMath::Max(Value, 0.0f);
		RoomState.UpperLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::Oxygen;
	}
	if (TryGetConcentrationPercent(
		RoomIndex,
		{TEXT("LO2"), TEXT("O2L"), TEXT("LOWERO2")},
		OxygenMolecularWeight,
		Value))
	{
		RoomState.LowerLayer.OxygenPercent = FMath::Max(Value, 0.0f);
		RoomState.LowerLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::Oxygen;
	}

	if (TryGetRawSeries(RoomIndex, {TEXT("USOOT"), TEXT("UPPERSOOT")}, Value, Unit))
	{
		if (Unit && UnitContains(*Unit, TEXT("g/m")))
		{
			Value /= 1000.0f;
		}
		RoomState.UpperLayer.SootKgPerCubicMeter = FMath::Max(Value, 0.0f);
		RoomState.UpperLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::Soot;
	}
	if (TryGetRawSeries(RoomIndex, {TEXT("LSOOT"), TEXT("LOWERSOOT")}, Value, Unit))
	{
		if (Unit && UnitContains(*Unit, TEXT("g/m")))
		{
			Value /= 1000.0f;
		}
		RoomState.LowerLayer.SootKgPerCubicMeter = FMath::Max(Value, 0.0f);
		RoomState.LowerLayer.AvailableChannels |= UE::Mobius::BRiskHazardChannels::Soot;
	}

	if (TryGetRawSeries(
		RoomIndex,
		{TEXT("FEDGAS"), TEXT("GASFED"), TEXT("FEDTOXIC"), TEXT("TOXICFED")},
		Value,
		Unit))
	{
		RoomState.DirectGasFed = FMath::Max(Value, 0.0f);
		RoomState.AvailableChannels |= UE::Mobius::BRiskHazardChannels::DirectGasFed;
	}
	if (TryGetRawSeries(
		RoomIndex,
		{TEXT("FEDTHERMAL"), TEXT("THERMALFED"), TEXT("FEDHEAT"), TEXT("HEATFED")},
		Value,
		Unit))
	{
		RoomState.DirectThermalFed = FMath::Max(Value, 0.0f);
		RoomState.AvailableChannels |= UE::Mobius::BRiskHazardChannels::DirectThermalFed;
	}
}

FName UBRiskEgressSubsystem::NormalizeSeriesName(const FString& SeriesName)
{
	FString Normalized = SeriesName;
	Normalized.TrimStartAndEndInline();
	Normalized.ToUpperInline();
	Normalized.ReplaceInline(TEXT(" "), TEXT(""));
	Normalized.ReplaceInline(TEXT("-"), TEXT("_"));

	int32 LastUnderscore = INDEX_NONE;
	if (Normalized.FindLastChar(TEXT('_'), LastUnderscore) && LastUnderscore + 1 < Normalized.Len())
	{
		bool bNumericSuffix = true;
		for (int32 Index = LastUnderscore + 1; Index < Normalized.Len(); ++Index)
		{
			if (!FChar::IsDigit(Normalized[Index]))
			{
				bNumericSuffix = false;
				break;
			}
		}

		if (bNumericSuffix)
		{
			Normalized.LeftInline(LastUnderscore);
		}
	}

	return FName(*Normalized);
}

bool UBRiskEgressSubsystem::SampleAlignedSeries(
	const TArray<double>& Times,
	const TArray<double>& Values,
	const double TimeSeconds,
	float& OutValue)
{
	if (Times.IsEmpty() || Values.Num() != Times.Num())
	{
		return false;
	}

	if (TimeSeconds <= Times[0])
	{
		OutValue = static_cast<float>(Values[0]);
		return true;
	}
	if (TimeSeconds >= Times.Last())
	{
		OutValue = static_cast<float>(Values.Last());
		return true;
	}

	int32 LowerIndex = 0;
	int32 UpperIndex = Times.Num() - 1;
	while (UpperIndex - LowerIndex > 1)
	{
		const int32 MidIndex = (LowerIndex + UpperIndex) / 2;
		if (Times[MidIndex] <= TimeSeconds)
		{
			LowerIndex = MidIndex;
		}
		else
		{
			UpperIndex = MidIndex;
		}
	}

	const double TimeSpan = Times[UpperIndex] - Times[LowerIndex];
	const double Alpha = TimeSpan > UE_DOUBLE_SMALL_NUMBER
		? (TimeSeconds - Times[LowerIndex]) / TimeSpan
		: 0.0;
	OutValue = static_cast<float>(FMath::Lerp(Values[LowerIndex], Values[UpperIndex], Alpha));
	return true;
}

bool UBRiskEgressSubsystem::TryGetRawSeries(
	const int32 RoomIndex,
	const std::initializer_list<const TCHAR*> Aliases,
	float& OutValue,
	const FString*& OutUnit) const
{
	if (!RoomSeriesCaches.IsValidIndex(RoomIndex))
	{
		return false;
	}

	const FRoomSeriesCache& Cache = RoomSeriesCaches[RoomIndex];
	for (const TCHAR* Alias : Aliases)
	{
		const int32* SeriesIndex = Cache.SeriesLookup.Find(FName(Alias));
		if (!SeriesIndex
			|| !Cache.SeriesValues.IsValidIndex(*SeriesIndex)
			|| !Cache.SeriesUnits.IsValidIndex(*SeriesIndex))
		{
			continue;
		}

		OutValue = Cache.SeriesValues[*SeriesIndex];
		OutUnit = &Cache.SeriesUnits[*SeriesIndex];
		return true;
	}

	return false;
}

bool UBRiskEgressSubsystem::TryGetConcentrationPpm(
	const int32 RoomIndex,
	const std::initializer_list<const TCHAR*> Aliases,
	const float MolecularWeight,
	float& OutPpm) const
{
	float RawValue = 0.0f;
	const FString* Unit = nullptr;
	if (!TryGetRawSeries(RoomIndex, Aliases, RawValue, Unit) || !Unit)
	{
		return false;
	}

	if (UnitContains(*Unit, TEXT("ppm")))
	{
		OutPpm = RawValue;
		return true;
	}
	if (UnitContains(*Unit, TEXT("%")))
	{
		OutPpm = RawValue * 10000.0f;
		return true;
	}
	if (UnitContains(*Unit, TEXT("mol/mol")) || UnitContains(*Unit, TEXT("mole fraction")))
	{
		OutPpm = RawValue * 1000000.0f;
		return true;
	}
	if (UnitContains(*Unit, TEXT("kg/kg")) || UnitContains(*Unit, TEXT("mass fraction")))
	{
		OutPpm = RawValue * (AirMolecularWeight / MolecularWeight) * 1000000.0f;
		return true;
	}

	return false;
}

bool UBRiskEgressSubsystem::TryGetConcentrationPercent(
	const int32 RoomIndex,
	const std::initializer_list<const TCHAR*> Aliases,
	const float MolecularWeight,
	float& OutPercent) const
{
	float Ppm = 0.0f;
	if (!TryGetConcentrationPpm(RoomIndex, Aliases, MolecularWeight, Ppm))
	{
		return false;
	}

	OutPercent = Ppm / 10000.0f;
	return true;
}
