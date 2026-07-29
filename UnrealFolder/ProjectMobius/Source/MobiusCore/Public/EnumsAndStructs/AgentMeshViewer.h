#pragma once
#include "CoreMinimal.h"
struct FAgentMeshViewer
{
public:
	FAgentMeshViewer()
	{}
	FAgentMeshViewer(int32 InAgentID, const FText& InAgentName, const FText& InGender, const FText& InDemographic,
	                 const FVector& InAgentWorldPosition, float InAgentSpeed, float InGaitDirectionalSpeed,
	                 float InAgentHeight, float InSpeedFractionOfMax)
		: AgentID(InAgentID), AgentName(InAgentName), Gender(InGender), Demographic(InDemographic),
		  AgentWorldPosition(InAgentWorldPosition), AgentSpeed(InAgentSpeed), GaitDirectionalSpeed(InGaitDirectionalSpeed),
		  AgentHeight(InAgentHeight), SpeedFractionOfMax(InSpeedFractionOfMax)
	{}

	int32 AgentID = -1; // Unique identifier for the agent - -1 indicates no agent or invalid data
	FText AgentName = FText::FromString("Default"); // Name of the agent, can be used for debugging or display purposes
	FText Gender = FText::FromString("Unknown");
	FText Demographic = FText::FromString("Adult"); // Demographic information, e.g., Adult, Child, Elderly
	FVector AgentWorldPosition = FVector(FVector::ZeroVector); // World position of the agent
	float AgentSpeed = 0.0f; // Speed of the agent in m/s
	float GaitDirectionalSpeed = 0.0f; // Speed of the agent in m/s, this is the speed in the direction of the agent's movement
	float AgentHeight = 180.0f; // Height of the agent in cm(this is calculated from mesh size * scale)
	/** Speed flux of the agent, this is the percentage difference between the current speed and the max speed
	i.e. if the value is 1 then the agent is moving at max speed */
	float SpeedFractionOfMax = 0.0f;

	//TODO: // Add more properties as needed, such as direction, health, etc.
	// And possibly a flag to indicate if it is visible to the camera - as we don't want to render widgets that are not visible
};

/**
 * Struct to hold the data needed to render the agent egress health widget,
 * this is separate from FAgentMeshViewer as it is only used for the egress health widget
 * and we want to keep the data separate for clarity and maintainability.
 */
struct FAgentEgressTenabilityViewer
{
	public:
	FAgentEgressTenabilityViewer() {}
	FAgentEgressTenabilityViewer(int32 InAgentID, FVector InAgentWorldPos, float InAgentEgressHealth)
	{
		// Unique Agent ID - ensures no double ups and fast lookups
		AgentID = InAgentID;

		// the world location so we know where to render the SMeshWidget
		AgentWorldPosition = InAgentWorldPos;

		// ensures all inputs for egress health are expected values
		AgentEgressHealth = FMath::Clamp(InAgentEgressHealth, 0.0f, 1.0f);
	}

	/** Unique identifier for the agent - -1 indicates no agent or invalid data */
	int32 AgentID = -1;

	FVector AgentWorldPosition = FVector(FVector::ZeroVector); // World position of the agent

	/**
	 * Backwards-compatible display value: AgentEgressHealth = 1 - Clamp01(DisplayRisk).
	 * This is NOT an analytical health value; prefer DisplayRisk and the separate
	 * tenability fields below.
	 */
	float AgentEgressHealth = 1.0f;

	// --- Tenability publishing (one consistent bar = DisplayRisk; icon = criterion) ---
	/**
	 * Is there a measurement to draw at all? False means the ABSENCE of data, which every other
	 * field here reports identically to a measurement of "clear" (DisplayRisk 0, criterion None).
	 * The in-world bar HIDES when this is false — an empty bar reads as "this agent is fine", which
	 * is a different and possibly untrue claim. See FAgentEgressTenabilityFragment::bHasTenabilityData.
	 */
	bool bHasTenabilityData = false;

	// Display-only aggregate risk: MAX of enabled category risks (never the sum), 0..1.
	float DisplayRisk = 0.0f;

	// Separate normalized per-category risks (0..1, not additive).
	float VisibilityRisk = 0.0f;
	float ToxicFEDRisk = 0.0f;
	float ThermalFEDRisk = 0.0f;
	float TemperatureRisk = 0.0f;
	float LayerHeightRisk = 0.0f;

	// Accumulated agent FED dose (Track B per-room deltas).
	float AccumulatedToxicFED = 0.0f;
	float AccumulatedThermalFED = 0.0f;

	// Current sampled tenability values.
	float CurrentVisibilityM = 20.0f;
	float CurrentTemperatureC = 24.0f;
	float CurrentLayerHeightM = 0.0f;
	float CurrentHeatReleaseKW = 0.0f;

	// ETenabilityCriterion stored as uint8 to avoid a MobiusCore->ProjectMobius
	// module dependency. 0 = None; see ETenabilityCriterion for the mapping.
	uint8 CurrentDominantCriterion = 0;
	uint8 FirstFailureCriterion = 0;

	// Bit flags (UE::Mobius::TenabilityFailureFlags) of all simultaneous failures.
	uint8 FailureMask = 0;

	float FirstFailureTimeSeconds = -1.0f;
	bool bTenabilityFailed = false;
};
