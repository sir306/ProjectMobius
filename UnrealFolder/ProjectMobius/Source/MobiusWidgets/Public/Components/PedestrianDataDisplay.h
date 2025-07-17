// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "PedestrianDataDisplay.generated.h"

class UAgentInfoDisplay;
class UFieldAndTextWidget;
class UTextBlock;
class UGridPanel;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UPedestrianDataDisplay : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void SynchronizeProperties() override;

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	
	void ConfigureTextBlockStyles() const;

	void SetupTextBlockTitles() const;

	void UpdateFieldTextBlocks() const;
public:
	/** Grid Panel to arrange items */
	UPROPERTY(EditAnywhere,BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UGridPanel> WidgetHeadGridPanel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UFieldAndTextWidget> TitleFieldWidget8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UAgentInfoDisplay> InWorldSMeshDisplay;
};
