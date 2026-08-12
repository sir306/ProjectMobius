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
//   0 = male adult, 1 = female adult, 2 = elderly male, 3 = elderly female, 4 = child,
//   5 = empty wheelchair.
//
// Slot 5 is NOT reachable from ComputeSlot and so does not appear in the cases below — it is
// dispatched by FEntityRenderingFragment::MobilityAid, additively, on top of the human slot. See
// FWheelchairSlotDispatchTest at the bottom of this file.
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

// ---------------------------------------------------------------------------------------------
// Wheelchair chair-slot dispatch.
//
// A wheelchair agent is the first thing in this system to render from TWO slots at once: its HUMAN
// from its own demographic slot (0-4, via ComputeSlot exactly as before) and an empty CHAIR from
// WheelchairSlot. The chair is dispatched by FEntityRenderingFragment::MobilityAid in the
// extract, NOT by ComputeSlot — which is why the eight cases above are untouched by this feature.
//
// The invariant worth guarding is that those two dispatch routes can never collide: if the chair
// slot ever equalled a slot ComputeSlot can return, chair transforms would overwrite that
// demographic's human transforms at matching indices, silently deleting agents from the render.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWheelchairSlotDispatchTest,
	"ProjectMobius.Render.WheelchairSlotDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWheelchairSlotDispatchTest::RunTest(const FString& Parameters)
{
	// The chair slot must be a real dispatch-table index — the B5 pointer tables are sized NumSlots.
	TestTrue(TEXT("WheelchairSlot is a valid dispatch index"),
		MobiusNiagaraDemographics::WheelchairSlot < MobiusNiagaraDemographics::NumSlots);

	// ...and must not collide with ANY slot ComputeSlot can hand back for a human.
	const EAgeDemographic Ages[] = {
		EAgeDemographic::Ead_Child, EAgeDemographic::Ead_Elderly,
		EAgeDemographic::Ead_Adult, EAgeDemographic::Ead_Default };

	for (const bool bIsMale : { true, false })
	{
		for (const EAgeDemographic Age : Ages)
		{
			const uint8 HumanSlot = MobiusNiagaraDemographics::ComputeSlot(bIsMale, Age);
			TestTrue(
				FString::Printf(TEXT("human slot %u must differ from the chair slot %u"),
					HumanSlot, MobiusNiagaraDemographics::WheelchairSlot),
				HumanSlot != MobiusNiagaraDemographics::WheelchairSlot);
		}
	}

	// The unrouted sentinel must still sit outside the (now larger) slot range, or an unrouted agent
	// would start aliasing the chair arrays instead of being skipped.
	TestTrue(TEXT("InvalidSlot still >= NumSlots after the chair slot was added"),
		MobiusNiagaraDemographics::InvalidSlot >= MobiusNiagaraDemographics::NumSlots);

	// Fragment defaults: an agent is not a wheelchair user and has no chair until the name parser
	// says otherwise. Ema_None must be the zero value so a zeroed fragment cannot invent a chair.
	const FEntityRenderingFragment DefaultFragment;
	TestEqual(TEXT("MobilityAid defaults to Ema_None"),
		static_cast<int32>(DefaultFragment.MobilityAid), static_cast<int32>(EMobilityAid::Ema_None));
	TestEqual(TEXT("Ema_None is the zero value, so a zeroed fragment cannot invent a chair"),
		static_cast<int32>(EMobilityAid::Ema_None), 0);
	TestEqual(TEXT("ChairInstanceID defaults to -1 (no chair)"),
		DefaultFragment.ChairInstanceID, -1);

	return true;
}

#endif // !UE_BUILD_SHIPPING
