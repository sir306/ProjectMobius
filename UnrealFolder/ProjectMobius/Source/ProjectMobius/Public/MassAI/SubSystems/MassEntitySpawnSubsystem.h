// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
//#include "MassEntitySubsystem.h"
#include "MassSpawnerSubsystem.h"
#include "Interfaces/ProjectMobiusInterface.h"
#include "MassAI/Fragments/SharedFragments/SimulationFragment.h"
#include "MassAI/Fragments/SharedFragments/RepresenatationFragments/AgentRepresenatationFragment.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntitySpawnSubsystem.generated.h"

class UAgentDataSubsystem;

// Delegate to broadcast when the pedestrian data is loaded and processed
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPedestrianDataLoaded);

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UMassEntitySpawnSubsystem : public UMassSpawnerSubsystem, public IProjectMobiusInterface
{
	GENERATED_BODY()
	
public:
	/** Constructor */
	UMassEntitySpawnSubsystem();

	/**
	* Initializes the subsystem, dependencies and registers it with the collection.
	*
	* @param Collection: The collection that owns this subsystem, used to register other subsystems with the subsystem.
	* 
	*/
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Deintialize the subsystem */
	virtual void Deinitialize() override;

	/**
	* A method used for spawning our custom archetypes at any point we are in the world not just begin play.
	* This method can be called from blueprints or code.
	*
	* @param NumberOfPedestriansToSpawn: The number of pedestrians to spawn
	* 
	*/
	UFUNCTION(BlueprintCallable, Category = "MassAI|Spawn")
	void SpawnMassEntityPedestrians(int32 NumberOfPedestriansToSpawn);

	/**
	* A method for spawning the max amount of pedestrians that can be spawned from the data file 
	* This method can be called from blueprints or code.
	*/
	UFUNCTION(BlueprintCallable, Category = "MassAI|Spawn")
	void SpawnMaxPedestrians();

	/**
	* A method used for destroying our spawned pedestrians at any point we are in the world not just end play.
	* This method can only be called from code due to the input of TConstArrayView is not accessible in blueprints.
	* 
	* @param EntitiesToDestroy: The pedestrians entity handles to destroy
	* 
	*/
	void DestroySpawnedPedestrians(TConstArrayView<FMassEntityHandle> EntitiesToDestroy);
	
	/**
	* Create the archetype for the pedestrian entity
	*/
	UFUNCTION()
	void CreatePedestrianArchetype();

	/**
	* Create the template data for the pedestrian entity archetype
	*/
	UFUNCTION()
	void CreatePedestrianTemplateData();

	/**
	 * Load the Pedestrian Data from the JSON file using agent data subsystem to get the data asynchronously
	 */
	UFUNCTION()
	void LoadPedestrianData();

	/**
	* Build the Pedestrian Movement Fragment Data
	*/
	UFUNCTION()
	void BuildPedestrianMovementFragmentData();

	/**
	* Build the Pedestrian Representation Fragment Data
	*/
	void BuildPedestrianRepresentationFragmentData(); // TODO: add inputs to allow for customisation of the pedestrian representation currently hardcoded values

	// The archetype handle for the pedestrian archetype
	FMassArchetypeHandle PedestrianArchetypeHandle;

	// The Handle for spawned pedestrians
	TArray<FMassEntityHandle> SpawnedEntityPedestrianHandles;

	// The template data for the pedestrians contains fragments and shared fragments etc
	FMassEntityTemplateData PedestrianTemplateData;

	UPROPERTY()
	FSimulationFragment SimulationFragment;
	UPROPERTY()
	FAgentRepresentationFragment AgentRepresentationFragment;
	UPROPERTY()
	FSharedStruct SharedSimulationFragment;
	UPROPERTY()
	FSharedStruct SharedAgentRepresentationFrag;

	/** Niagara Shared Rep Frag - contains all the variable that are sent to the niagara system */
	UPROPERTY()
	FSharedStruct NiagaraSharedRepFrag;
	
	FMassArchetypeSharedFragmentValues ArchetypeSharedFragmentValues;

	// The agent data subsystem for getting the data for the pedestrians
	UPROPERTY()
	UAgentDataSubsystem* AgentDataSubsystem;

	FOnPedestrianDataLoaded OnPedestrianDataReadyToSpawn; // Delegate to broadcast when the pedestrian data is loaded and processed

};
