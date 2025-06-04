// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ProjectMobiusGameInstance.generated.h"

// DELEGATES
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTimeDilationScaleFactorChanged); // To broadcast time dilation scale changes
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPedestrianVectorFileChanged, FString, PedestrianVectorFile); // To broadcast simulation data file changes with file
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPedestrianVectorFileUpdated); // Simple signal to say the file has changed
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeshFileChanged); // To broadcast mesh data file changes
// simulations and meshes come in different units, so we need to broadcast when the scale changes and update
// the simulations and meshes so they are in the correct units and scale and are consistent
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeshScaleChanged); // To broadcast when a different mesh scale is selected
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSimulationMovementScaleChange); // To broadcast when the simulation movement scale changes
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalScaleChange); // The scales are all linked, so we can use a global scale to change all scales
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDataLoading, bool, bIsDataBeingLoaded); // To broadcast when data is being loaded


/**
 * This Game Instance is used to store data that needs to be persistent between levels or accessible from
 * any class in Project Mobius. This is where directories DataFiles, MeshFiles, and other data that needs to be accessed
 * from multiple classes will be stored.
 */
UCLASS()
class PROJECTMOBIUS_API UProjectMobiusGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
#pragma region PUBLIC_METHODS
	/** Constructor */
	UProjectMobiusGameInstance();

	/** Initialize the Game Instance */
	virtual void Init() override;

	/** Shutdown the Game Instance */
	virtual void Shutdown() override;

	//TODO: convert methods below so we can broadcast events when the data is changed

	/**
	 * Set the Pedestrian Data File Path
	 *
	 * @param NewPedestrianDataFilePath - The new file path to set
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation Settings")
	void SetPedestrianDataFilePath(const FString& NewPedestrianDataFilePath);

	/**
	 * Set the Pedestrian Data File Name
	 *
	 * @param NewPedestrianDataFileName - The new file name to set
	 */
	void SetPedestrianDataFileName(const FString& NewPedestrianDataFileName);

	/**
	 * Set the Simulation Mesh File Path
	 *
	 * @param NewSimulationMeshFilePath - The new file path to set
	 */
	UFUNCTION(BlueprintCallable, Category = "Simulation Settings")
	void SetSimulationMeshFilePath(const FString& NewSimulationMeshFilePath);

	/**
	 * Set the Simulation Mesh File Name
	 *
	 * @param NewSimulationMeshFileName - The new file name to set
	 */
	void SetSimulationMeshFileName(const FString& NewSimulationMeshFileName);

	/**
	 * Set the Time Dilation Scale Factor
	 *
	 * @param NewTimeDilationScaleFactor - The new time dilation scale factor to set
	 */
	void SetTimeDilationScaleFactor(const float NewTimeDilationScaleFactor);

	/**
	 * Set the Mesh Scale for the runtime mesh generator
	 *
	 * @param NewMeshScale - The new mesh scale to set
	 */
	void SetMeshScale(const float NewMeshScale);

	/**
	 * Set the Simulation Movement Scale for the mass entity simulation
	 *
	 * @param NewSimulationMovementScale - The new simulation movement scale to set
	 */
	void SetSimulationMovementScale(const float NewSimulationMovementScale);

	/**
	 * Set the Global Scale for the simulation, mesh, and other scales
	 * This is so the user can change all scales at once and have the system be as big or small as they want
	 *
	 * @param NewGlobalScale - The new global scale to set
	 */
	void SetGlobalScale(const float NewGlobalScale);

	/**
	 * Set the state of the data loading to prevent simulations from starting too early
	 * and broadcast that data is being loaded
	 *
	 * @param[bool] bNewLoadingState - The new state of the data loading
	 */
	void SetDataLoadingState(const bool bNewLoadingState);

#pragma endregion PUBLIC_METHODS

#pragma region PUBLIC_VARIABLES
	/** Time Dilation Scale Factor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings")
	float TimeDilationScaleFactor;

	/** Mesh Model Scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings")
	float MeshScale;

	/** Simulation Trajectory Scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings")
	float SimulationMovementScale;

	/** Delegate that we can broadcast time dilation scale changes */
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnTimeDilationScaleFactorChanged OnTimeDilationScaleFactorChanged;

	/** Delegate that we can broadcast simulation data changes with a file name*/
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnPedestrianVectorFileChanged OnPedestrianVectorFileChanged;

	/** Delegate that we can broadcast simulation data changes */
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnPedestrianVectorFileUpdated OnPedestrianVectorFileUpdated;

	/** Delegate to broadcast Mesh file has changed */
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnMeshFileChanged OnMeshFileChanged;
	
	/** Delegate that can broadcast mesh scale changes */
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnMeshScaleChanged OnMeshScaleChanged;

	/** Delegate that can broadcast simulation scale changes */
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnSimulationMovementScaleChange OnSimulationMovementScaleChange;

	/** Delegate that can broadcast global scale changes */
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnGlobalScaleChange OnGlobalScaleChange;

	/** Delegate to broadcast that data is being loaded and prevent spawning too early */
	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FDataLoading OnDataLoading;

#pragma endregion PUBLIC_VARIABLES

protected:
#pragma region PROTECTED_METHODS

#pragma endregion PROTECTED_METHODS

