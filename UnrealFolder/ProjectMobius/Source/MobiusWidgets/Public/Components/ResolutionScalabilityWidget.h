// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ScalabilitySettingWidget.h"
#include "ResolutionScalabilityWidget.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UResolutionScalabilityWidget : public UScalabilitySettingWidget
{
	GENERATED_BODY()

protected:
	virtual void NativePreConstruct() override;

	void UpdateCurrentScreenResolutionVal();

	virtual void UpdateScalabilityLevel() override;

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	TArray<FIntPoint> GetAllAvailableResolutions() const;

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	void UpdateScreenResolution(FIntPoint NewResolution);

	/** Current Screen Resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings")
	FIntPoint CurrentScreenResolution = FIntPoint(0,0);
};
