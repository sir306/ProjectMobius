// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

// LANDED 2026-08-11 (session 2) in MobiusCore, not the MobiusDataImporter plugin its staging header
// named: the only consumer is FAssimpMeshLoaderRunnable's IFC branch (MobiusCore), reached from
// ARuntimeMeshBuilder. Paths:
//   Source\MobiusCore\Public\Ifc\MobiusIfcRenderableClasses.h
//   Source\MobiusCore\Private\Ifc\MobiusIfcRenderableClasses.cpp
// Caller that consumes the verdicts and emits the single summary log line:
//   Source\MobiusCore\Private\Ifc\MobiusIfcMeshLoader.cpp
//
// ============================================================================================
// WHY THIS EXISTS
// ============================================================================================
// IFC++'s getShapeInputData() hands back EVERY product that carries a shape -- including solids
// that must NEVER be drawn:
//   - IfcOpeningElement: the void volumes subtracted from walls (door/window holes). Render one
//     and you fill the hole back in with a solid block.
//   - IfcSpace: room-filling solids. Render one and every room becomes an opaque box. IfcSpace
//     IS consumed elsewhere (B-RISK room-polygon path) -- just never as a render mesh. See
//     IsRoomVolumeClass() below, which is the deliberately separate door for that consumer.
// This file is an ALLOWLIST, not a denylist. A denylist silently admits every new class the next
// IFC file happens to contain (a schema has hundreds of entity types; nobody has enumerated them
// all here). An allowlist instead DROPS anything unrecognised -- loudly, via the stats struct --
// so a missing element shows up as a one-line summary instead of a silently wrong render or a
// silently missing one.
//
// ============================================================================================
// EMPIRICAL CLASS CENSUS (ground truth measured from the two Mobius test files)
// ============================================================================================
// IFC2X3 ISO-Test-1-2x3.ifc, products WITH geometry (44 total):
//   24 IfcBuildingElementProxy, 7 IfcOpeningElement, 6 IfcWindow, 4 IfcWallStandardCase,
//    2 IfcSlab, 1 IfcDoor
// IFC4X3_ADD2 ISO-Test-8-FireSmoke.ifc, products WITH geometry (205 total):
//   76 IfcBuildingElementProxy, 36 IfcOpeningElement, 26 IfcWall, 19 IfcWindow, 17 IfcSensor,
//   14 IfcDoor, 14 IfcSpace, 2 IfcSlab, 1 IfcGeographicElement
// Products with NO geometry in either file (spatial containers + *Type/*Style declarations --
// see NonRepresentableClassNames below): IfcProject, IfcSite, IfcBuilding, IfcBuildingStorey,
// IfcRoof, and the *Type/*Style entities. NOTE: IfcRoof is classified Render below despite having
// zero instances-with-geometry in these two files -- by spec it IS an IfcBuildingElement and can
// carry its own shape in other models; these two ISO conformance files simply don't exercise it.
// It is NOT added to NonRepresentableClassNames (that set is for classes that can never carry a
// shape at all, which IfcRoof is not).
//
// Expected renderable counts (verify against these when wiring up the caller/test):
//   IFC2X3:  44 total - 7 IfcOpeningElement (VolumeOnly)                          = 37 renderable
//            (24 BuildingElementProxy + 6 Window + 4 WallStandardCase + 2 Slab + 1 Door = 37)
//   IFC4X3: 205 total - 36 IfcOpeningElement - 14 IfcSpace (VolumeOnly, sum 50)   = 155 baseline
//            renderable-if-annotations-counted; MINUS 17 IfcSensor + 1 IfcGeographicElement
//            (Annotation class, see bMobiusIfcRenderAnnotationClasses below) = 137 DEFAULT
//            renderable with annotations off.
//            (76 BuildingElementProxy + 26 Wall + 19 Window + 14 Door + 2 Slab = 137)
//            137 + 50 + 18 = 205 -- every product in the census is accounted for, Unknown = 0.
//
// ============================================================================================
// FOUR VERDICTS (not a bool -- a bare true/false loses the reason a drop happened)
// ============================================================================================
//   Render      draw it.
//   VolumeOnly  has solid geometry that must NEVER be drawn (IfcOpeningElement, IfcSpace, ...).
//   Annotation  equipment/marker proxy geometry (IfcSensor, IfcGeographicElement); OWNER POLICY
//               decides whether these are drawn -- see bMobiusIfcRenderAnnotationClasses.
//   Unknown     not in any list above -- DROP, but count and name it loudly. This is the case
//               that catches a new schema's new class instead of silently rendering or silently
//               losing it. (Known-shapeless classes -- spatial containers, *Type/*Style -- are
//               deliberately kept OUT of the loud per-name report; see IsKnownNonRepresentableClass
//               and FMobiusIfcClassStats::Record.)
//
// ============================================================================================
// DATA STRUCTURE CHOICE: sorted static TCHAR* arrays + binary search over FStringView
// ============================================================================================
// Chose this over a `static const TSet<FString>` because:
//   1. Zero heap allocation, even at static-init time -- the arrays are compile-time string
//      literal pointers, so there is no static-initialization-order risk across module/DLL
//      boundaries (a real hazard for a `static const TSet<FString>` function-local or namespace-
//      scope global built from FString, which allocates on first touch).
//   2. Callers query with FStringView, so a per-product classification never constructs an FString
//      just to do the lookup -- fully allocation-free on the hot path, not merely "O(1)-ish".
//   3. At the scale here (fewer than 90 distinct names across every list combined) a ~7-compare
//      binary search is indistinguishable from TSet's O(1) hashing in practice, and the flat
//      arrays are cache-friendlier (contiguous, fits comfortably in L1) than a hash table's
//      bucket-and-tombstone layout.
// MOBIUSCORE_API on the free functions and on FMobiusIfcClassStats is load-bearing, not decoration:
// these are called from ProjectMobiusTests (a separate module, hence a separate DLL), and without the
// export they link inside MobiusCore and fail with LNK2019 in the test module only -- which is a
// failure the Editor target catches and the Game target does not, since ProjectMobiusTests is only in
// the Editor target's ExtraModuleNames. Hit for real on 2026-08-12.
//
// This header has NO dependency on IFC++ or the C shim -- it operates purely on the class-name
// string IFC++ reports via EntityFactory::getStringForClassID(), case-sensitive exact match
// (e.g. "IfcWall"). Includable from a module with bEnableExceptions = false and no RTTI: no
// exceptions, no dynamic_cast, no UHT/reflection macros (this is a plain-C++ lookup utility, not
// a UObject, so it carries no .generated.h dependency).
//
// ============================================================================================
// HARD RULE: NO UE_LOG (or on-screen debug) IN ANY PER-PRODUCT PATH
// ============================================================================================
// This is a real Mobius project rule that has been violated before: no UE_LOG or on-screen debug
// in tick-path or latency-sensitive code. ClassifyClass() and FMobiusIfcClassStats::Record() only
// accumulate counts -- they never log. The CALLER logs exactly ONE summary line, built from
// FMobiusIfcClassStats::Summarize(), AFTER the load completes and OUTSIDE the per-product loop.
//
// ============================================================================================
// OWNER-POLICY DECISIONS LEFT OPEN IN THIS FILE (flag for confirmation, do not silently resolve)
// ============================================================================================
//   - bMobiusIfcRenderAnnotationClasses (below): should IfcSensor / IfcGeographicElement marker
//     geometry actually be drawn? Defaulted to false. Flip this one constant once decided.
//   - AnnotationClassNames currently holds only IfcSensor + IfcGeographicElement (the two seen in
//     the test files). Real models may carry other equipment/marker proxies (IfcAlarm,
//     IfcController, IfcFlowInstrumentElement, ...) -- until confirmed one way or the other they
//     will fall through to Unknown and be reported, which is the safe default for something new.

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"

