// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "MassAI/Fragments/EntityInfoFragment.h"
#include "DisplayAgentUIStatsProcessor.generated.h"


/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UDisplayAgentUIStatsProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UDisplayAgentUIStatsProcessor();

protected:
	virtual void ConfigureQueries() override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override; // note this is a pure virtual function that needs to be implemented otherwise engine will crash
	void UpdateUIStats(const FEntityInfoFragment& EntityInfo, const FEntityMovementFragment& EntityMovement, const FEntityRenderingFragment& EntityRendering);

private:
	// Entity Query
	UPROPERTY()
	FMassEntityQuery EntityQuery;

	// holder for mesh viewer data
	TArray<FAgentMeshViewer> AgentData;

	FAgentMeshViewer SelectedAgentData = FAgentMeshViewer(); // Holds the currently selected agent data for the mesh viewer
	FAgentMeshViewer HoveredAgentData = FAgentMeshViewer(); // Holds the currently selected agent data for the mesh viewer
};
