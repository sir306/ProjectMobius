// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LoadingSubsystem.generated.h"

/**
 * Loading notification delegate, to broadcast current loading percent
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoadingPercentChanged, float, NewLoadPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadingTextChanged, FString, NewTitle, FString, NewText);
// change the loading text to DECLARE_DELEGATE_TwoParams(FOnLoadingTextChanged, FString&, FString&); once we know no blueprints will use it

/**
 * 
 */
UCLASS()
class MOBIUSCORE_API ULoadingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	ULoadingSubsystem();
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;


	/**
	 * Calculate the Load Percent - int32 input
	 * - after the calculation is complete it will update the percent value and check if the load is complete
	 *
	 * @param CurrentLoad - Current Load Value
	 * @param TotalLoad - Total Load Value
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void CalculateLoadPercentInt(int32 CurrentLoad, int32 TotalLoad);

	/**
	 * Calculate the Load Percent - float input
	 * - after the calculation is complete it will update the percent value and check if the load is complete
	 *
	 * @param CurrentLoad - Current Load Value
	 * @param TotalLoad - Total Load Value
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void CalculateLoadPercentFloat(float CurrentLoad, float TotalLoad);

	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void BroadcastNewLoadPercent(float NewLoadPercent)
	{
		OnLoadingPercentChanged.Broadcast(NewLoadPercent);
	}

	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void SetLoadingTextAndTitle(const FString& NewLoadingText, const FString& NewLoadingTitle) const
	{
		OnLoadingTextChanged.Broadcast(NewLoadingTitle, NewLoadingText);
	}

#pragma region LoadingProperties
	UPROPERTY(BlueprintAssignable, Category = "LoadingNotifyWidget|Events")
	FOnLoadingPercentChanged OnLoadingPercentChanged;

	UPROPERTY(BlueprintAssignable, Category = "LoadingNotifyWidget|Events")
	FOnLoadingTextChanged OnLoadingTextChanged;
	
#pragma endregion LoadingProperties
	
};
