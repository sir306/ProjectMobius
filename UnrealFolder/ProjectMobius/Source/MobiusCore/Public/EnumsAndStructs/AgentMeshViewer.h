#pragma once
#include "CoreMinimal.h"
struct FAgentMeshViewer
{
public:
	FAgentMeshViewer():
		AgentID(0),
		AgentWorldPosition(FVector::ZeroVector),
		AgentSpeed(0.0f),
		AgentHeight(180.0f) // Default height in cm
	{}
	FAgentMeshViewer(int32 InAgentID, FVector InAgentWorldPosition, float InAgentSpeed, float InAgentHeight):
		AgentID(InAgentID),
		AgentWorldPosition(InAgentWorldPosition),
		AgentSpeed(InAgentSpeed),
		AgentHeight(InAgentHeight)
	{}
	
	int32 AgentID = 0; // Unique identifier for the agent
	FVector AgentWorldPosition = FVector(FVector::ZeroVector); // World position of the agent
	float AgentSpeed = 0.0f; // Speed of the agent in m/s
	float AgentHeight = 180.0f; // Height of the agent in cm(this is calculated from mesh size * scale)
	//TODO: // Add more properties as needed, such as direction, health, etc.
	// And possibly a flag to indicate if it is visible to the camera - as we don't want to render widgets that are not visible
};
