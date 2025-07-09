// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AgentInfoDisplay.generated.h"

class USlateVectorArtData;
class SPedestrianAgentMeshWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UAgentInfoDisplay : public UWidget
{
	GENERATED_BODY()

public:
	UAgentInfoDisplay();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Info")
	float BaseSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Info")
	float ReferenceDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent Info")
	TObjectPtr<USlateVectorArtData> AgentInfoMeshAsset;

	int32 AgentID;

protected:
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	TSharedPtr<class SPedestrianAgentMeshWidget> DisplayWidget;
};
