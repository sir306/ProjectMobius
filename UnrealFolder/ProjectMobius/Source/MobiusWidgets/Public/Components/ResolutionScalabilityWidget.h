// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ScalabilitySettingWidget.h"
#include "Interfaces/TextHelperInterface.h"
#include "ResolutionScalabilityWidget.generated.h"

class UComboBoxString;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UResolutionScalabilityWidget : public UScalabilityWidgetBase, public ITextHelperInterface
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

	/**
	 * Populate the combo box with all available resolutions
	 */
	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	void PopulateResolutionComboBox();

	UFUNCTION(BlueprintCallable, Category = "Scalability Settings")
	void SetSelectedDropdownOption(const FIntPoint& NewOption);

#pragma region ComponentsAndProperties
	/** Combo string box to display all available resolutions, allowing the user to quickly adjust the resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UComboBoxString> ResolutionComboBox;
	
	/** Current Screen Resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scalability Settings")
	FIntPoint CurrentScreenResolution = FIntPoint(0,0);

#pragma endregion	
};