namespace MobiusIfc
{
	/** Verdict for one IFC class name, as reported by IFC++'s EntityFactory::getStringForClassID(). */
	enum class ERenderVerdict : uint8
	{
		/** Draw it. */
		Render,

		/** Has solid geometry that must NEVER be drawn (IfcOpeningElement, IfcSpace, ...). */
		VolumeOnly,

		/** Equipment/marker proxy geometry; see bMobiusIfcRenderAnnotationClasses. */
		Annotation,

		/** Not in any list above. Dropped, counted, and named -- never silently swallowed. */
		Unknown,
	};

	/**
	 * OWNER POLICY -- UNRESOLVED as of 2026-08-11: should Annotation-verdict classes (IfcSensor,
	 * IfcGeographicElement) actually be rendered? Defaulted to false (not drawn) until an owner
	 * confirms either way. Flip this single constant -- do not bury the choice by special-casing
	 * callers. FMobiusIfcClassStats tracks both AnnotationCount (always) and
	 * AnnotationRenderedCount (only non-zero once this is true) so flipping it is visible in the
	 * summary line.
	 */
	inline constexpr bool bMobiusIfcRenderAnnotationClasses = false;

	/**
	 * Classify one IFC class name. Case-sensitive exact match against the string IFC++ reports.
	 * O(log n) over n < 90 total known names, allocation-free (FStringView in, no FString built).
	 * Safe to call once per product returned by getShapeInputData() in the hot import-time loop.
	 */
	MOBIUSCORE_API ERenderVerdict ClassifyClass(FStringView ClassName);

