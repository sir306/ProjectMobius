// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

// See Ifc/MobiusIfcRenderableClasses.h for the full design rationale, the empirical class census,
// and the expected-count arithmetic.

#include "Ifc/MobiusIfcRenderableClasses.h"

namespace
{
	using namespace MobiusIfc;

	/**
	 * Ordinal (TCHAR-code-point) comparison of an FStringView against a null-terminated literal,
	 * with no FString construction on either side. Matches the ordering used to hand-sort every
	 * array below, so binary search over those arrays stays correct.
	 */
	int32 CompareViewToLiteral(FStringView View, const TCHAR* Other)
	{
		const TCHAR* A = View.GetData();
		const int32 ALen = View.Len();
		int32 Index = 0;
		for (; Index < ALen && Other[Index] != TEXT('\0'); ++Index)
		{
			if (A[Index] != Other[Index])
			{
				return A[Index] < Other[Index] ? -1 : 1;
			}
		}
		if (Index < ALen)
		{
			return 1; // View has more characters left over -> View > Other.
		}
		if (Other[Index] != TEXT('\0'))
		{
			return -1; // Other has more characters left over -> View < Other.
		}
		return 0;
	}

	/** Binary search a hand-sorted (ordinal, ascending) literal array. Allocation-free. */
	bool ContainsClassName(FStringView ClassName, const TCHAR* const* SortedNames, int32 Count)
	{
		int32 Lo = 0;
		int32 Hi = Count - 1;
		while (Lo <= Hi)
		{
			const int32 Mid = Lo + (Hi - Lo) / 2;
			const int32 Cmp = CompareViewToLiteral(ClassName, SortedNames[Mid]);
			if (Cmp == 0)
			{
				return true;
			}
			if (Cmp < 0)
			{
				Hi = Mid - 1;
			}
			else
			{
				Lo = Mid + 1;
			}
		}
		return false;
	}

	// ---------------------------------------------------------------------------------------
	// Render: solid building elements + common MEP flow elements. 48 entries. Sorted ordinally --
	// keep it that way or ContainsClassName's binary search silently starts missing entries.
	// Covers every WITH-geometry class seen in both census files (IfcBuildingElementProxy,
	// IfcWindow, IfcWallStandardCase/IfcWall, IfcSlab, IfcDoor) plus the broader real-world set:
	// real IFC models are not both rectangular Revit-architecture-only exports like the two test
	// files, so columns/beams/stairs/ramps/MEP terminals etc. are included pre-emptively.
	// ---------------------------------------------------------------------------------------
	const TCHAR* const RenderClassNames[] = {
		TEXT("IfcAirTerminal"),
		TEXT("IfcBeam"),
		TEXT("IfcBeamStandardCase"),
		TEXT("IfcBuildingElementProxy"),
		TEXT("IfcBuiltElement"),
		TEXT("IfcCableCarrierSegment"),
		TEXT("IfcChimney"),
		TEXT("IfcColumn"),
		TEXT("IfcColumnStandardCase"),
		TEXT("IfcCovering"),
		TEXT("IfcCurtainWall"),
		TEXT("IfcDoor"),
		TEXT("IfcDoorStandardCase"),
		TEXT("IfcDuctFitting"),
		TEXT("IfcDuctSegment"),
		TEXT("IfcElectricAppliance"),
		TEXT("IfcFan"),
		TEXT("IfcFireSuppressionTerminal"),
		TEXT("IfcFlowTerminal"),
		TEXT("IfcFooting"),
		TEXT("IfcFurnishingElement"),
		TEXT("IfcFurniture"),
		TEXT("IfcLightFixture"),
		TEXT("IfcMember"),
		TEXT("IfcMemberStandardCase"),
		TEXT("IfcPile"),
		TEXT("IfcPipeFitting"),
		TEXT("IfcPipeSegment"),
		TEXT("IfcPlate"),
		TEXT("IfcPlateStandardCase"),
		TEXT("IfcRailing"),
		TEXT("IfcRamp"),
		TEXT("IfcRampFlight"),
		TEXT("IfcRoof"),
		TEXT("IfcSanitaryTerminal"),
		TEXT("IfcShadingDevice"),
		TEXT("IfcSlab"),
		TEXT("IfcSlabElementedCase"),
		TEXT("IfcSlabStandardCase"),
		TEXT("IfcSpaceHeater"),
		TEXT("IfcStair"),
		TEXT("IfcStairFlight"),
		TEXT("IfcValve"),
		TEXT("IfcWall"),
		TEXT("IfcWallElementedCase"),
		TEXT("IfcWallStandardCase"),
		TEXT("IfcWindow"),
		TEXT("IfcWindowStandardCase"),
	};

	// ---------------------------------------------------------------------------------------
	// VolumeOnly: has solid geometry, must NEVER be drawn. 11 entries, sorted ordinally.
	// IfcOpeningElement (7 / 36 in the two census files) and IfcSpace (14 in IFC4X3) are the two
	// that matter today; the rest are the same category by IFC-spec definition (voids, virtual
	// boundary elements, spatial zoning) even though the test files carry zero of them.
	// ---------------------------------------------------------------------------------------
	const TCHAR* const VolumeOnlyClassNames[] = {
		TEXT("IfcAnnotation"),
		TEXT("IfcFeatureElementSubtraction"),
		TEXT("IfcGrid"),
		TEXT("IfcOpeningElement"),
		TEXT("IfcOpeningStandardCase"),
		TEXT("IfcProxy"),
		TEXT("IfcSpace"),
		TEXT("IfcSpatialZone"),
		TEXT("IfcVirtualElement"),
		TEXT("IfcVoidingFeature"),
		TEXT("IfcZone"),
	};

