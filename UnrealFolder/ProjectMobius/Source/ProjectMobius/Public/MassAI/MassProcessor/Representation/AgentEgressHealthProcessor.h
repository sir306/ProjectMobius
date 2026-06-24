// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "MassProcessor.h"
#include "AgentEgressHealthProcessor.generated.h"

/**
 * Builds the lightweight per-frame snapshot consumed by the agent egress
 * health SMeshWidget.
 */
UCLASS()
class PROJECTMOBIUS_API UAgentEgressHealthProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAgentEgressHealthProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override;

private:
	UPROPERTY()
	FMassEntityQuery EntityQuery;

	/** Reused through buffer swapping with UStatisticSubsystem. */
	TArray<FAgentEgressTenabilityViewer> AgentEgressHealthData;
};
