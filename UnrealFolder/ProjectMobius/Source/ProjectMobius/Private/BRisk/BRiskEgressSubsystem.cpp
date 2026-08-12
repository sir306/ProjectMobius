// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "BRisk/BRiskEgressSubsystem.h"

#include "Async/Async.h"                                   // Async / AsyncTask (background build + game-thread apply)
#include "BRisk/AgentTenabilityTimeline.h"
#include "BRisk/BRiskDataSubsystem.h"
#include "BRiskDataImporter.h"
#include "IMobiusErrorReporter.h"                          // user-facing report for defaulted tenability endpoints
#include "MassAI/SubSystems/MassEntitySpawnSubsystem.h"    // GetSimulationFragment / GetAgentTimeBetweenSteps
#include "SimData/ISimSampleProvider.h"                    // ISimSampleProvider::ForEachTimestep (build source)
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

	// Cancel any in-flight timeline build so the worker stops early. The job captures everything it
	// needs by value/shared-ptr, so it is self-contained if it does run to completion; the game-thread
	// apply is guarded by a weak pointer to this subsystem and no-ops once we are gone.
	if (AgentTimelineBuildCancel.IsValid())
	{
		*AgentTimelineBuildCancel = true;
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
	// Room containment is single-sourced through ResolveRoomIndexAtLocation so the
	// offline timeline builder and this live sampler always agree on which room a
	// location falls in (scientific-integrity invariant 4). RoomVolumes is built
	// parallel to RoomStates, so the returned index maps straight back.
	const int32 RoomIndex = UE::Mobius::Tenability::ResolveRoomIndexAtLocation(
		RoomVolumes, WorldLocation, PreferredRoomIndex);

	return RoomStates.IsValidIndex(RoomIndex) ? &RoomStates[RoomIndex] : nullptr;
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
	// Immediate teardown of any built/in-flight timelines (invalidation matrix: B-Risk cleared ->
	// cancel in-flight build, drop timelines, tenability inert). ClearCachedData also bumps
	// ScenarioGeneration so a straggler apply that re-derives the key would discard anyway, but we do
	// not rely on that alone: cancel the worker so it stops reading the (about-to-be-freed) provider,
	// drop the set, and reset BuiltKey to the all-zero sentinel that never matches a real key.
	if (AgentTimelineBuildCancel.IsValid())
	{
		*AgentTimelineBuildCancel = true;
	}
	bAgentTimelineBuildInFlight = false;
	InFlightKey = UE::Mobius::Tenability::FAgentTimelineKey();
	AgentTimelines = UE::Mobius::Tenability::FAgentTimelineSet(); // clears map + resets BuiltKey to sentinel

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
	RoomVolumes.SetNum(Rooms.Num());
	RoomSeriesCaches.SetNum(Rooms.Num());

	for (int32 RoomIndex = 0; RoomIndex < Rooms.Num(); ++RoomIndex)
	{
		const FBRiskRoomGeometry& Room = Rooms[RoomIndex];
		FBRiskEgressRoomState& RoomState = RoomStates[RoomIndex];
		RoomState.RoomIndex = RoomIndex;
		RoomState.RoomId = Room.RoomId;
		// Must use the exact same conversion as the smoke visualizer (BRiskCoord::MakeRoomFootprint)
		// so agent world positions map to the correct room (tenability lookup).
		//
		// The footprint is consumed WHOLE: WorldBounds takes its bounding box (the Z slab and the
		// cheap XY reject), and RoomVolumes keeps its polygon so containment can exclude the parts
		// of that box the room does not occupy. Both are filled here, in one loop, from one
		// MakeRoomFootprint call — RoomVolumes is indexed to subscript RoomStates, so building them
		// together is what makes the two arrays impossible to desync.
		const BRiskCoord::FRoomFootprintCm Footprint =
			BRiskCoord::MakeRoomFootprint(Room, Scale, BRiskDataSubsystem->GetRoomFrame());
		RoomState.WorldBounds = Footprint.Bounds;

		RoomVolumes[RoomIndex] =
			UE::Mobius::Tenability::MakeRoomVolume(Footprint.Bounds, Footprint.Polygon);
		// Default the smoke layer to the ceiling until zone data overrides it in ResolveTypedRoomState.
		RoomState.LayerHeightWorldCm = RoomState.WorldBounds.Max.Z;

		// B-Risk packs every room into a single zone table, one column per channel
		// suffixed with the room id ("ULOD_1", "ULOD_2", ...). Collect just this
		// room's columns from across all zone tables, keyed by their suffix-stripped
		// name so TryGetRawSeries' aliases match regardless of room count.
		FRoomSeriesCache& SeriesCache = RoomSeriesCaches[RoomIndex];
		for (int32 TableIndex = 0; TableIndex < ZoneTables.Num(); ++TableIndex)
		{
			const FBRiskZoneTable& ZoneTable = ZoneTables[TableIndex];
			for (int32 SeriesIndex = 0; SeriesIndex < ZoneTable.Series.Num(); ++SeriesIndex)
			{
				const FBRiskSeries& Series = ZoneTable.Series[SeriesIndex];
				const int32 Suffix = ExtractRoomIdSuffix(Series.Name);
				// Take columns whose suffix matches this room. A suffix-less column is
				// only attributed to a single-room scenario as a defensive fallback.
				const bool bMatchesRoom = (Suffix == Room.RoomId)
					|| (Suffix == INDEX_NONE && Rooms.Num() == 1);
				if (!bMatchesRoom)
				{
					continue;
				}

				const FName NormalizedName = NormalizeSeriesName(Series.Name);
				const int32 CacheIndex = SeriesCache.SeriesNames.Add(NormalizedName);
				SeriesCache.SeriesUnits.Add(Series.Unit);
				SeriesCache.SeriesValues.Add(0.0f);
				SeriesCache.SourceTableIndex.Add(TableIndex);
				SeriesCache.SourceSeriesIndex.Add(SeriesIndex);
				SeriesCache.SeriesLookup.FindOrAdd(NormalizedName, CacheIndex);
			}
		}
	}

	// ONE place, ONCE per scenario load, is the only safe home for this: the endpoints it inspects are
	// read by BuildTenabilitySettingsFromEndpoints, which the timeline-currency poll calls per agent per
	// frame. Reporting from there would emit a formatted warning per missing endpoint per agent per frame
	// (~1.5M lines/s for a 5k crowd at 60 Hz) - a stall and a disk filler, not a diagnostic. Both callers
	// of RebuildRoomCache reach here, and it runs after the early-out above, so it only ever describes a
	// scenario that actually loaded.
	ReportTenabilityEndpointFallbacks();

	++Revision;
}

