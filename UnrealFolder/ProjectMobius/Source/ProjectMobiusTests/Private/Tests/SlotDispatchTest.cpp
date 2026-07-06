// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// SlotDispatchTest.cpp
//
// Guards the B4/B5 Niagara demographic slot dispatch. The legacy per-frame extract in
// UNiagaraAgentRepProcessor branched on (bIsMale, AgeDemographic) to pick a demographic's array
// triplet; B4 routes each agent ONCE at spawn (AgentRepresentation_MOP) through
// MobiusNiagaraDemographics::ComputeSlot and B5 dispatches by that slot. The slot value must
// reproduce the legacy branch for every (gender x age) combination or agents write into the wrong
// demographic's arrays (wrong mesh/animation, or an out-of-bounds InstanceID against a shorter array).
//
// The expected values below transcribe the legacy extract branch for every demographic the spawn path
// actually routes: Ead_Child -> Children arrays; Ead_Elderly -> ElderlyMale/ElderlyFemale by gender;
// Ead_Adult -> MaleAdult/FemaleAdult by gender. Slot order matches
// UNiagaraAgentRepProcessor::MapAgentCountToArray:
//   0 = male adult, 1 = female adult, 2 = elderly male, 3 = elderly female, 4 = child.
//
// Ead_Default is the one demographic the spawn switch leaves UNROUTED (no InstanceID, no array Add), so
// ComputeSlot returns InvalidSlot (>= NumSlots) and the extract skips it — hardening a latent, currently
// unreachable index-0 aliasing/race hazard. Reachable demographics are unchanged (Invariant 4). The two
// Ead_Default cases below therefore expect InvalidSlot, NOT the adult slot.
//
// Run from the Session Frontend (search "ProjectMobius.Render") or:
//   UnrealEditor ProjectMobius.uproject
//     -ExecCmds="Automation RunTests ProjectMobius.Render.SlotDispatchMatchesBranch" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "MassAI/Fragments/EntityInfoFragment.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSlotDispatchMatchesBranchTest,
	"ProjectMobius.Render.SlotDispatchMatchesBranch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlotDispatchMatchesBranchTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		bool bIsMale;
		EAgeDemographic Age;
		uint8 ExpectedSlot; // the array triplet the legacy extract branch picked
		const TCHAR* What;
	};

	const FCase Cases[] =
	{
		{ true,  EAgeDemographic::Ead_Child,   4, TEXT("male child -> Children arrays") },
		{ true,  EAgeDemographic::Ead_Elderly, 2, TEXT("male elderly -> ElderlyMale arrays") },
		{ true,  EAgeDemographic::Ead_Adult,   0, TEXT("male adult -> MaleAdult arrays") },
		// Ead_Default is unrouted at spawn -> InvalidSlot -> extract skips it
		{ true,  EAgeDemographic::Ead_Default, MobiusNiagaraDemographics::InvalidSlot, TEXT("male default -> InvalidSlot (unrouted, skipped)") },
		{ false, EAgeDemographic::Ead_Child,   4, TEXT("female child -> Children arrays") },
		{ false, EAgeDemographic::Ead_Elderly, 3, TEXT("female elderly -> ElderlyFemale arrays") },
		{ false, EAgeDemographic::Ead_Adult,   1, TEXT("female adult -> FemaleAdult arrays") },
		{ false, EAgeDemographic::Ead_Default, MobiusNiagaraDemographics::InvalidSlot, TEXT("female default -> InvalidSlot (unrouted, skipped)") },
	};

	static_assert(UE_ARRAY_COUNT(Cases) == 8, "every (gender x age) combination must be covered");

	for (const FCase& Case : Cases)
	{
		const uint8 Slot = MobiusNiagaraDemographics::ComputeSlot(Case.bIsMale, Case.Age);

		TestEqual(Case.What, Slot, Case.ExpectedSlot);

		// A routed demographic must land in a valid dispatch-table index (an out-of-range slot would
		// read past the B5 pointer tables); the unrouted Ead_Default must be >= NumSlots so the extract
		// skips it rather than aliasing a real agent's element.
		if (Case.Age == EAgeDemographic::Ead_Default)
		{
			TestTrue(FString::Printf(TEXT("%s: slot %u >= NumSlots (skipped)"), Case.What, Slot),
				Slot >= MobiusNiagaraDemographics::NumSlots);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("%s: slot %u < NumSlots"), Case.What, Slot),
				Slot < MobiusNiagaraDemographics::NumSlots);
		}
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
