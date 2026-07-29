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
#include "MassEntityTypes.h" // So we can use the FMassFragment
#include "EnumsAndStructs/MassAIEnums.h"
#include "EntityInfoFragment.generated.h"


/**
 * Contains information about the entity, that is useful for analytical purposes but currently holds no information
 * useful to the viewer such as:
 * - Entity ID (this is important for identifying the entity in the simulation, and is used by other fragments)
 * - Entity Name
 * - Entity SimTimeS
 * - Entity MaxSpeed
 * - Entity M_Plane
 * - Entity Map
 */
USTRUCT(BlueprintType)
struct PROJECTMOBIUS_API FEntityInfoFragment: public FMassFragment
{
	GENERATED_BODY()
	
	/** The Entity ID */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	int32 EntityID = 0;

	/** The Entity Name */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntityName = "Default[0]";

	/** The Entity simTimeS */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntitySimTimeS = "0.0";

	/** The Entity MaxSpeed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	float EntityMaxSpeed = 1.0f;

	/** The Entity M_Plane */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	FString EntityM_Plane = "F#0";

	/** Entity Map */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "EntityInfo")
	int32 EntityMap = 0;
};

/**
 * For every entity that is moving, we need to have a movement fragment, this tells the system where the
 * entity is currently located, what its current speed is, and what its current movement bracket is.
 */
USTRUCT()
struct PROJECTMOBIUS_API FEntityMovementFragment: public FMassFragment
{
	GENERATED_BODY()
	
	/** The Entity ID */
	UPROPERTY(EditAnywhere, Category = "EntityInfo")
	int32 EntityID = 0;

	/** The current location of the pedestrian */
	UPROPERTY(EditAnywhere, Category = "PedestrianMovement")
	FVector CurrentLocation = FVector::ZeroVector;

	/** The current Rotation of the pedestrian */
	UPROPERTY(EditAnywhere, Category = "PedestrianMovement")
	FRotator CurrentRotation = FRotator::ZeroRotator;

	/** Current Speed of the agent */
	UPROPERTY(EditAnywhere, Category = "PedestrianMovement")
	float CurrentSpeed = 0.0f;

	/** Gait/Directional Speed */
	UPROPERTY(EditAnywhere, Category = "PedestrianMovement")
	float GaitDirectionalSpeed = 0.0f;

	/** Current Movement Bracket */
	UPROPERTY(EditAnywhere, Category = "PedestrianMovement")
	EPedestrianMovementBracket CurrentMovementBracket  = EPedestrianMovementBracket::Emb_NotMoving;

	// Quick Fix for flow counters - when we set this fragment we need to sim time stamp it so we can use it for flow counters
	UPROPERTY(EditAnywhere, Category = "PedestrianMovement")
	float LastUpdatedSimTime = 0.0f;

	/** True when this frame's pose came from a streaming stand-in block (cold miss served another
	 *  timestep's data — cosmetic only). Time-integrating analysis must hold state on such frames;
	 *  cleared the first frame the exact block is served. */
	UPROPERTY(EditAnywhere, Category = "PedestrianMovement")
	bool bSampleApproximate = false;
};

/**
 * Niagara demographic slot routing — render dispatch only.
 * Slot order matches UNiagaraAgentRepProcessor::MapAgentCountToArray:
 * 0 = male adult, 1 = female adult, 2 = elderly male, 3 = elderly female, 4 = child.
 */
namespace MobiusNiagaraDemographics
{
	/** Number of demographic slots (and Niagara array triplets) */
	inline constexpr uint8 NumSlots = 5;

	/** Sentinel for an agent that was NOT routed into any demographic array at spawn — the extract
	 *  skips it (Slot >= NumSlots). Any value >= NumSlots works; 0xFF is chosen to be obviously invalid. */
	inline constexpr uint8 InvalidSlot = 0xFF;

