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
#include "MassEntitySubsystem.h"
#include "EnumsAndStructs/AgentMeshViewer.h"
#include "Subsystems/WorldSubsystem.h"
#include "StatisticSubsystem.generated.h"

struct FFlowCounterData;
class AFlowCounter;
class UStatisticSubsystem;
/**
 * Delegates to broadcast information changes
 */
DECLARE_MULTICAST_DELEGATE(FOnAgentInfoChanged);
DECLARE_MULTICAST_DELEGATE(FOnSelectedAgentInfoChanged);

/*
 * The TMassExternalSubsystemTraits is required for this subsystem so it can be used with mass entity
 * i.e. the representation processor that calls on this subsystem
 */
template<>
struct TMassExternalSubsystemTraits<UStatisticSubsystem> final
{
	enum
	{
		ThreadSafeRead = true,
		ThreadSafeWrite = false,
		GameThreadOnly = true, // needs to be game thread as we calling rendering api
	};
};

/** TODO: this will be used to pull logic from the heatmap subsystem and the floor stats widget so only one subsystem is used
 * and provide a way to break one of the circular dependencies
 * This subsystem will be used to perform calculations and statistics gathering for user interfaces
 */
UCLASS()
class MOBIUSCORE_API UStatisticSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UStatisticSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	virtual void Deinitialize() override;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/**
	 * Updates the internal mesh data with the provided agent information and notifies listeners of changes.
	 *
	 * @param AgentData The array of FAgentMeshViewer objects representing the updated agent information,
	 *                  including details such as position, speed, demographic, and other attributes.
	 */
	void UpdateAgentInfoMeshData(const TArray<FAgentMeshViewer>& AgentData);

	/**
	 * Updates the data of the currently selected agent with the provided agent information
	 * and broadcasts a notification to listeners about the change.
	 *
	 * @param AgentData A reference to an FAgentMeshViewer object containing updated
	 *                  details about the selected agent, such as position, speed,
	 *                  demographic information, and other related attributes.
	 */
	void UpdateSelectedAgentData(const FAgentMeshViewer& AgentData);

	/**
	 * Updates the data of the currently hovered agent with the provided agent information
	 * and broadcasts a notification to listeners about the change.
	 *
	 * @param AgentData A reference to an FAgentMeshViewer object containing updated
	 *                  details about the hovered agent, such as position, speed,
	 *                  demographic information, and other related attributes.
	 */
	void UpdateHoveredAgentData(const FAgentMeshViewer& AgentData);


	/**
	 * Retrieves the mesh data for all agents currently stored within the subsystem.
	 *
	 * @return An array of FAgentMeshViewer objects, each representing an agent's information such as position, speed, demographic, and other attributes.
	 */
	TArray<FAgentMeshViewer> GetAgentInfoMeshData();

	/**
	 * Retrieves the mesh data for the currently selected agent.
	 *
	 * @return An FAgentMeshViewer object containing information about the currently selected agent, including
	 *         attributes such as position, speed, demographic, and other relevant details.
	 */
	FAgentMeshViewer GetSelectedAgentInfoMeshData();

	/**
	 * Retrieves the mesh data for the agent that is currently hovered over.
	 *
	 * @return An FAgentMeshViewer object containing detailed information about the hovered agent, including attributes
	 *         such as position, speed, demographic, and other relevant details.
	 */
	FAgentMeshViewer GetHoveredAgentInfoMeshData();

	/** */
	void AddFlowCounter(AFlowCounter* FlowCounter);
	
	/** */
	void RemoveFlowCounter(AFlowCounter* FlowCounter);

	/**  */
	bool IsAgentLocationInAFlowCounterBand(const FVector& AgentLocation) const;

	/** */
	void SendDataToFlowCounter(UE::TConsumeAllMpmcQueue<FFlowCounterData>& FlowData, int32 FlowCounterIndex = 0);

	FOnAgentInfoChanged OnAgentInfoChanged; // Delegate to notify when agent info changes
	FOnSelectedAgentInfoChanged OnSelectedAgentInfoChanged; // Delegate to notify when selected agent info changes

private:
	TArray<FAgentMeshViewer> PedestrianAgentData = TArray<FAgentMeshViewer>(); // Holds the current agent data for the mesh viewer
	FAgentMeshViewer SelectedAgentData = FAgentMeshViewer(); // Holds the currently selected agent data for the mesh viewer
	FAgentMeshViewer HoveredAgentData = FAgentMeshViewer(); // Holds the currently selected agent data for the mesh viewer

	/** Reference to the FlowCounter actor, if needed for statistics gathering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StatisticSubsystem|FlowCounter", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<AFlowCounter>> FlowCounters = TArray<TObjectPtr<AFlowCounter>>();

public:
	// GETTERS AND SETTERS
	/**
	 * Retrieves the reference to the FlowCounter actor associated with the world.
	 *
	 * @return A pointer to the AFlowCounter actor if it exists, otherwise nullptr.
	 */
	FORCEINLINE TArray<TObjectPtr<AFlowCounter>> GetFlowCounters() { return FlowCounters; }
	
};
