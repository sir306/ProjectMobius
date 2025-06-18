// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

#pragma once

#include "CoreMinimal.h"
#include "MassRepresentationSubsystem.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "MRS_RepresentationSubsystem.generated.h"

/**
 * 
 */

class UNiagaraSystem;
enum class EAgeDemographic : uint8;

template<>
struct TMassExternalSubsystemTraits<UMRS_RepresentationSubsystem> final
{
	enum
	{
		ThreadSafeRead = true,
		ThreadSafeWrite = false,
		GameThreadOnly = false,
	};
};

/**
 * Enum for pedestrian genders
 */
UENUM()
enum class EPedestrianGender : uint8
{
	Epg_Male = 0,
	Epg_Female = 1,
	Epg_Default = 2
};

UENUM()
enum class EPedestrianMovementBracket : uint8
{
	Emb_NotMoving = 0,
	Emb_Shuffle = 1,
	Emb_SlowWalk = 2,
	Emb_Walk = 3,
	Emb_BriskWalk = 4,
	Emb_Error = 5
};

struct FVatMovementFrames
{
	float FirstFrame;
	float LastFrame;
	float LowSpeed;
	float HighSpeed;
	float AnimatedSpeed;
	float AnimatedStepLength;
	EPedestrianMovementBracket MovementBracket;
};

// These default bracket parameters are coarse and we will ultimately need more, narrower bands of speed and step length
static const FVatMovementFrames AvatarGaitSpeedBands[] = {             // TODO: need more, narrower bands: fastShuffle, VerySlowWalk etc.
	// loSpeed, hiSpeed, animated speed, animatedStepLength, enum avatar mode
	{ 1.0f, 1.0f, 0.00f,    0.02f,   0.000f,         0.020f,             EPedestrianMovementBracket::Emb_NotMoving },  
	{ 16.0f, 48.0f, 0.02f,    0.20f,   0.125f,         0.050f,             EPedestrianMovementBracket::Emb_Shuffle },    
	{  57.1f, 90.8f, 0.20f,    0.89f,   0.6353f,        0.360f,             EPedestrianMovementBracket::Emb_SlowWalk },  
	{ 93.0f, 125.0f,0.89f,    1.66f,   1.1293f,        0.690f,             EPedestrianMovementBracket::Emb_Walk },      
	{ 129.0f, 155.0f,1.66f,    2.7f,    2.1925f,        0.925f,             EPedestrianMovementBracket::Emb_BriskWalk },  
	{ 0.0f, 0.0f,-1.0f,    -0.01f,  -0.00f,         -0.00f,             EPedestrianMovementBracket::Emb_Error }       // negative speeds / error
};

/**
 * Subsystem for the representation of agents
 */
UCLASS()
class PROJECTMOBIUS_API UMRS_RepresentationSubsystem : public UMassRepresentationSubsystem, public IProjectMobiusInterface
{
	GENERATED_BODY()
	
public:
#pragma region PUBLIC_METHODS
	/** Constructor */
	UMRS_RepresentationSubsystem();

	/**
	 * Method to set the material on the pedestrian instance
	 *
	 * @param[UMaterialInstanceDynamic*] MaterialInst The material instance to set
	 * @param[EPedestrianGender] AgentGender The gender of the agent to set
	 * 
	 */
	UFUNCTION(BlueprintCallable, Category = "MRS|Subsystem|ThesisResearch")
	void SetPedestrianMaterial(UMaterialInstanceDynamic* MaterialInst, EPedestrianGender AgentGender);
	void SetPedestrianMaterial(UMaterialInstanceDynamic* MaterialInstBody,UMaterialInstanceDynamic* MaterialInstEyes, EPedestrianGender AgentGender);
	void SetPedestrianMaterial(UMaterialInstanceDynamic* MaterialInstBody,UMaterialInstanceDynamic* MaterialInstEyes, EPedestrianGender AgentGender, EAgeDemographic AgentAgeDemographic);

	/**
	 * Get male material via the mobius interface, will return nullptr if the material is not set
	 *
	 * @return[UMaterialInstanceDynamic*] The male material instance
	 */
	UMaterialInstanceDynamic* GetSelectedMaleMaterialInstance(UWorld* World);

	UFUNCTION(BlueprintCallable)
	UMaterialInstanceDynamic* GetSelectedMaleEyesMaterialInstance(UWorld* World);

	/**
	 * Get the female material via the mobius interface, will return nullptr if the material is not set
	 * 
	 * @return[UMaterialInstanceDynamic*] The female material instance 
	 */
	UFUNCTION(BlueprintCallable)
	UMaterialInstanceDynamic* GetSelectedFemaleMaterialInstance();

	UFUNCTION(BlueprintCallable)
	UMaterialInstanceDynamic* GetSelectedFemaleEyesMaterialInstance();

	/**
	 * Method to calculate which speed to use for the agent based on the current speed
	 *
	 * @param[float] CurrentSpeed The current speed of the agent
	 * @param[float] AgentMaxSpeed The agents max speed TODO: this is a quick fix to create variations in agent movement speeds
	 * @param[float&] StepsPerSecond The steps per second for the agent
	 *
	 * @return[struct FVatMovementFrames] The movement frames for the agent
	 */
	static FVatMovementFrames CalculateMovementFrames(float CurrentSpeed, float AgentMaxSpeed, float& StepsPerSecond);

	/**
	 * Get the movement frame for the movement bracket
	 */
	static FVatMovementFrames GetMovementFrames(EPedestrianMovementBracket MovementBracket);

	/**
	 * Set the max height of rendered agents
	 */
	UFUNCTION(BlueprintCallable, Category = "MRS|Subsystem|ThesisResearch")
	void SetMaxRenderHeight(float NewMaxRenderHeight);

	/**
	 * Get the max height of rendered agents
	 */
	UFUNCTION(BlueprintCallable, Category = "MRS|Subsystem|ThesisResearch")
	float GetMaxRenderHeight() const { return MaxRenderHeight; }

	/**
	 * Returns the object path for the niagara agent system
	 * @param[bool] bIsLowSpec If true, returns the low spec version of the niagara agent system
	 * @return[const TCHAR*] The object path for the niagara agent system
	 */
	static const TCHAR* GetNiagaraAgentSystemObjectPath(bool bIsLowSpec = false);

	/**
	 * Load the Niagara agent system, when calling this method you should perform a null check on the returned value
	 * @param[bool] bIsLowSpec If true, loads the low spec version of the niagara agent system
	 * @return[UNiagaraSystem*] The loaded niagara system
	 */
	static UNiagaraSystem* LoadNiagaraAgentSystem(bool bIsLowSpec = false);

#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_VARIABLES
	///** PTR to the shared entity manager */
	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MRS|Subsystem")
	//TSharedPtr<FMassEntityManager> EntityManager;

	// Max height of rendered agents
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MRS|Subsystem")
	float MaxRenderHeight = FLT_MAX;

#pragma endregion PUBLIC_VARIABLES

protected:
#pragma region INHERTITED_METHODS
	// USubsystem BEGIN
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// USubsystem END
#pragma endregion INHERTITED_METHODS
};

