#pragma once
#include "CoreMinimal.h"
struct FAgentMeshViewer
{
public:
	FAgentMeshViewer()
	{}
	FAgentMeshViewer(int32 InAgentID, const FText& InAgentName, const FText& InGender, const FText& InDemographic,
	                 const FVector& InAgentWorldPosition, float InAgentSpeed, float InGaitDirectionalSpeed,
	                 float InAgentHeight, float InAgentSpeedFlux)
		: AgentID(InAgentID), AgentName(InAgentName), Gender(InGender), Demographic(InDemographic),
		  AgentWorldPosition(InAgentWorldPosition), AgentSpeed(InAgentSpeed), GaitDirectionalSpeed(InGaitDirectionalSpeed),
		  AgentHeight(InAgentHeight), AgentSpeedFlux(InAgentSpeedFlux)
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
	float AgentSpeedFlux = 0.0f;// TODO: name of variable doesn't make sense
	
	//TODO: // Add more properties as needed, such as direction, health, etc.
	// And possibly a flag to indicate if it is visible to the camera - as we don't want to render widgets that are not visible
};
