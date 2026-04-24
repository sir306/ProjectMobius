// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlowCounterWidget.generated.h"

class UFlowSectionCounter;
class UFieldAndTextWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UFlowCounterWidget : public UUserWidget
{
	GENERATED_BODY()
	
#pragma region METHODS
	// Start of UUserWidget overrides
public:
	

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void SynchronizeProperties() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	
	// End of UUserWidget overrides

public:
	/** When the counter is created we want to ensure the widgets title values are set and the live count is set to 0 in the text */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|Methods")
	void InitializeCounterWidget();
	
	/** Update the live agent count value, clamps to a min value of 0, will also update the text if the new value is different
	 *
	 * @param[int32] NewValue The new live agent count value to set
	 */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|Methods")
	void UpdateLiveAgentCount(const int32 NewValue);
	
	/** Updates the live agent count text field with the current live agent count property */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|Methods")
	void UpdateLiveAgentCountField();

	/** Adds a new flow section counter to the uniform grid panel, will increment the current flow data sections property */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|Methods")
	void AddFlowSectionCounter();

	/** Removes the last flow section counter from the uniform grid panel, will decrement the current flow data sections property */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|Methods")
	void RemoveFlowSectionCounter();

	/** Update Section Counters style format, called when the section counter value changes */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|Methods")
	void UpdateFlowSectionCountersStyle();

	// TODO: add methods for updating specific section counter values

protected:
	/** Create a new flow counter section widget, simple method performs no checks, so make sure to nullptr check after calling this method */
	UFlowSectionCounter* CreateNewFlowSectionCounterWidget();

private:
	/** Internal implementation — takes explicit geometry so NativeTick can pass MyGeometry instead of stale CachedGeometry. Returns true when style was fully applied. */
	bool UpdateFlowSectionCountersStyleInternal(const FGeometry& WidgetGeometry);
	
#pragma endregion METHODS

#pragma region PROPERTIES
public:
	// Expose minimum font size
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	int32 MinFontSize = 12;

	/** Title text for this widget */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	FText TitleText;

	/** Widget blueprint to spawn the section counter components */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties")
	TSubclassOf<UFlowSectionCounter> FlowSectionCounterWidgetClass;
	
	bool bNeedsSectionStyleUpdate = true;

protected:
	/** Uniform Grid Panel for root component  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties", meta = (BindWidget))
	TObjectPtr<class UUniformGridPanel> RootUniformGridPanel;
	
	/** Displays the live agent count that has passed through the counter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties", meta = (BindWidgetOptional))
	TObjectPtr<UFieldAndTextWidget> LiveAgentCountFieldAndTextWidget;

	/** When displaying specific flow data, we add them to this uniform grid, this ensures sizing allocation is equal and
	 * provides ease of visually displaying the section containers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties", meta = (BindWidgetOptional))
	TObjectPtr<class UUniformGridPanel> FlowDataUniformGridPanel;

	/** Optional Scroll box that we may use for preventing the uniform grid panel being to overpopulated and causing widget size to increase beyond the ideal size */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FlowCounter|Properties", meta=(BindWidgetOptional))
	TObjectPtr<class UScrollBox> OptionalScrollBox = nullptr;

private:
	/** The current number of flow data sections added to the grid always has a minimum value of 1, values should be updated with the setter/methods */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties", meta = (BindWidgetOptional, AllowPrivateAccess = "true", ClampMin = "1"))
	int32 CurrentFlowDataSections = 1;

	/** The current agent count displayed in the live agent count field */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties", meta = (BindWidgetOptional, AllowPrivateAccess = "true", ClampMin = "0"))
	int32 CurrentLiveAgentCount = 0;

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlowCounter|Properties", meta = (AllowPrivateAccess = "true"))
	TArray<UFlowSectionCounter*> FlowSectionCounters;

#pragma endregion PROPERTIES

#pragma region GETTERS_SETTERS
public:
	// Getters
	/** Get the number of current flow data sections */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|GettersSetters")
	FORCEINLINE int32 GetFlowDataSections() const { return CurrentFlowDataSections; }

	/** Get the current live agent number for this counter */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|GettersSetters")
	FORCEINLINE int32 GetLiveAgentCount() const { return CurrentLiveAgentCount; }
	
	// Setters
	/** Set the number of current flow data sections, will clamp to a minimum of 1 */
	UFUNCTION(BlueprintCallable, Category="FlowCounterWidget|GettersSetters")
	FORCEINLINE void SetFlowDataSections(const int32 Value) { CurrentFlowDataSections = FMath::Clamp(Value, 1, Value); }

protected:
	

public:
#pragma endregion GETTERS_SETTERS
};