	// ---------------------------------------------------------------------------------------
	// Annotation: equipment/marker proxy geometry. OWNER POLICY unresolved -- see
	// bMobiusIfcRenderAnnotationClasses in the header. 2 entries today (both seen in the IFC4X3
	// census: 17 IfcSensor, 1 IfcGeographicElement); real models may carry more equipment-proxy
	// classes (IfcAlarm, IfcController, IfcFlowInstrumentElement, ...) that are deliberately left
	// OFF this list until confirmed -- they fall through to Unknown, which is the safe default for
	// an unconfirmed new class.
	// ---------------------------------------------------------------------------------------
	const TCHAR* const AnnotationClassNames[] = {
		TEXT("IfcGeographicElement"),
		TEXT("IfcSensor"),
	};

	// ---------------------------------------------------------------------------------------
	// Known-non-representable: spatial-structure containers + *Type/*Style/IfcTypeProduct type
	// declarations. These never carry a shape by IFC-spec definition (not merely "had none in our
	// two test files" -- unlike IfcRoof, see the header comment). 16 entries, sorted ordinally.
	// ---------------------------------------------------------------------------------------
	const TCHAR* const NonRepresentableClassNames[] = {
		TEXT("IfcBuilding"),
		TEXT("IfcBuildingElementProxyType"),
		TEXT("IfcBuildingStorey"),
		TEXT("IfcDoorStyle"),
		TEXT("IfcDoorType"),
		TEXT("IfcGeographicElementType"),
		TEXT("IfcProject"),
		TEXT("IfcRoofType"),
		TEXT("IfcSensorType"),
		TEXT("IfcSite"),
		TEXT("IfcSlabType"),
		TEXT("IfcSpaceType"),
		TEXT("IfcTypeProduct"),
		TEXT("IfcWallType"),
		TEXT("IfcWindowStyle"),
		TEXT("IfcWindowType"),
	};

	const TCHAR* RoomVolumeClassName = TEXT("IfcSpace");
}

namespace MobiusIfc
{
	ERenderVerdict ClassifyClass(FStringView ClassName)
	{
		if (ContainsClassName(ClassName, RenderClassNames, UE_ARRAY_COUNT(RenderClassNames)))
		{
			return ERenderVerdict::Render;
		}
		if (ContainsClassName(ClassName, VolumeOnlyClassNames, UE_ARRAY_COUNT(VolumeOnlyClassNames)))
		{
			return ERenderVerdict::VolumeOnly;
		}
		if (ContainsClassName(ClassName, AnnotationClassNames, UE_ARRAY_COUNT(AnnotationClassNames)))
		{
			return ERenderVerdict::Annotation;
		}
		return ERenderVerdict::Unknown;
	}

	bool IsRoomVolumeClass(FStringView ClassName)
	{
		return CompareViewToLiteral(ClassName, RoomVolumeClassName) == 0;
	}

	bool IsKnownNonRepresentableClass(FStringView ClassName)
	{
		return ContainsClassName(ClassName, NonRepresentableClassNames, UE_ARRAY_COUNT(NonRepresentableClassNames));
	}

	void FMobiusIfcClassStats::Record(ERenderVerdict Verdict, FStringView ClassName)
	{
		switch (Verdict)
		{
		case ERenderVerdict::Render:
			++RenderCount;
			break;

		case ERenderVerdict::VolumeOnly:
			++VolumeOnlyCount;
			break;

		case ERenderVerdict::Annotation:
			++AnnotationCount;
			if (bMobiusIfcRenderAnnotationClasses)
			{
				++AnnotationRenderedCount;
			}
			break;

		case ERenderVerdict::Unknown:
		default:
			if (IsKnownNonRepresentableClass(ClassName))
			{
				// Expected shapeless class (spatial container / *Type / *Style) -- should never
				// reach here via getShapeInputData() in the first place, but if a caller ever
				// classifies the full product list, keep it out of the loud per-name report.
				++KnownNonRepresentableCount;
			}
			else
			{
				// Genuine surprise: a class this allowlist has never seen. Count it AND name it --
				// this is the one case that must never be silent.
				++UnknownCount;
				++DroppedUnknownClasses.FindOrAdd(FString(ClassName));
			}
			break;
		}
	}

	FString FMobiusIfcClassStats::Summarize() const
	{
		FString Result = FString::Printf(
			TEXT("IFC render filter: %d rendered, %d volume-only dropped, %d annotation seen (rendered=%s, %d actually rendered), %d unknown dropped, %d known-non-representable seen"),
			RenderCount,
			VolumeOnlyCount,
			AnnotationCount,
			bMobiusIfcRenderAnnotationClasses ? TEXT("true") : TEXT("false"),
			AnnotationRenderedCount,
			UnknownCount,
			KnownNonRepresentableCount);

		if (DroppedUnknownClasses.Num() > 0)
		{
			Result += TEXT("; UNKNOWN CLASSES SEEN (allowlist needs updating): ");
			bool bFirst = true;
			for (const TPair<FString, int32>& Pair : DroppedUnknownClasses)
			{
				if (!bFirst)
				{
					Result += TEXT(", ");
				}
				Result += FString::Printf(TEXT("%s(%d)"), *Pair.Key, Pair.Value);
				bFirst = false;
			}
		}

		return Result;
	}
}
