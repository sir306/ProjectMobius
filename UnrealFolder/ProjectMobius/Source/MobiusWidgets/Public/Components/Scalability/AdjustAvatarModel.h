// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnumsAndStructs/AvatarScalabilityEnum.h"
#include "AdjustAvatarModel.generated.h"

/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API UAdjustAvatarModel : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	
	
	/** Get the current selected avatar model type from the performance util */
	UFUNCTION(BlueprintCallable, Category = "Avatar Scalability Settings")
	EPedestrianScalabilitySettings GetCurrentAvatarModelType() const;
	
	
	/** TODO: Listed likely functions we will need to implement once the core logic and avatar models are ready
		* Adjust the avatar model based on the current scalability settings *
		* Turn on/off animations for realistic pedestrian avatars (i.e. ones that have legs and arms) *
	*/
	// As we have only two model one animated and one static blob, we only need a single function to toggle between them for now

	/** Toggles between realistic avatar with anims and static blob avatars - useful when users want more control of what they see */
	UFUNCTION(BlueprintCallable, Category = "Avatar Scalability Settings")
	void ToggleAvatarModel();

	/** Store the current Scalability setting of the pedestrian avatars */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Avatar Scalability Settings")
	TEnumAsByte<EPedestrianScalabilitySettings> CurrentAvatarModelType = EPedestrianScalabilitySettings::EPss_High;// for now we only use low or high, but we can expand this later if needed
};