	/**
	 * True only for IfcSpace. Separate from ClassifyClass() on purpose: IfcSpace is VolumeOnly for
	 * rendering (never drawn as a mesh) but MUST still be reachable for room-polygon consumption
	 * (B-RISK path). This is the door that lets that consumer pick it up without weakening the
	 * render filter -- do not make Render/VolumeOnly do double duty for this.
	 */
	MOBIUSCORE_API bool IsRoomVolumeClass(FStringView ClassName);

	/**
	 * True for classes that structurally never carry a shape at all: spatial-structure containers
	 * (IfcProject, IfcSite, IfcBuilding, IfcBuildingStorey) and *Type/*Style/IfcTypeProduct type
	 * declarations. In the normal getShapeInputData()-driven pipeline these should never reach
	 * ClassifyClass() (they have no shape to be enumerated by), but if a caller ever classifies the
	 * full product list instead (e.g. a future diagnostic pass), checking this FIRST keeps them out
	 * of the loud "unknown class" report as false alarms, without silently discarding them --
	 * FMobiusIfcClassStats::Record() still counts them, in KnownNonRepresentableCount, separate
	 * from the genuine-surprise UnknownCount/DroppedUnknownClasses.
	 */
	MOBIUSCORE_API bool IsKnownNonRepresentableClass(FStringView ClassName);

	/**
	 * Accumulates verdict counts across one IFC import. The caller owns one instance per import,
	 * calls Record() per product inside the hot loop (no logging in there -- see header comment),
	 * then calls Summarize() exactly once after the loop to produce the single log line.
	 */
	struct MOBIUSCORE_API FMobiusIfcClassStats
	{
		int32 RenderCount = 0;
		int32 VolumeOnlyCount = 0;
		int32 AnnotationCount = 0;

		/** Non-zero only once bMobiusIfcRenderAnnotationClasses is flipped true. */
		int32 AnnotationRenderedCount = 0;

		/** Genuine surprises only -- a new schema's new class. Drives the loud report. */
		int32 UnknownCount = 0;

		/** Quiet counterpart to UnknownCount: spatial containers / *Type/*Style, expected shapeless. */
		int32 KnownNonRepresentableCount = 0;

		/** Dropped-class name -> count, for GENUINE Unknown verdicts only (never false-alarms
		 *  known-non-representable classes into this map -- see IsKnownNonRepresentableClass). */
		TMap<FString, int32> DroppedUnknownClasses;

		/**
		 * Record one product's verdict. Allocation-free for Render, VolumeOnly, Annotation and for
		 * known-non-representable classes -- i.e. for every product in a well-formed file.
		 *
		 * It DOES allocate on every Unknown-verdict call, not merely the first one per distinct name:
		 * `DroppedUnknownClasses.FindOrAdd(FString(ClassName))` has to materialise the FString key
		 * before TMap can look it up, and TMap has no heterogeneous FStringView lookup to avoid that.
		 * So a file containing many instances of one unrecognised class allocates and frees a short
		 * string per instance. Accepted deliberately: Unknown means the allowlist has already failed
		 * to account for something and the load is about to be reported as suspect, so the cost is
		 * bounded by how wrong the file is, and it buys a precise per-class dropped-count report.
		 * If a real file ever makes this hot, add a small FStringView-keyed probe before the FindOrAdd.
		 */
		void Record(ERenderVerdict Verdict, FStringView ClassName);

		/** One-line human-readable summary. Call exactly once, after the import loop, never inside it. */
		FString Summarize() const;
	};
}
