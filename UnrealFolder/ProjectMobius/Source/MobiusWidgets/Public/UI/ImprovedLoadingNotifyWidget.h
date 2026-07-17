// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Animation/CurveSequence.h" // BW3/P6: intro fade+scale (FCurveSequence, mirrors SMoveableWindow D67)
#include "ImprovedLoadingNotifyWidget.generated.h"

class UTextBlock;
class UBaseLoadingWidget;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UImprovedLoadingNotifyWidget : public UUserWidget
{
	GENERATED_BODY()

#pragma region METHODS
public:
#pragma region PUBLIC_METHODS
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Synchronize properties with the widget
	virtual void SynchronizeProperties() override;

	/**
	 * Update Load percent value
	 *
	 * @param[float] NewLoadPercent - New Load Percent Value
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void UpdateLoadPercent(float NewLoadPercent);

	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void SetIsLoadingGeometry(bool bNewIsLoadingGeometry);
	
	/**
	 * To simplify the loading widget we can auto hide it when the load is complete
	 */
	void IsLoadingComplete();

	/**
	 * Reset the loading percent to 0
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void ResetLoadPercent();

	/**
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void UpdateLoadingTitleTextWidget();

	UFUNCTION(BlueprintCallable, Category = "LoadingNotifyWidget|Methods")
	void SetLoadingText(bool bIsLoadingBar, FText& NewLoadingText);

	void UpdateLoadingTitleText();

	void UpdateGameInstanceLoadingState();
	
	/**
	 * Method to get the current load percent
	 * 
	 * @return float - The current load percent 
	 */
	//UFUNCTION()
	//float GetLoadPercent() const { return LoadPercent; }
	
	
#pragma endregion PUBLIC_METHODS

#pragma region PROTECTED_METHODS
protected:
	void UpdateLoadingWidgets();

	void SetLoadingWidgetVisibility(TObjectPtr<UBaseLoadingWidget> LoadingWidget, bool bIsVisible);

	/**
	 * §5/P6 entrance: (re)start the 150ms CubicOut intro (render-opacity 0->1 + centred scale .97->1)
	 * on the popup root. Called from the Collapsed->visible transition in IsLoadingComplete(). Driven in
	 * NativeTick from IntroCurve — same C++ pattern as SMoveableWindow's OpenAnimation (D67), which avoids
	 * the UE 5.5 "WidgetAnimation can't be created via python" block (D78).
	 */
	void PlayIntroAnimation();
#pragma endregion PROTECTED_METHODS
	
#pragma endregion METHODS

#pragma region PROPERTIES

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UBaseLoadingWidget> LoadingBarWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UBaseLoadingWidget> LoadingInfiniteWidget;

	/** Text block to show current Load Title - this will inform the user what loading action is being done */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UTextBlock> LoadingTitleText;

private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsLoadingComplete = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsLoadingGeometry = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	bool bIsLoadingPedestrianVectors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	float LoadingBarPercent = 1.0f;

	/** Ptr to the game instance to prevent recasting */
	UPROPERTY()
	TObjectPtr<class UProjectMobiusGameInstance> ProjectMobiusGameInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	FText LoadingTitle;

	/** §5/P6 intro animation (150ms CubicOut) + its lerp handle. Plain members (not UPROPERTY). */
	FCurveSequence IntroAnimation;
	FCurveHandle IntroCurve;

#pragma endregion PROPERTIES

#pragma region GETTERS_SETTERS
public:
	FORCEINLINE void SetLoadingGeometry(bool bIsLoadingGeometryComplete) { bIsLoadingGeometry = bIsLoadingGeometryComplete; UpdateLoadingWidgets(); }

	FORCEINLINE void SetLoadingPedestrianVectors(float NewLoadPercent){ LoadingBarPercent = NewLoadPercent; UpdateLoadingWidgets(); }

#pragma endregion GETTERS_SETTERS
};
