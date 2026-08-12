// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassAI/Fragments/AgentEgressTenabilityFragments.h"
#include "AgentEgressHealthCalculationProcessor.generated.h"

/**
 * Integrates B-Risk room exposure into persistent per-agent health fragments.
 *
 * The default dose budgets are a configurable prototype, not a validated
 * ISO 13571/Purser implementation. Direct B-Risk FED channels take precedence
 * when the source scenario exports them.
 */
UCLASS(Config = Mass, DefaultConfig)
class PROJECTMOBIUS_API UAgentEgressHealthCalculationProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAgentEgressHealthCalculationProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& ExecutionContext) override;

private:
	static float NormalizePositiveDose(float Dose, float Budget);
	static float NormalizeAboveThreshold(float Value, float Threshold, float CriticalValue);

	UPROPERTY()
	FMassEntityQuery EntityQuery;

	/** Tenability endpoints + enabled criteria, rebuilt from B-Risk input on scenario load. */
	FTenabilityAnalysisSettings TenabilitySettings;

	/** Scenario generation the cached TenabilitySettings were built for. */
	uint64 TenabilitySettingsGeneration = TNumericLimits<uint64>::Max();

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Integration", meta = (ClampMin = "0.001"))
	float MaximumIntegrationStepSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Smoke", meta = (ClampMin = "0.0"))
	float OpticalDensityThresholdPerMeter = 0.1f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Smoke", meta = (ClampMin = "0.001"))
	float SmokeDoseBudget = 120.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Heat")
	float HeatThresholdC = 60.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Heat", meta = (ClampMin = "0.001"))
	float HeatDoseBudget = 7200.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.0"))
	float CarbonMonoxideThresholdPpm = 200.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.001"))
	float CarbonMonoxideDoseBudget = 60000.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.0"))
	float CarbonDioxideThresholdPercent = 2.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.001"))
	float CarbonDioxideDoseBudget = 480.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.0"))
	float HydrogenCyanideThresholdPpm = 80.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.001"))
	float HydrogenCyanideDoseBudget = 7200.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float OxygenThresholdPercent = 19.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Egress Health|Gas", meta = (ClampMin = "0.001"))
	float OxygenDeficitDoseBudget = 300.0f;
};