	/**
	 * Compute the render dispatch slot for an agent. For the three demographics the spawn path actually
	 * routes (child / elderly / adult, each ×gender) this reproduces the legacy per-frame extract branch
	 * exactly — verified by ProjectMobius.Render.SlotDispatchMatchesBranch.
	 *
	 * Ead_Default is the ONLY case the spawn switch (AgentRepresentation_MOP::ProcessEntity) leaves
	 * unrouted: it assigns no InstanceID (stays 0) and Adds the agent to no array. Returning InvalidSlot
	 * makes the parallel extract skip it, which (1) keeps the per-(slot,InstanceID) write targets truly
	 * unique so ParallelFor is race-free by construction, and (2) stops an unrouted agent aliasing the
	 * real adult holding InstanceID 0. The legacy sequential extract instead wrote such an agent into the
	 * adult arrays at index 0 (deterministic last-writer-wins) — a latent bug that is currently
	 * unreachable (no runtime path assigns Ead_Default; fragment default is Ead_Adult). Skipping it is a
	 * defensive hardening of that unreachable path, so reachable output is unchanged (Invariant 4).
	 */
	FORCEINLINE uint8 ComputeSlot(const bool bIsMale, const EAgeDemographic AgeDemographic)
	{
		if (AgeDemographic == EAgeDemographic::Ead_Child)
		{
			return 4;
		}
		if (AgeDemographic == EAgeDemographic::Ead_Elderly)
		{
			return bIsMale ? 2 : 3;
		}
		if (AgeDemographic == EAgeDemographic::Ead_Adult)
		{
			return bIsMale ? 0 : 1;
		}
		// Ead_Default (or any future unrouted demographic): not rendered — see doc comment above
		return InvalidSlot;
	}
}

/**
 * For every entity that is rendered, we need to have a rendering fragment, this tells the system whether the
 * entity should be rendered, and if so what properties it has, such as:
 * - what instance ID it is associated with
 * - the gender of the agent
 * - the age demographic of the agent
 * - whether it is ready to be destroyed
 * - whether the animation has changed
 */
USTRUCT()
struct PROJECTMOBIUS_API FEntityRenderingFragment: public FMassFragment
{
	GENERATED_BODY()

	/** The Entity ID */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	int32 EntityID = 0;

	/** Should this agent be rendered */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	bool bRenderAgent = true;

	/** The Instance ID associated for this Entity */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	int32 InstanceID = 0;

	/** Agent Gender */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	bool bIsMale = true;

	/** Agent Age Demographic */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	EAgeDemographic AgeDemographic = EAgeDemographic::Ead_Adult;

	/** Ready to be destroyed */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	bool bReadyToDestroy = false;
	
	/** Animation Changed */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	bool bAnimationChanged = false;

	/** show this pedestrian details */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	uint8 showPedestrianStats = 0;// 0 = don't show, 1 = show, TODO: 2 = show and highlight

	/**
	 * Render-only camera visibility (default true = drawn). Written by the camera cull (B7),
	 * read by EXACTLY ONE consumer: the Niagara W term. NEVER an analysis gate — analysis gates
	 * on bRenderAgent above (sample presence + tenability), which culling must never touch.
	 */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	bool bVisibleToCamera = true;

	/**
	 * Render-only tenability hide (default false = drawn). Set true while an agent has passed its
	 * tenability-failure time so its mesh stops drawing and the in-world fail marker stands in for it.
	 *
	 * Deliberately a SECOND flag rather than clearing bRenderAgent, which is overloaded as the
	 * analysis gate and would break four unrelated readers at once:
	 *   - AgentEgressHealthCalculationProcessor: stops computing dose and the failure projection
	 *   - AgentEgressHealthProcessor: drops the agent from the published snapshot -> NO fail marker,
	 *     i.e. hiding the agent would delete the very thing meant to replace it
	 *   - AgentHeatmapProcessor: stops accumulating
	 *   - NiagaraAgentRepProcessor: bReadyToDestroy = !bRenderAgent, so the entity is marked for
	 *     destruction rather than merely hidden
	 * Same render-only contract as bVisibleToCamera above, but a separate flag because that one is
	 * reserved for the camera cull (B7) — one flag with two writers in different processors would be
	 * a last-write-wins race.
	 *
	 * Read by EXACTLY ONE consumer: the Niagara W term, ANDed with bRenderAgent and bVisibleToCamera.
	 * NEVER an analysis gate. Derived state, not a latch — PedestrianMovementProcessor clears it on
	 * the not-failed path so scrubbing back before the failure time re-shows the agent.
	 */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	bool bHiddenByTenabilityFailure = false;

	/** Demographic dispatch slot for the Niagara arrays, set once at spawn alongside InstanceID
	 *  (see MobiusNiagaraDemographics::ComputeSlot) */
	UPROPERTY(EditAnywhere, Category = "PedestrianRendering")
	uint8 NiagaraDemographicSlot = 0;
};

/**
 * Collision Fragment, this tells the system whether the entity has collided with something, and if so what or
 * to provide useable events such as: clicks, hover, etc.
 */
USTRUCT()
struct PROJECTMOBIUS_API FEntityCollisionFragment: public FMassFragment //TODO: this may want to be a FObjectWrapperFragment ?? TBD
{
	GENERATED_BODY()

	/**
	 * 
	 */
	UPROPERTY()
	TWeakObjectPtr<class UCapsuleComponent> Capsule = nullptr;
};
