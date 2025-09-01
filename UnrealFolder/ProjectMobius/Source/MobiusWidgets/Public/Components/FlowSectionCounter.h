// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlowSectionCounter.generated.h"

class UBorder;
class UFieldAndTextWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UFlowSectionCounter : public UUserWidget
{
	GENERATED_BODY()

#pragma region METHODS
protected:
	virtual void NativePreConstruct() override;
	
	virtual void NativeConstruct() override;
	
	virtual void NativeDestruct() override;

public:
	virtual void SynchronizeProperties() override;

private:

#pragma endregion METHODS

#pragma region PROPERTIES
public:
	/** Root Widget Panel is a grid panel */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties", meta = (BindWidget))
	TObjectPtr<class UGridPanel> RootWidgetGridPanel;

	/** Background Border Inner */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties", meta = (BindWidget))
	TObjectPtr<UBorder> BackgroundBorderInner;
	
	/** Background Border Edge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties", meta = (BindWidget))
	TObjectPtr<UBorder> BackgroundBorderEdge;
	
	/** Section Header Text - Format is "Section {Number} Count:" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties")
	FText SectionHeaderText = FText::FromString("Section 1 Count:");

	/** Section Header Count Text - displays text that reflects the live agent count in this section*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties")
	FText SectionHeaderAgentCountText = FText::FromString("0");

	/** Flow Type Title Text - eg "Section Flow Rate:" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties")
	FText FlowTypeTitleText = FText::FromString("Section Flow Rate:");

	/** Flow data value text - eg "0.00m/s" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties")
	FText FlowValueText = FText::FromString("0.00m/s");

	/** Title and Field widget - we can reuse this widget to display the header text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties", meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> SectionHeaderFieldAndTextWidget;

	/** Title and Field widget - we can reuse this widget to display the type of flow and the value of that flow text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties", meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> FlowTypeAndValueFieldAndTextWidget;

	/** Text Style slate sheet */

	/** Current Agent Count */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties")
	int32 CurrentAgentCount = 0;

	/** Current Flow Value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowSectionCounter|Properties")
	float CurrentFlowValue = 0.0f;

#pragma endregion PROPERTIES
};
