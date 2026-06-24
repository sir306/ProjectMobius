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

	// Track A: B-Risk calculated room values (monitor-height, cumulative FED).
	OutSample.CalcFEDSum = RoomState->CalcFEDSum;
	OutSample.CalcFEDRadSum = RoomState->CalcFEDRadSum;
	OutSample.CalcVisibilityM = RoomState->CalcVisibilityM;
	OutSample.CalcHeatReleaseKW = RoomState->CalcHeatReleaseKW;
	OutSample.CalcLayerHeightM = RoomState->CalcLayerHeightM;
	OutSample.CalcUpperTemperatureC = RoomState->CalcUpperTemperatureC;
	OutSample.CalcLowerTemperatureC = RoomState->CalcLowerTemperatureC;
	OutSample.bHasCalcFEDSum = RoomState->bHasCalcFEDSum;
	OutSample.bHasCalcFEDRadSum = RoomState->bHasCalcFEDRadSum;
	OutSample.bHasCalcVisibility = RoomState->bHasCalcVisibility;
	OutSample.bHasCalcLayerHeight = RoomState->bHasCalcLayerHeight;
	OutSample.bHasCalcTemperature = RoomState->bHasCalcTemperature;

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

	// When rooms overlap (e.g. a corridor modelled inside a larger B-Risk zone),
	// the smallest enclosing volume is the most specific spatial match for the agent.
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
	const FAgentEgressTenabilityFragment& Health)
{
	if (AgentId < 0 || TimeSeconds < 0.0f)
	{
		return;
	}

	TArray<FAgentEgressHealthHistorySample>& History = AgentHealthHistory.FindOrAdd(AgentId);
	FAgentEgressHealthHistorySample NewSample;
	NewSample.TimeSeconds = TimeSeconds;
	NewSample.DisplayRisk = Health.DisplayRisk;
	NewSample.ShownCriterion = static_cast<uint8>(
		Health.bTenabilityFailed ? Health.FirstFailureCriterion : Health.CurrentDominantCriterion);

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

	// Collapse collinear samples: if the new risk rate equals the previous interval's
	// rate AND the shown criterion is unchanged, overwrite the tail rather than
	// appending. Keeps the rewind history compact while preserving criterion changes.
	if (History.Num() >= 2)
	{
		const FAgentEgressHealthHistorySample& PreviousSample = History[History.Num() - 2];
		const FAgentEgressHealthHistorySample& LastSample = History.Last();
		const float PreviousDuration = LastSample.TimeSeconds - PreviousSample.TimeSeconds;
		const float NewDuration = NewSample.TimeSeconds - LastSample.TimeSeconds;
		if (PreviousDuration > UE_KINDA_SMALL_NUMBER && NewDuration > UE_KINDA_SMALL_NUMBER
			&& LastSample.ShownCriterion == NewSample.ShownCriterion
			&& PreviousSample.ShownCriterion == LastSample.ShownCriterion)
		{
			const float PreviousRiskRate =
				(LastSample.DisplayRisk - PreviousSample.DisplayRisk) / PreviousDuration;
			const float NewRiskRate =
				(NewSample.DisplayRisk - LastSample.DisplayRisk) / NewDuration;
			if (FMath::IsNearlyEqual(PreviousRiskRate, NewRiskRate, 0.0001f))
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
	FAgentEgressTenabilityFragment& InOutHealth) const
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

	// Restore the bar's display risk by interpolation; the shown criterion is a
	// discrete label, so snap it to the bracket the scrub time falls in.
	InOutHealth.DisplayRisk = FMath::Lerp(LowerSample->DisplayRisk, UpperSample->DisplayRisk, Alpha);
	InOutHealth.CurrentDominantCriterion =
		static_cast<ETenabilityCriterion>(Alpha < 0.5f ? LowerSample->ShownCriterion : UpperSample->ShownCriterion);
	InOutHealth.Health = 1.0f - FMath::Clamp(InOutHealth.DisplayRisk, 0.0f, 1.0f);
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
		// Default the smoke layer to the ceiling until zone data overrides it in ResolveTypedRoomState.
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
		ApplyTenabilityCalcToRoom(RoomIndex);
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
	RoomState.bHasCalcFEDSum = false;
	RoomState.bHasCalcFEDRadSum = false;
	RoomState.bHasCalcVisibility = false;
	RoomState.bHasCalcLayerHeight = false;
	RoomState.bHasCalcTemperature = false;
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

FBRiskTenabilitySample UBRiskEgressSubsystem::SampleTenabilityTableAtTime(
	const FBRiskTenabilityRoomTable& Table,
	const double TimeSeconds)
{
	if (Table.Samples.Num() == 0)
	{
		return FBRiskTenabilitySample();
	}

	if (Table.Samples.Num() == 1 || TimeSeconds <= Table.Samples[0].SampleTimeSeconds)
	{
		return Table.Samples[0];
	}

	const FBRiskTenabilitySample& LastSample = Table.Samples.Last();
	if (TimeSeconds >= LastSample.SampleTimeSeconds)
	{
		return LastSample;
	}

	// Binary search for the bracketing pair, then linear-interpolate every channel.
	int32 LowerIndex = 0;
	int32 UpperIndex = Table.Samples.Num() - 1;
	while (UpperIndex - LowerIndex > 1)
	{
		const int32 MidIndex = (LowerIndex + UpperIndex) / 2;
		if (Table.Samples[MidIndex].SampleTimeSeconds <= TimeSeconds)
		{
			LowerIndex = MidIndex;
		}
		else
		{
			UpperIndex = MidIndex;
		}
	}

	const FBRiskTenabilitySample& Lo = Table.Samples[LowerIndex];
	const FBRiskTenabilitySample& Hi = Table.Samples[UpperIndex];
	const double Duration = Hi.SampleTimeSeconds - Lo.SampleTimeSeconds;
	const double Alpha = Duration > UE_DOUBLE_KINDA_SMALL_NUMBER
		? FMath::Clamp((TimeSeconds - Lo.SampleTimeSeconds) / Duration, 0.0, 1.0)
		: 0.0;

	FBRiskTenabilitySample Result;
	Result.SampleTimeSeconds = TimeSeconds;
	Result.HeatReleaseKW = FMath::Lerp(Lo.HeatReleaseKW, Hi.HeatReleaseKW, Alpha);
	Result.LayerHeightM = FMath::Lerp(Lo.LayerHeightM, Hi.LayerHeightM, Alpha);
	Result.UpperTemperatureC = FMath::Lerp(Lo.UpperTemperatureC, Hi.UpperTemperatureC, Alpha);
	Result.LowerTemperatureC = FMath::Lerp(Lo.LowerTemperatureC, Hi.LowerTemperatureC, Alpha);
	Result.VisibilityM = FMath::Lerp(Lo.VisibilityM, Hi.VisibilityM, Alpha);
	Result.FEDSum = FMath::Lerp(Lo.FEDSum, Hi.FEDSum, Alpha);
	Result.FEDRadSum = FMath::Lerp(Lo.FEDRadSum, Hi.FEDRadSum, Alpha);

	// Availability is the AND of both endpoints: a channel is usable for
	// interpolation only when present in both bracketing samples.
	Result.bHasHeatRelease = Lo.bHasHeatRelease && Hi.bHasHeatRelease;
	Result.bHasLayerHeight = Lo.bHasLayerHeight && Hi.bHasLayerHeight;
	Result.bHasUpperTemperature = Lo.bHasUpperTemperature && Hi.bHasUpperTemperature;
	Result.bHasLowerTemperature = Lo.bHasLowerTemperature && Hi.bHasLowerTemperature;
	Result.bHasVisibility = Lo.bHasVisibility && Hi.bHasVisibility;
	Result.bHasFEDSum = Lo.bHasFEDSum && Hi.bHasFEDSum;
	Result.bHasFEDRadSum = Lo.bHasFEDRadSum && Hi.bHasFEDRadSum;
	return Result;
}

void UBRiskEgressSubsystem::ApplyTenabilityCalcToRoom(const int32 RoomIndex)
{
	if (!RoomStates.IsValidIndex(RoomIndex) || !BRiskDataSubsystem)
	{
		return;
	}

	FBRiskEgressRoomState& RoomState = RoomStates[RoomIndex];
	if (!BRiskDataSubsystem->HasTenabilityData())
	{
		return;
	}

	// Track A is keyed by B-Risk room id, independent of room array order.
	const TArray<FBRiskTenabilityRoomTable>& Tables = BRiskDataSubsystem->GetTenabilityTables();
	const FBRiskTenabilityRoomTable* Table = Tables.FindByPredicate(
		[&RoomState](const FBRiskTenabilityRoomTable& Candidate)
		{
			return Candidate.RoomId == RoomState.RoomId;
		});
	if (!Table)
	{
		return;
	}

	const FBRiskTenabilitySample Sample = SampleTenabilityTableAtTime(*Table, SampleTimeSeconds);
	RoomState.CalcFEDSum = static_cast<float>(Sample.FEDSum);
	RoomState.CalcFEDRadSum = static_cast<float>(Sample.FEDRadSum);
	RoomState.CalcVisibilityM = static_cast<float>(Sample.VisibilityM);
	RoomState.CalcHeatReleaseKW = static_cast<float>(Sample.HeatReleaseKW);
	RoomState.CalcLayerHeightM = static_cast<float>(Sample.LayerHeightM);
	RoomState.CalcUpperTemperatureC = static_cast<float>(Sample.UpperTemperatureC);
	RoomState.CalcLowerTemperatureC = static_cast<float>(Sample.LowerTemperatureC);
	RoomState.bHasCalcFEDSum = Sample.bHasFEDSum;
	RoomState.bHasCalcFEDRadSum = Sample.bHasFEDRadSum;
	RoomState.bHasCalcVisibility = Sample.bHasVisibility;
	RoomState.bHasCalcLayerHeight = Sample.bHasLayerHeight;
	RoomState.bHasCalcTemperature = Sample.bHasUpperTemperature || Sample.bHasLowerTemperature;
}

FTenabilityAnalysisSettings UBRiskEgressSubsystem::BuildTenabilitySettingsFromEndpoints() const
{
	FTenabilityAnalysisSettings Settings;
	if (!BRiskDataSubsystem)
	{
		return Settings;
	}

	const FBRiskTenabilityEndpoints& Endpoints = BRiskDataSubsystem->GetTenabilityEndpoints();

	if (Endpoints.bHasMonitorHeight)
	{
		Settings.MonitorHeightM = static_cast<float>(Endpoints.MonitorHeightM);
	}
	else
	{
		UE_LOG(LogBRiskEgressSubsystem, Warning,
			TEXT("B-Risk input has no monitor_height; using default %.2f m."), Settings.MonitorHeightM);
	}

	if (Endpoints.bHasEndpointVisibility)
	{
		Settings.EndpointVisibilityM = static_cast<float>(Endpoints.EndpointVisibilityM);
	}
	else
	{
		UE_LOG(LogBRiskEgressSubsystem, Warning,
			TEXT("B-Risk input has no endpoint_visibility; using default %.2f m."), Settings.EndpointVisibilityM);
	}

	if (Endpoints.bHasEndpointFED)
	{
		Settings.EndpointToxicFED = static_cast<float>(Endpoints.EndpointFED);
	}
	else
	{
		UE_LOG(LogBRiskEgressSubsystem, Warning,
			TEXT("B-Risk input has no endpoint_FED; using default %.2f."), Settings.EndpointToxicFED);
	}

	// endpoint_radiation is B-Risk's radiant/thermal FED endpoint.
	if (Endpoints.bHasEndpointRadiation)
	{
		Settings.EndpointThermalFED = static_cast<float>(Endpoints.EndpointRadiation);
	}
	else
	{
		UE_LOG(LogBRiskEgressSubsystem, Warning,
			TEXT("B-Risk input has no endpoint_radiation; using default thermal FED endpoint %.2f."),
			Settings.EndpointThermalFED);
	}

	// endpoint_temp is NOT a Celsius layer-temperature threshold (observed O(1000)).
	// Do not wire it into a Celsius criterion. Leave the temperature criterion disabled.
	if (Endpoints.bHasEndpointTemp)
	{
		UE_LOG(LogBRiskEgressSubsystem, Verbose,
			TEXT("B-Risk endpoint_temp=%.1f present but not mapped to a Celsius criterion."),
			Endpoints.EndpointTempRaw);
	}

	return Settings;
}

FName UBRiskEgressSubsystem::NormalizeSeriesName(const FString& SeriesName)
{
	FString Normalized = SeriesName;
	Normalized.TrimStartAndEndInline();
	Normalized.ToUpperInline();
	Normalized.ReplaceInline(TEXT(" "), TEXT(""));
	Normalized.ReplaceInline(TEXT("-"), TEXT("_"));

	// B-Risk exports zone series with a trailing room index (e.g. "UCO_1", "ULOD_2").
	// Strip it so the aliases in TryGetRawSeries match regardless of room count.
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
		// Mass fraction → ppm: multiply by (air molar mass / species molar mass) × 1e6.
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