#pragma region PROTECTED_VARIABLES
	/** To prevent simulations from starting we set this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings", meta = (AllowPrivateAccess = "true"))
	bool bIsDataBeingLoaded = false;
#pragma endregion PROTECTED_VARIABLES

private:
#pragma region PRIVATE_METHODS

#pragma endregion PRIVATE_METHODS

#pragma region PRIVATE_VARIABLES

#pragma region DATA_FILES
	/** Pedestrian Data File Path -- The complete file path */
	FString PedestrianDataFilePath;

	/** Pedestrian Data File Name -- The name of the file */
	FString PedestrianDataFileName;

	/** Simulation Mesh File Path -- The complete file path */
	FString SimulationMeshFilePath;

	/** Simulation Mesh File Name -- The name of the file */
	FString SimulationMeshFileName;

	//TODO: we will bring datatables back as it makes it to manage and requires less memory allocation
	// /** DataTable that holds the different types of meshes for the agents */
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation Settings", meta = (AllowPrivateAccess = "true"))
	// UDataTable* AgentMeshDataTable;
#pragma endregion DATA_FILES

#pragma region SIMULATION_VARIABLES
	/** To ensure selected materials for agents are persistent we can store them here */
	/** Male pedestrian material */
	UPROPERTY()
	UMaterialInstanceDynamic* SelectedMaleAdultMaterialInstance = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* SelectedMaleAdultEyesMaterialInstance = nullptr;

	/** Female adult pedestrian material */
	UPROPERTY()
	UMaterialInstanceDynamic* SelectedFemaleAdultMaterialInstance = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* SelectedFemaleAdultEyesMaterialInstance = nullptr;

	/** Male elderly pedestrian material */
	UPROPERTY()
	UMaterialInstanceDynamic* SelectedMaleElderlyMaterialInstance = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* SelectedMaleElderlyEyesMaterialInstance = nullptr;

	/** Female Elderly pedestrian material */
	UPROPERTY()
	UMaterialInstanceDynamic* SelectedFemaleElderlyMaterialInstance = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* SelectedFemaleElderlyEyesMaterialInstance = nullptr;

	/** Children pedestrian material */
	UPROPERTY()
	UMaterialInstanceDynamic* SelectedChildrenMaterialInstance = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* SelectedChildrenEyesMaterialInstance = nullptr;

#pragma endregion SIMULATION_VARIABLES

#pragma endregion PRIVATE_VARIABLES


public:
#pragma region GETTERS
	/** Get the Pedestrian Data File Path */
	FString GetPedestrianDataFilePath() const { return PedestrianDataFilePath; }

	/** Get the Pedestrian Data File Name */
	FString GetPedestrianDataFileName() const { return PedestrianDataFileName; }

	/** Get the Simulation Mesh File Path */
	FString GetSimulationMeshFilePath() const { return SimulationMeshFilePath; }

	/** Get the Simulation Mesh File Name */
	FString GetSimulationMeshFileName() const { return SimulationMeshFileName; }

	/** Get the Time Dilation Scale Factor */
	float GetTimeDilationScaleFactor() const { return TimeDilationScaleFactor; }

	//TODO: we will bring datatables back as it makes it to manage and requires less memory allocation
	// /** Get the Agent Mesh Data Table */
	// UDataTable* GetAgentMeshDataTable() const { return AgentMeshDataTable; }

	/** Get the male pedestrian material */
	UFUNCTION(BlueprintCallable)
	UMaterialInstanceDynamic* GetSelectedMaleMaterialInstance() const { return SelectedMaleAdultMaterialInstance; }

	UFUNCTION(BlueprintCallable)
	UMaterialInstanceDynamic* GetSelectedMaleEyesMaterialInstance() const { return SelectedMaleAdultEyesMaterialInstance; }

	/** Get the female pedestrian material */
	UMaterialInstanceDynamic* GetSelectedFemaleMaterialInstance() const { return SelectedFemaleAdultMaterialInstance; }

	UMaterialInstanceDynamic* GetSelectedFemaleEyesMaterialInstance() const { return SelectedFemaleAdultEyesMaterialInstance; }
#pragma endregion GETTERS

#pragma region SETTERS
	/** Set the male adult pedestrian body material */
	FORCEINLINE void SetSelectedMaleAdultMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedMaleAdultMaterialInstance = NewMaterialInstance; }
	FORCEINLINE void SetSelectedMaleAdultEyesMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedMaleAdultEyesMaterialInstance = NewMaterialInstance; }

	/** Set the male elderly pedestrian body material */
	FORCEINLINE void SetSelectedMaleElderlyMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedMaleElderlyMaterialInstance = NewMaterialInstance; }
	FORCEINLINE void SetSelectedMaleElderlyEyesMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedMaleElderlyEyesMaterialInstance = NewMaterialInstance; }

	/** Set the female pedestrian body material */
	FORCEINLINE void SetSelectedFemaleAdultMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedFemaleAdultMaterialInstance = NewMaterialInstance; }
	FORCEINLINE void SetSelectedFemaleAdultEyesMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedFemaleAdultEyesMaterialInstance = NewMaterialInstance; }

	/** Set the female elderly pedestrian body material */
	FORCEINLINE void SetSelectedFemaleElderlyMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedFemaleElderlyMaterialInstance = NewMaterialInstance; }
	FORCEINLINE void SetSelectedFemaleElderlyEyesMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedFemaleElderlyEyesMaterialInstance = NewMaterialInstance; }

	/** Set the children pedestrian body material */
	FORCEINLINE void SetSelectedChildrenMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedChildrenMaterialInstance = NewMaterialInstance; }
	FORCEINLINE void SetSelectedChildrenEyesMaterialInstance(UMaterialInstanceDynamic* NewMaterialInstance) { SelectedChildrenEyesMaterialInstance = NewMaterialInstance; }


	
#pragma endregion SETTERS
};