void UBRiskEgressSubsystem::ReportTenabilityEndpointFallbacks() const
{
	if (!BRiskDataSubsystem)
	{
		return;
	}

	const FBRiskTenabilityEndpoints& Endpoints = BRiskDataSubsystem->GetTenabilityEndpoints();
	const FTenabilityAnalysisSettings Defaults; // the documented fallbacks, before any override

	// Each entry: the input1.xml tag the user would have to add, and the default standing in for it.
	TArray<FString> Substituted;
	if (!Endpoints.bHasMonitorHeight)
	{
		Substituted.Add(FString::Printf(TEXT("monitor_height -> %.2f m"), Defaults.MonitorHeightM));
	}
	if (!Endpoints.bHasEndpointVisibility)
	{
		Substituted.Add(FString::Printf(TEXT("endpoint_visibility -> %.2f m"), Defaults.EndpointVisibilityM));
	}
	if (!Endpoints.bHasEndpointFED)
	{
		Substituted.Add(FString::Printf(TEXT("endpoint_FED -> %.2f"), Defaults.EndpointToxicFED));
	}
	if (!Endpoints.bHasEndpointRadiation)
	{
		Substituted.Add(FString::Printf(TEXT("endpoint_radiation -> %.2f (thermal FED)"), Defaults.EndpointThermalFED));
	}

	// endpoint_temp is NOT a Celsius layer-temperature threshold (observed O(1000) - it is raw Kelvin in
	// real B-Risk input). Deliberately not mapped to a Celsius criterion, so its presence is a Verbose
	// note rather than a substitution: nothing was defaulted.
	if (Endpoints.bHasEndpointTemp)
	{
		UE_LOG(LogBRiskEgressSubsystem, Verbose,
			TEXT("B-Risk endpoint_temp=%.1f present but not mapped to a Celsius criterion."),
			Endpoints.EndpointTempRaw);
	}

	if (Substituted.IsEmpty())
	{
		return;
	}

	const FString Joined = FString::Join(Substituted, TEXT("; "));
	UE_LOG(LogBRiskEgressSubsystem, Warning,
		TEXT("B-Risk input supplied no %s. Mobius substituted its documented defaults: %s."),
		Substituted.Num() == 1 ? TEXT("tenability endpoint") : TEXT("tenability endpoints"), *Joined);

	// Surface it to the user as well as the log. Tenability results computed against a substituted
	// endpoint are still valid analysis, but they are NOT the scenario's own criteria - and that is
	// exactly the thing someone comparing Mobius against a B-Risk report needs to know before they
	// conclude the numbers disagree.
	//
	// Severity Warning with NO prompt, deliberately: a missing endpoint is legitimate input that Mobius
	// handles by documented substitution, so a modal on every load would train the user to dismiss it.
	// This is the record they read WHEN they hit a discrepancy, not an interruption.
	if (IMobiusErrorReporter* Reporter = IMobiusErrorReporter::Get(this))
	{
		Reporter->ReportError(
			NSLOCTEXT("MobiusBRisk", "TenabilityEndpointsTitleBar", "B-Risk tenability"),
			NSLOCTEXT("MobiusBRisk", "TenabilityEndpointsTitle", "Tenability endpoints defaulted"),
			FText::Format(
				NSLOCTEXT("MobiusBRisk", "TenabilityEndpointsBody",
					"This scenario's input1.xml did not supply every tenability endpoint. Mobius used its "
					"documented defaults instead, so failure times are computed against these rather than "
					"the scenario's own criteria:\n\n{0}"),
				FText::FromString(Joined)),
			NSLOCTEXT("MobiusBRisk", "TenabilityEndpointsLocation", "B-Risk input1.xml <tenability>"),
			EMobiusErrorSeverity::Warning,
			/*bShowPrompt*/ false);
	}
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
		FRoomSeriesCache& SeriesCache = RoomSeriesCaches[RoomIndex];

		for (int32 CacheIndex = 0; CacheIndex < SeriesCache.SeriesValues.Num(); ++CacheIndex)
		{
			const int32 TableIndex = SeriesCache.SourceTableIndex[CacheIndex];
			const int32 SeriesIndex = SeriesCache.SourceSeriesIndex[CacheIndex];
			if (!ZoneTables.IsValidIndex(TableIndex)
				|| !ZoneTables[TableIndex].Series.IsValidIndex(SeriesIndex))
			{
				continue;
			}

			const FBRiskZoneTable& ZoneTable = ZoneTables[TableIndex];
			float SampledValue = 0.0f;
			if (SampleAlignedSeries(
				ZoneTable.TimeSeconds,
				ZoneTable.Series[SeriesIndex].Values,
				NewSimulationTime,
				SampledValue))
			{
				SeriesCache.SeriesValues[CacheIndex] = SampledValue;
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
	RoomVolumes.Reset();
	RoomSeriesCaches.Reset();
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

void UBRiskEgressSubsystem::SampleTenabilityDoseAtRoomIndex(
	const int32 RoomIndex,
	const double TimeSeconds,
	double& OutToxicFED,
	double& OutThermalFED) const
{
	OutToxicFED = 0.0;
	OutThermalFED = 0.0;

	if (!BRiskDataSubsystem || !RoomStates.IsValidIndex(RoomIndex))
	{
		return;
	}

	// Same RoomIndex -> RoomId -> table lookup as ApplyTenabilityCalcToRoom, so the live processor's
	// DoseAt query and the offline timeline build agree on which curve backs a given room index.
	const int32 RoomId = RoomStates[RoomIndex].RoomId;
	const TArray<FBRiskTenabilityRoomTable>& Tables = BRiskDataSubsystem->GetTenabilityTables();
	const FBRiskTenabilityRoomTable* Table = Tables.FindByPredicate(
		[RoomId](const FBRiskTenabilityRoomTable& Candidate) { return Candidate.RoomId == RoomId; });
	if (!Table)
	{
		return; // no curve for this room -> no dose contribution (never fabricate)
	}

	const FBRiskTenabilitySample Sample = SampleTenabilityTableAtTime(*Table, TimeSeconds);
	OutToxicFED = Sample.FEDSum;
	OutThermalFED = Sample.FEDRadSum;
}

// ---------------------------------------------------------------------------------------------------
// Agent-timeline build orchestration + file-change invalidation (Task 2)
// ---------------------------------------------------------------------------------------------------

uint32 UBRiskEgressSubsystem::HashTenabilitySettings(const FTenabilityAnalysisSettings& Settings)
{
	// Field-by-field HashCombine — NOT a memcpy hash: FTenabilityAnalysisSettings has bool members and
	// float members, so the struct carries indeterminate padding bytes that would make a byte hash
	// non-deterministic across builds/instances. Fold each field explicitly. Floats are folded via
	// their exact bit pattern (GetTypeHash(float) already does this) so two settings with bitwise-equal
	// endpoints hash identically and a genuine endpoint change (any bit) changes the hash.
	uint32 Hash = 0;
	Hash = HashCombine(Hash, GetTypeHash(Settings.MonitorHeightM));
	Hash = HashCombine(Hash, GetTypeHash(Settings.EndpointVisibilityM));
	Hash = HashCombine(Hash, GetTypeHash(Settings.ReferenceVisibilityM));
	Hash = HashCombine(Hash, GetTypeHash(Settings.EndpointToxicFED));
	Hash = HashCombine(Hash, GetTypeHash(Settings.EndpointThermalFED));
	Hash = HashCombine(Hash, GetTypeHash(Settings.EndpointTemperatureC));
	// Fold the criterion-enable bools as one packed byte via an explicit uint32 cast (no reliance on a
	// GetTypeHash(bool) overload). Order is fixed, so any toggle flips a distinct bit -> a distinct hash.
	uint32 Flags = 0;
	Flags |= (Settings.bUseVisibilityCriterion  ? 1u : 0u) << 0;
	Flags |= (Settings.bUseToxicFEDCriterion     ? 1u : 0u) << 1;
	Flags |= (Settings.bUseThermalFEDCriterion   ? 1u : 0u) << 2;
	Flags |= (Settings.bUseTemperatureCriterion  ? 1u : 0u) << 3;
	Flags |= (Settings.bUseLayerHeightCriterion  ? 1u : 0u) << 4;
	Hash = HashCombine(Hash, GetTypeHash(Flags));
	return Hash;
}

UE::Mobius::Tenability::FAgentTimelineKey UBRiskEgressSubsystem::MakeCurrentTimelineKey() const
{
	using namespace UE::Mobius::Tenability;

	// Both datasets must be present for a real key. Either absent -> all-zero sentinel: real agent
	// generations start at 1 (SimDataGenerationCounter is pre-incremented) and ClearCachedData bumps
	// ScenarioGeneration, so a sentinel can never accidentally equal a genuine built key.
	const UWorld* World = GetWorld();
	const UMassEntitySpawnSubsystem* SpawnSubsystem =
		World ? World->GetSubsystem<UMassEntitySpawnSubsystem>() : nullptr;
	const FSimulationFragment* SimFragment =
		SpawnSubsystem ? SpawnSubsystem->GetSimulationFragment() : nullptr;

	const bool bAgentDataPresent =
		SimFragment && SimFragment->Provider.IsValid() && SimFragment->Provider->IsValidAndPopulated();
	const bool bScenarioDataPresent =
		BRiskDataSubsystem && BRiskDataSubsystem->HasTenabilityData() && !RoomStates.IsEmpty();

	if (!bAgentDataPresent || !bScenarioDataPresent)
	{
		return FAgentTimelineKey(); // sentinel
	}

	FAgentTimelineKey Key;
	Key.AgentDataGeneration = SimFragment->DataGeneration;
	Key.ScenarioGeneration = ScenarioGeneration;
	Key.SettingsHash = HashTenabilitySettings(BuildTenabilitySettingsFromEndpoints());
	return Key;
}

bool UBRiskEgressSubsystem::AreAgentTimelinesCurrent() const
{
	const UE::Mobius::Tenability::FAgentTimelineKey Current = MakeCurrentTimelineKey();
	// A sentinel current key (dataset absent) is never "current": nothing valid to show.
	if (Current == UE::Mobius::Tenability::FAgentTimelineKey())
	{
		return false;
	}
	return AgentTimelines.BuiltKey == Current;
}

const UE::Mobius::Tenability::FAgentTenabilityTimeline*
UBRiskEgressSubsystem::FindCurrentAgentTimeline(const int32 EntityID) const
{
	// Only ever hand out timelines from a set whose key matches the live triple. Stale, building or
	// absent -> nullptr, so the caller renders the no-data state and never a mismatched number.
	if (!AreAgentTimelinesCurrent())
	{
		return nullptr;
	}
	return AgentTimelines.Timelines.Find(EntityID);
}

void UBRiskEgressSubsystem::RequestAgentTimelineRebuild(const UE::Mobius::Tenability::FAgentTimelineKey& Key)
{
	using namespace UE::Mobius::Tenability;

	// Debounce 1: never build for the sentinel key (a dataset is absent — nothing to build).
	if (Key == FAgentTimelineKey())
	{
		return;
	}
	// Debounce 2: the built set already matches this key — nothing to do.
	if (AgentTimelines.BuiltKey == Key)
	{
		return;
	}
	// Debounce 3: a build for an equal key is already in flight. A build for a DIFFERENT key
	// (a newer request) is allowed to supersede: it cancels the older job and dispatches afresh.
	if (bAgentTimelineBuildInFlight && InFlightKey == Key)
	{
		return;
	}

	// Snapshot everything the build needs by value / shared-immutable so the worker touches no
	// subsystem state (integrity: no UObject access off the game thread).
	const UWorld* World = GetWorld();
	const UMassEntitySpawnSubsystem* SpawnSubsystem =
		World ? World->GetSubsystem<UMassEntitySpawnSubsystem>() : nullptr;
	const FSimulationFragment* SimFragment =
		SpawnSubsystem ? SpawnSubsystem->GetSimulationFragment() : nullptr;
	if (!SimFragment || !SimFragment->Provider.IsValid() || !SimFragment->Provider->IsValidAndPopulated())
	{
		return; // agent data gone since the caller derived the key — next-frame poll re-requests
	}
	if (!BRiskDataSubsystem || !BRiskDataSubsystem->HasTenabilityData())
	{
		return;
	}

	// TSharedPtr copy keeps the sample allocation alive for the job's lifetime regardless of a file
	// swap on the game thread (which builds a NEW fragment/provider, never mutates this one).
	TSharedPtr<ISimSampleProvider> Provider = SimFragment->Provider;

	// Room geometry + ids parallel to RoomStates (same order the live sampler resolves against),
	// copied so the worker reads an immutable snapshot. Copying RoomVolumes rather than just the
	// bounding boxes is what keeps the offline builder resolving rooms identically to the live
	// sampler once footprints are involved (invariant 4) — bounds alone would silently give the
	// precomputed timelines the old over-claiming bbox rule.
	TArray<UE::Mobius::Tenability::FRoomVolume> RoomGeometry = RoomVolumes;
	TArray<int32> RoomIds;
	RoomIds.Reserve(RoomStates.Num());
	for (const FBRiskEgressRoomState& RoomState : RoomStates)
	{
		RoomIds.Add(RoomState.RoomId);
	}

	// Value copy of the tenability tables (rooms x samples — small), so the FED sampler reads a
	// private, immutable snapshot instead of live subsystem data. Indexed parallel to RoomGeometry/Ids
	// via RoomId so the builder's RoomIndex (into RoomGeometry) maps to the right table. Non-const so it
	// can be MoveTemp'd into the async capture below (MoveTemp static-asserts against a const source).
	TArray<FBRiskTenabilityRoomTable> Tables = BRiskDataSubsystem->GetTenabilityTables();

	// Timestep index -> seconds. The provider's ForEachTimestep yields the agent-grid index; multiply
	// by the agent sample interval to recover seconds (the same clock the live sampler uses).
	const float TimeBetweenSteps = SpawnSubsystem->GetAgentTimeBetweenSteps();

		// Analysis-settings snapshot for Layer 2 (failure precompute). Captured by value so the worker
		// evaluates the SAME endpoints/criteria that produced Key.SettingsHash: a settings change bumps
		// the hash, mismatches the built key, and the apply discards (then the next-frame poll rebuilds).
		const FTenabilityAnalysisSettings SettingsSnapshot = BuildTenabilitySettingsFromEndpoints();

	// Fresh cancel flag for this dispatch; supersede any older in-flight job.
	if (AgentTimelineBuildCancel.IsValid())
	{
		*AgentTimelineBuildCancel = true; // tell the previous worker to stop
	}
	TSharedRef<FThreadSafeBool> Cancel = MakeShared<FThreadSafeBool>(false);
	AgentTimelineBuildCancel = Cancel;
	InFlightKey = Key;
	bAgentTimelineBuildInFlight = true;
	const uint32 Serial = ++AgentTimelineBuildSerial;

	TWeakObjectPtr<UBRiskEgressSubsystem> WeakThis(this);

	Async(EAsyncExecution::ThreadPool,
		[WeakThis, Serial, Cancel, Provider, RoomGeometry = MoveTemp(RoomGeometry), RoomIds = MoveTemp(RoomIds),
		 Tables = MoveTemp(Tables), Key, TimeBetweenSteps, SettingsSnapshot]() mutable
	{
		FAgentTimelineSet BuiltSet;

		// The build reads *Cancel before starting the pass and again per-timestep, so a cancel that
		// arrives before or during the sweep short-circuits the work and yields an empty set (which the
		// apply then discards on the cancel check).
		if (!*Cancel && Provider.IsValid() && Provider->IsValidAndPopulated())
		{
			// The FED sampler backs the builder with the SAME curve evaluation the live room state uses
			// (SampleTenabilityTableAtTime) so offline dose and live display agree (integrity invariant 4).
			// RoomIndex indexes RoomGeometry/RoomIds; map it to the matching table via RoomId. Captures
			// Tables + a private copy of the RoomIndex->RoomId map BY VALUE: RoomIds is MoveTemp'd into
			// the builder below, so a by-reference capture would dangle. Tables is captured by value into
			// the sampler's own copy (owned by the enclosing async body) so it outlives every AddTimestep.
			TArray<int32> SamplerRoomIds = RoomIds; // private copy for the sampler
			auto FEDSampler = [Tables, SamplerRoomIds = MoveTemp(SamplerRoomIds)]
				(int32 RoomIndex, double TimeSeconds, double& OutToxic, double& OutThermal)
			{
				OutToxic = 0.0;
				OutThermal = 0.0;
				if (!SamplerRoomIds.IsValidIndex(RoomIndex))
				{
					return;
				}
				const int32 RoomId = SamplerRoomIds[RoomIndex];
				const FBRiskTenabilityRoomTable* Table = Tables.FindByPredicate(
					[RoomId](const FBRiskTenabilityRoomTable& Candidate) { return Candidate.RoomId == RoomId; });
				if (!Table)
				{
					return;
				}
				const FBRiskTenabilitySample Sample = SampleTenabilityTableAtTime(*Table, TimeSeconds);
				OutToxic = Sample.FEDSum;
				OutThermal = Sample.FEDRadSum;
			};

			// RoomIndex -> table-index map, parallel to RoomGeometry/RoomIds, resolved via RoomId. Layer 2
			// walks each interval's RoomIndex through this to reach the room's tenability curve. Built from
			// a private RoomIds copy because RoomIds is MoveTemp'd into the builder just below.
			TArray<int32> RoomIndexToTableIndex;
			RoomIndexToTableIndex.Reserve(RoomIds.Num());
			for (const int32 MapRoomId : RoomIds)
			{
				RoomIndexToTableIndex.Add(Tables.IndexOfByPredicate(
					[MapRoomId](const FBRiskTenabilityRoomTable& Candidate) { return Candidate.RoomId == MapRoomId; }));
			}

			// Per-agent pose track recorded DURING the same guaranteed-complete pass. The builder keeps only
			// a single last-seen pose per open interval (discarded on Finish()), so the failure pose at an
			// arbitrary crossing time needs its own record. Recording it here keeps pose resolution off the
			// game thread and uses the exact trajectory samples the intervals were built from (no second read).
			struct FPoseKey { float TimeSeconds; FVector Location; FRotator Rotation; };
			TMap<int32, TArray<FPoseKey>> PoseTracks;

			FAgentTimelineSetBuilder Builder(MoveTemp(RoomGeometry), MoveTemp(RoomIds), MoveTemp(FEDSampler));

			// Guaranteed-complete ascending pass (Invariant 5). Both providers implement ForEachTimestep
			// with their own storage/reader and touch no UObject state, so this is safe off the game
			// thread (FFullyResidentProvider iterates a shared-immutable TMap held alive by the captured
			// TSharedPtr; FStreamingProvider opens its own FArchive and never touches the windowed ring).
			Provider->ForEachTimestep(
				[&Builder, &Cancel, &PoseTracks, TimeBetweenSteps](int32 Timestep, const TArray<FSimMovementSample>& Samples)
				{
					if (*Cancel)
					{
						return;
					}
					const float TimeSeconds = static_cast<float>(Timestep) * TimeBetweenSteps;
					Builder.AddTimestep(TimeSeconds, Samples);
					for (const FSimMovementSample& Sample : Samples)
					{
						PoseTracks.FindOrAdd(Sample.EntityID).Add({ TimeSeconds, Sample.Position, Sample.Rotation });
					}
				});

			if (!*Cancel)
			{
				BuiltSet = Builder.Finish();
				BuiltSet.BuiltKey = Key;

				// Layer 2: per-agent failure precompute over the same captured tables + settings, still off
				// the game thread. The PoseSampler interpolates that agent's recorded pose track at the
				// crossing time (ascending times -> binary search + linear lerp; clamps at the ends).
				for (auto& TimelinePair : BuiltSet.Timelines)
				{
					const int32 EntityID = TimelinePair.Key;
					const TArray<FPoseKey>* Track = PoseTracks.Find(EntityID);
					TFunction<bool(float, FVector&, FRotator&)> PoseSampler;
					if (Track && Track->Num() > 0)
					{
						PoseSampler = [Track](float TimeSeconds, FVector& OutLoc, FRotator& OutRot) -> bool
						{
							const TArray<FPoseKey>& Keys = *Track;
							if (TimeSeconds <= Keys[0].TimeSeconds)
							{
								OutLoc = Keys[0].Location;
								OutRot = Keys[0].Rotation;
								return true;
							}
							if (TimeSeconds >= Keys.Last().TimeSeconds)
							{
								OutLoc = Keys.Last().Location;
								OutRot = Keys.Last().Rotation;
								return true;
							}
							int32 LoIdx = 0;
							int32 HiIdx = Keys.Num() - 1;
							while (HiIdx - LoIdx > 1)
							{
								const int32 MidIdx = (LoIdx + HiIdx) / 2;
								if (Keys[MidIdx].TimeSeconds <= TimeSeconds) { LoIdx = MidIdx; } else { HiIdx = MidIdx; }
							}
							const float Span = Keys[HiIdx].TimeSeconds - Keys[LoIdx].TimeSeconds;
							const float Alpha = Span > UE_SMALL_NUMBER
								? FMath::Clamp((TimeSeconds - Keys[LoIdx].TimeSeconds) / Span, 0.0f, 1.0f)
								: 0.0f;
							OutLoc = FMath::Lerp(Keys[LoIdx].Location, Keys[HiIdx].Location, Alpha);
							OutRot = FMath::Lerp(Keys[LoIdx].Rotation, Keys[HiIdx].Rotation, Alpha);
							return true;
						};
					}
					ComputeFailureData(
						TimelinePair.Value, Tables, RoomIndexToTableIndex, SettingsSnapshot, PoseSampler);
				}
			}
		}

		// Hand the result back to the game thread. The apply re-derives the live key and discards on
		// any mismatch (a file swapped mid-build), on cancel, or if a newer job superseded this one.
		AsyncTask(ENamedThreads::GameThread,
			[WeakThis, Serial, Cancel, BuiltSet = MoveTemp(BuiltSet)]() mutable
		{
			if (UBRiskEgressSubsystem* Self = WeakThis.Get())
			{
				Self->ApplyAgentTimelineBuildResult(Serial, Cancel, MoveTemp(BuiltSet));
			}
		});
	});
}

void UBRiskEgressSubsystem::ApplyAgentTimelineBuildResult(
	const uint32 Serial,
	const TSharedRef<FThreadSafeBool>& Cancel,
	UE::Mobius::Tenability::FAgentTimelineSet&& BuiltSet)
{
	// Clear the in-flight flag only if THIS is still the latest dispatched job. A newer request bumped
	// AgentTimelineBuildSerial and set its own InFlightKey; leaving those alone lets it complete.
	const bool bIsLatestJob = (Serial == AgentTimelineBuildSerial);
	if (bIsLatestJob)
	{
		bAgentTimelineBuildInFlight = false;
	}

	// Discard if cancelled (scenario cleared / superseded) or superseded by a newer job.
	if (*Cancel || !bIsLatestJob)
	{
		return;
	}

	// Re-derive the CURRENT key and keep the result only if the built key still matches it (a file
	// swapped DURING the build bumps a generation, so the captured BuiltKey no longer matches -> drop;
	// the processor's next-frame poll re-requests against the fresh key).
	const UE::Mobius::Tenability::FAgentTimelineKey CurrentKey = MakeCurrentTimelineKey();
	if (BuiltSet.BuiltKey != CurrentKey || CurrentKey == UE::Mobius::Tenability::FAgentTimelineKey())
	{
		return;
	}

	AgentTimelines = MoveTemp(BuiltSet);
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
	// SILENT BY CONTRACT - do not add logging here. This is a hot path, not a load-time step: it feeds
	// MakeCurrentTimelineKey's SettingsHash, which the health processor's timeline-currency check
	// re-derives PER AGENT PER FRAME. Every diagnostic about these endpoints belongs in
	// ReportTenabilityEndpointFallbacks, which runs once per scenario load and also surfaces the
	// substitution to the user. A warning here reappears at frame rate x crowd size.
	//
	// It must also stay a pure function of the endpoints for a second reason: the hash it feeds is what
	// decides whether the precomputed timelines are stale, so two calls with the same endpoints have to
	// produce bitwise-identical settings.
	FTenabilityAnalysisSettings Settings;
	if (!BRiskDataSubsystem)
	{
		return Settings;
	}

	const FBRiskTenabilityEndpoints& Endpoints = BRiskDataSubsystem->GetTenabilityEndpoints();

	// Each endpoint the input supplies overrides the documented default; each one it omits keeps the
	// default, and ReportTenabilityEndpointFallbacks is what tells anyone that happened.
	if (Endpoints.bHasMonitorHeight)
	{
		Settings.MonitorHeightM = static_cast<float>(Endpoints.MonitorHeightM);
	}
	if (Endpoints.bHasEndpointVisibility)
	{
		Settings.EndpointVisibilityM = static_cast<float>(Endpoints.EndpointVisibilityM);
	}
	if (Endpoints.bHasEndpointFED)
	{
		Settings.EndpointToxicFED = static_cast<float>(Endpoints.EndpointFED);
	}
	// endpoint_radiation is B-Risk's radiant/thermal FED endpoint.
	if (Endpoints.bHasEndpointRadiation)
	{
		Settings.EndpointThermalFED = static_cast<float>(Endpoints.EndpointRadiation);
	}
	// endpoint_temp is deliberately NOT read: it is not a Celsius layer-temperature threshold (observed
	// O(1000) - raw Kelvin in real B-Risk input), so the temperature criterion stays disabled rather
	// than being wired to a value that would mean something else.

	return Settings;
}

int32 UBRiskEgressSubsystem::ExtractRoomIdSuffix(const FString& SeriesName)
{
	FString Trimmed = SeriesName;
	Trimmed.TrimStartAndEndInline();

	int32 LastUnderscore = INDEX_NONE;
	if (!Trimmed.FindLastChar(TEXT('_'), LastUnderscore) || LastUnderscore + 1 >= Trimmed.Len())
	{
		return INDEX_NONE;
	}

	for (int32 Index = LastUnderscore + 1; Index < Trimmed.Len(); ++Index)
	{
		if (!FChar::IsDigit(Trimmed[Index]))
		{
			return INDEX_NONE;
		}
	}

	return FCString::Atoi(*Trimmed.Mid(LastUnderscore + 1));
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
