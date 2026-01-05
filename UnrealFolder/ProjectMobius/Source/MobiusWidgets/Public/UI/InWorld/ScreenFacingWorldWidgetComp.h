// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "ScreenFacingWorldWidgetComp.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MOBIUSWIDGETS_API UScreenFacingWorldWidgetComp : public UWidgetComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UScreenFacingWorldWidgetComp();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	/** Update the widget comp to face the camera */
	void UpdateWidgetToFaceCamera();

	/**
	 * Calculate the rotation required for the widget to face the camera.
	 * @return A FRotator representing the rotation needed to align the widget with the camera.
	 */
	FRotator GetRotationToFaceCamera() const;

	/** Timer Handle to control the rate at we update the widget rotation towards the camera */
	FTimerHandle UpdateWidgetRotationTimerHandle;

	/** Rate at which to update the widget rotation towards the camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ScreenFacingWidgetComp|Properties")
	float UpdateRate = 1.0f;

	/** ptr to the camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="ScreenFacingWidgetComp|Properties")
	TObjectPtr<class UCameraComponent> CameraPtr;
};
