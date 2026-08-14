// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "CoreMinimal.h"
#include "Algo/Reverse.h"
#include "Containers/ArrayView.h"

/**
 * B-Risk / Smokeview store geometry in a RIGHT-handed metric frame: X and Y are
 * horizontal (in Smokeview's default view X runs screen-right, Y runs into the
 * screen / depth) and Z is up. Unreal Engine is LEFT-handed with X forward, Y
 * right, Z up. Converting requires swapping X and Y: this both matches Smokeview's
 * on-screen layout (B-Risk X -> UE Y "right", B-Risk Y -> UE X "forward") and
 * corrects right- to left-handed chirality so the scene is not mirrored. Z (up) is
 * shared. Scale converts metres -> Unreal units (cm).
 *
 * EVERY B-Risk geometry conversion must go through these helpers so the smoke
 * volumes, vent slabs, fire/sprinkler markers and the egress room bounds (used for
 * agent -> room lookup) stay in exact agreement.
 *
 * EXCEPTION - Zones-data.json footprints use FootprintToUnreal, NOT ToUnreal. The
 * companion JSON is authored in Revit's internal frame, which reaches Unreal with Y
 * negated rather than swapped (see that function's comment). The two mappings differ
 * by a 90 degree rotation about the world origin, so a room must be built entirely
 * from one or the other - never mixed.
 */
namespace BRiskCoord
{
	/**
	 * Which XY mapping a scenario's geometry uses. There are two, they differ by a 90 degree
	 * rotation, and mixing them within one scenario is the single most productive source of bugs
	 * in this pipeline - so this is decided ONCE per scenario by the importer and threaded to every
	 * consumer, never inferred per room.
	 *
	 * The choice is deliberately driven by whether Zones-data.json was applied, because that is
	 * exactly what tells us which frame the numbers can be trusted in:
	 *
	 *  - JSON present  -> Revit. Measured against ground truth: all 16 door/window actors in the
	 *    12 RoomTest Datasmith export land 0.100 m from a polygon edge, and the FBX agrees.
	 *  - No JSON       -> SmokeviewSwap. There is no external ground truth for a hand-authored
	 *    .smv, so preserve the historical orientation, which is how the same model reads inside
	 *    Smokeview itself. Changing it would silently re-orient every pre-JSON scenario.
	 */
	enum class ERoomFrame : uint8
	{
		/** Legacy X<->Y swap (ToUnreal / ToUnrealBox). Matches Smokeview's own plan orientation. */
		SmokeviewSwap,

		/** Measured Revit mapping (x, -y, z) (FootprintToUnreal). Required for JSON footprints. */
		Revit,
	};

	/** Convert a B-Risk point (metres) to an Unreal location (cm), swapping X<->Y. */
	FORCEINLINE FVector ToUnreal(const FVector& BRiskMetres, float Scale)
	{
		return FVector(BRiskMetres.Y, BRiskMetres.X, BRiskMetres.Z) * Scale;
	}

	/**
	 * Convert a B-Risk axis-aligned room box (lower-corner Origin + Size, metres) to
	 * an Unreal-space FBox (cm). A pure X<->Y swap preserves min/max ordering, so the
	 * swapped Origin stays the box minimum.
	 */
	FORCEINLINE FBox ToUnrealBox(const FVector& OriginMetres, const FVector& SizeMetres, float Scale)
	{
		return FBox(
			ToUnreal(OriginMetres, Scale),
			ToUnreal(OriginMetres + SizeMetres, Scale));
	}

	/**
	 * Convert a Zones-data.json footprint vertex (Revit internal frame, metres) plus a
	 * separate elevation to an Unreal location (cm). Y is negated; X is not; there is no
	 * X<->Y swap.
	 *
	 * Measured, not assumed: every one of the 16 door/window actors in the 12 RoomTest
	 * Datasmith export lands exactly 0.100 m (half the 0.2 m wall thickness) from a
	 * Zones-data.json polygon edge under this mapping, across 8 edges in 2 rooms. The FBX
	 * export of the same model is in the JSON's own frame with no flip, and Assimp's
	 * MakeLeftHanded reproduces the same negation - two independent loaders agreeing.
	 *
	 * The negation is a reflection (det = -1) and therefore flips polygon winding, so
	 * callers must re-normalise winding AFTER converting, not before.
	 */
	FORCEINLINE FVector FootprintToUnreal(const FVector2D& RevitMetresXY, double ElevationMetres, float Scale)
	{
		return FVector(RevitMetresXY.X, -RevitMetresXY.Y, ElevationMetres) * Scale;
	}

	/**
	 * FootprintToUnreal for a full 3D point already in the B-Risk / Revit world frame (metres) -
	 * fire origins and sprinkler heads, which are room-local offsets added to Room.Origin.
	 */
	FORCEINLINE FVector FootprintToUnreal(const FVector& RevitMetres, float Scale)
	{
		return FVector(RevitMetres.X, -RevitMetres.Y, RevitMetres.Z) * Scale;
	}

	/**
	 * Convert a B-Risk world point (metres) to Unreal (cm) under the scenario's frame.
	 *
	 * Use this for anything positioned in the room's world space - fire origins, sprinkler heads -
	 * rather than calling ToUnreal or FootprintToUnreal directly, so a marker can never land in a
	 * different frame from the room that contains it.
	 */
	FORCEINLINE FVector WorldToUnreal(const FVector& BRiskMetres, float Scale, ERoomFrame Frame)
	{
		return (Frame == ERoomFrame::Revit)
			? FootprintToUnreal(BRiskMetres, Scale)
			: ToUnreal(BRiskMetres, Scale);
	}

	/** How close an equivalent-rectangle side must be to a footprint extent to count as a match. */
	constexpr double RoomAxisMatchToleranceM = 1.0e-3;

	/** Outcome of trying to line a room's B-Risk local axes up with its real footprint. */
	enum class ERoomLocalAxes : uint8
	{
		/** No footprint to check against - offsets used as authored. */
		Unverified,
		/** Equivalent rectangle matches the footprint as-is; offsets used as authored. */
		Aligned,
		/** Equivalent rectangle is the footprint transposed; local X/Y were swapped. */
		Transposed,
		/** Footprint is not a rectangle, so B-Risk's local frame cannot be mapped onto it. */
		Unmappable,
	};
}

/**
 * Geometry descriptor for a single room parsed from a B-Risk SMV manifest.
 * Dimensions and origin are stored in the B-Risk coordinate space (metres).
 * The Label is populated from the companion LABEL block that follows each ROOM block.
 */
struct MOBIUSDATAIMPORTER_API FBRiskRoomGeometry
{
	/** 1-based room identifier as declared in the ROOM keyword block. */
	int32 RoomId = INDEX_NONE;

	/**
	 * Full extents of the room along each axis in metres: X = room_length,
	 * Y = room_width, Z = height (SR282 p.17, Figure 10 - "X, length" / "Y, width").
	 *
	 * IMPORTANT: these carry NO orientation information. B-Risk derives L and W from
	 * floor area and perimeter (SR282 eq. 1-2), and L always takes the +sqrt branch,
	 * so L is simply the larger root. A room longer in Y therefore arrives transposed.
	 * Only FootprintPolygon describes the real footprint - prefer it when present.
	 */
	FVector Size = FVector::ZeroVector;

	/** Lower-corner world origin of the room in metres (room_absx / room_absy / elevation). */
	FVector Origin = FVector::ZeroVector;

	/** Human-readable room name from the LABEL block immediately following this ROOM block. */
	FString Label;

	/**
	 * True plan footprint of the room, from the companion Zones-data.json when present.
	 * Outer ring only, in the same XY frame and units (metres) as Origin, normalised to
	 * counter-clockwise winding. Empty when the scenario has no Zones-data.json, in which
	 * case consumers must fall back to the Origin/Size rectangle.
	 */
	TArray<FVector2D> FootprintPolygon;

	/** Zones-data.json floorElevation (metres). Only meaningful when bHasFootprintExtents is true. */
	double FootprintFloorElevationM = 0.0;

	/** Zones-data.json height (metres). Only meaningful when bHasFootprintExtents is true. */
	double FootprintHeightM = 0.0;

	/**
	 * True when the JSON supplied BOTH floorElevation and height. Consumers take Z from the .smv
	 * either way; this only says whether the JSON values are worth cross-checking against it.
	 * Without it, an omitted height is indistinguishable from a genuine zero.
	 */
	bool bHasFootprintExtents = false;

	/**
	 * Zones-data.json per-space tenability.odLimitPerM. Captured for round-trip only:
	 * Mobius drives tenability from the scenario-wide input1.xml endpoints, not from this.
	 */
	double OdLimitPerM = 0.0;

	/** True when OdLimitPerM was present in Zones-data.json. */
	bool bHasOdLimitPerM = false;
};

namespace BRiskCoord
{
	/** Weld distance for footprint ring vertices, in Unreal centimetres. */
	constexpr double FootprintWeldToleranceCm = 0.1;

	/** Shoelace signed area of a 2D ring. Positive means counter-clockwise. */
	FORCEINLINE double SignedRingArea(const TArray<FVector2D>& Ring)
	{
		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Ring.Num(); ++Index)
		{
			const FVector2D& Current = Ring[Index];
			const FVector2D& Next = Ring[(Index + 1) % Ring.Num()];
			TwiceArea += Current.X * Next.Y - Next.X * Current.Y;
		}
		return 0.5 * TwiceArea;
	}

	/**
	 * Even-odd point-in-ring test, in the ring's own 2D plane.
	 *
	 * THE containment predicate for a room footprint. Both the smoke mask
	 * (RasteriseFootprintMask) and agent -> room attribution
	 * (UE::Mobius::Tenability::ResolveRoomIndexAtLocation) resolve through this one function, so
	 * which voxels render and which room doses an agent can never disagree about where a room's
	 * walls are.
	 *
	 * Counts the ring edges crossing the ray travelling in +X from Point. A point lying exactly on
	 * an edge is NOT boundary-inclusive, and resolves to one side deterministically: where two
	 * rooms abut, the shared wall belongs to exactly one of them - never to both (as
	 * FBox::IsInsideOrOn would) and never to neither, so an agent in a doorway always lands in a
	 * room. Fewer than three vertices is not a ring and returns false; a caller that reads "no
	 * polygon" as "no constraint" must test the vertex count itself rather than rely on this.
	 */
	FORCEINLINE bool IsPointInRing(const TConstArrayView<FVector2D> Ring, const FVector2D& Point)
	{
		const int32 VertexCount = Ring.Num();
		if (VertexCount < 3)
		{
			return false;
		}

		bool bInside = false;
		for (int32 Index = 0, Previous = VertexCount - 1; Index < VertexCount; Previous = Index++)
		{
			const FVector2D& Current = Ring[Index];
			const FVector2D& Prior = Ring[Previous];

			const bool bStraddlesY = (Current.Y > Point.Y) != (Prior.Y > Point.Y);
			if (!bStraddlesY)
			{
				continue;
			}

			const double CrossingX = Current.X
				+ (Point.Y - Current.Y) * (Prior.X - Current.X) / (Prior.Y - Current.Y);
			if (Point.X < CrossingX)
			{
				bInside = !bInside;
			}
		}

		return bInside;
	}

	/** Where a B-Risk room is, in Unreal space (cm). See MakeRoomFootprint. */
	struct FRoomFootprintCm
	{
		/**
		 * Outer ring in the UE XY plane, normalised counter-clockwise, first vertex not repeated.
		 * Empty when the room had no usable Zones-data.json polygon and Bounds is the equivalent
		 * rectangle instead.
		 */
		TArray<FVector2D> Polygon;

		/** Axis-aligned bounds of the footprint including the floor-to-ceiling Z slab. */
		FBox Bounds = FBox(ForceInit);

		/** True when Polygon is populated. */
		bool bFromPolygon = false;
	};

	/**
	 * The single answer to "where is this room in Unreal space".
	 *
	 * EVERY consumer that describes where a room *is* - smoke volumes, egress bounds, agent->room,
	 * fire/sprinkler markers, vent slabs - must go through this, passing the scenario's frame from
	 * FBRiskScenarioData::RoomFrame. That is what keeps a scene from ending up half-rotated.
	 *
	 * Frame::Revit prefers the Zones-data.json polygon and falls back to the .smv equivalent
	 * rectangle under the same `(x, -y, z)` mapping - valid because `.smv` room_absx/room_absy are
	 * measured to be the same frame and units as the JSON (room 1's polygon min corner is exactly
	 * its absx/absy to four decimal places).
	 *
	 * Frame::SmokeviewSwap is rectangle-only by construction: a polygon can only come from
	 * Zones-data.json, and its presence is what selects Revit in the first place. Any polygon
	 * reaching this branch would be in the wrong frame, so it is deliberately ignored rather than
	 * converted.
	 *
	 * Caveat inherited from B-Risk and present in BOTH frames: for a room WITHOUT a polygon this
	 * puts the rectangle in the right place but cannot fix its shape. `L` is by definition the
	 * larger root of the area/perimeter equations (SR282 eq. 1-2), so a room longer in Y still
	 * arrives transposed.
	 */
	FORCEINLINE FRoomFootprintCm MakeRoomFootprint(
		const FBRiskRoomGeometry& Room,
		float Scale,
		ERoomFrame Frame)
	{
		FRoomFootprintCm Footprint;

		if (Frame == ERoomFrame::SmokeviewSwap)
		{
			// Byte-identical to the pre-JSON behaviour: a pure X<->Y swap preserves min/max
			// ordering, so the swapped origin is still the box minimum and ToUnrealBox is exact.
			Footprint.Bounds = ToUnrealBox(Room.Origin, Room.Size, Scale);
			return Footprint;
		}

		const double FloorZ = Room.Origin.Z * Scale;
		const double CeilingZ = (Room.Origin.Z + Room.Size.Z) * Scale;

		if (Room.FootprintPolygon.Num() >= 3)
		{
			// Convert BEFORE cleaning and re-winding: the Y negation is a reflection, so the
			// counter-clockwise winding the importer normalised in B-Risk space arrives inverted.
			Footprint.Polygon.Reserve(Room.FootprintPolygon.Num());
			for (const FVector2D& SourceVertex : Room.FootprintPolygon)
			{
				const FVector Converted = FootprintToUnreal(SourceVertex, 0.0, Scale);
				const FVector2D Point(Converted.X, Converted.Y);

				// Drop repeated vertices, including an explicit closing vertex: a zero-length
				// edge is a degenerate wall quad and a duplicate point for a triangulator.
				if (Footprint.Polygon.Num() > 0 && Point.Equals(Footprint.Polygon.Last(), FootprintWeldToleranceCm))
				{
					continue;
				}
				Footprint.Polygon.Add(Point);
			}
			while (Footprint.Polygon.Num() > 1
				&& Footprint.Polygon.Last().Equals(Footprint.Polygon[0], FootprintWeldToleranceCm))
			{
				Footprint.Polygon.Pop();
			}

			if (Footprint.Polygon.Num() >= 3)
			{
				if (SignedRingArea(Footprint.Polygon) < 0.0)
				{
					Algo::Reverse(Footprint.Polygon);
				}

				for (const FVector2D& Point : Footprint.Polygon)
				{
					Footprint.Bounds += FVector(Point.X, Point.Y, FloorZ);
					Footprint.Bounds += FVector(Point.X, Point.Y, CeilingZ);
				}

				Footprint.bFromPolygon = true;
				return Footprint;
			}

			// Degenerate ring - fall through to the rectangle rather than return nothing.
			Footprint.Polygon.Reset();
		}

		// FBox is accumulated rather than constructed from a corner pair: negating Y inverts the
		// min/max ordering, so ToUnrealBox's "the swapped origin stays the minimum" no longer holds.
		Footprint.Bounds += FootprintToUnreal(FVector2D(Room.Origin.X, Room.Origin.Y), Room.Origin.Z, Scale);
		Footprint.Bounds += FootprintToUnreal(
			FVector2D(Room.Origin.X + Room.Size.X, Room.Origin.Y + Room.Size.Y),
			Room.Origin.Z + Room.Size.Z,
			Scale);
		return Footprint;
	}

	/**
	 * Tolerance for comparing two candidate edges' distances, in centimetres.
	 *
	 * Distances, not squared distances. A tolerance on squared centimetres is not a tolerance on
	 * centimetres - at the measured 10 cm stand-off, two candidate edges a visible 0.5 mm apart
	 * differ by ~1 cm^2, so a squared comparison would sort near-ties by an amount nobody chose.
	 * A deliberate 0.1 mm.
	 */
	constexpr double OpeningEdgeTieToleranceCm = 0.01;

	/**
	 * How far past its expected stand-off an opening centre may sit before a consumer should stop
	 * treating it as belonging to that wall.
	 *
	 * The centre sits on the wall centreline, half a wall thickness outside the room's polygon, so
	 * the expected stand-off is hostThickness/2 and this is the slack on top. 5 cm accepts the
	 * ~14 mm mesh/zones model mismatch. Measured in the 12-room model: vent 32's centre is 10 cm
	 * from room 1's wall as declared, but 20 cm from room 2's, because the add-in derives
	 * wall-leakage from the room boundary rather than the wall centreline - so a bound is the only
	 * thing standing between that and a hole cut in a wall the opening does not belong to.
	 */
	constexpr double OpeningStandoffToleranceCm = 5.0;

	/** Stand-off allowance for a pre-v2 opening, which carries no hostThickness to derive one from. */
	constexpr double OpeningStandoffFallbackCm = 25.0;

	/**
	 * The largest distance from a wall at which an opening still counts as being ON that wall.
	 *
	 * One formula, not one per caller: ResolveOpeningEdge always returns SOME edge however far away
	 * the centre is, so every destructive consumer needs this bound, and two copies of it drift. The
	 * copy that omitted the no-thickness branch was a live example - on a pre-v2 export it collapsed
	 * to 5 cm and rejected every opening at the measured 10 cm stand-off.
	 */
	FORCEINLINE double MaxOpeningStandoffCm(double HostThicknessMetres, float Scale)
	{
		const double HalfThicknessCm = HostThicknessMetres * 0.5 * Scale;
		return (HalfThicknessCm > 0.0 ? HalfThicknessCm : OpeningStandoffFallbackCm)
			+ OpeningStandoffToleranceCm;
	}

	/** Which footprint edge an opening sits on, and where along it. See ResolveOpeningEdge. */
	struct FOpeningEdgePlacement
	{
		/** Index into the ring; the edge runs Ring[EdgeIndex] -> Ring[(EdgeIndex + 1) % Num]. */
		int32 EdgeIndex = INDEX_NONE;

		/** Distance of the opening centre along the edge from Ring[EdgeIndex], in centimetres. */
		double AlongCm = 0.0;

		/** Perpendicular distance from the opening centre to the edge, in centimetres. */
		double DistanceCm = 0.0;

		/** Length of the chosen edge, in centimetres. */
		double EdgeLengthCm = 0.0;

		/** True when the full opening width lies within the edge. A tie-break, NOT a rejection. */
		bool bFitsOnEdge = false;
	};

	/**
	 * Decide which wall of a room's footprint an opening belongs to.
	 *
	 * Shared by every consumer that needs to know where an opening meets a wall - the hazard
	 * marker slab and the room mesh's hole cut - because those two are sized from the same numbers
	 * and a marker that does not sit inside its own hole means one of them is wrong. That check
	 * only has force if both go through this function rather than each keeping a copy of it.
	 *
	 * Nearest edge wins, with the full opening width having to fit as the tie-break. Ties are real:
	 * an opening in a room corner is equidistant from two edges (measured: two of the 34 openings in
	 * the 12-room test sit exactly on a corner at 0.100 m from both). Without the fit test the
	 * winner would be whichever edge came first in winding order.
	 *
	 * CentrePlanCm is used verbatim and is NOT projected onto the ring: it sits on the wall
	 * centreline, roughly half a wall thickness outside the footprint, and for a wall shared by two
	 * rooms that is the only point both rooms agree on. The ring supplies the axis, nothing else.
	 *
	 * Note this always returns SOME edge for a non-degenerate ring, however far away the centre is.
	 * A caller that acts on the result destructively - cutting a hole, say - must also bound
	 * OutPlacement.DistanceCm, because "nearest" is not "on".
	 *
	 * @return false when the ring has fewer than three vertices or every edge is zero-length.
	 */
	FORCEINLINE bool ResolveOpeningEdge(
		const TArray<FVector2D>& Ring,
		const FVector2D& CentrePlanCm,
		double WidthCm,
		FOpeningEdgePlacement& OutPlacement)
	{
		OutPlacement = FOpeningEdgePlacement();
		if (Ring.Num() < 3)
		{
			return false;
		}

		for (int32 EdgeIndex = 0; EdgeIndex < Ring.Num(); ++EdgeIndex)
		{
			const FVector2D& A = Ring[EdgeIndex];
			const FVector2D& B = Ring[(EdgeIndex + 1) % Ring.Num()];
			const FVector2D Along = B - A;
			const double LengthSq = Along.SizeSquared();
			if (LengthSq <= 0.0)
			{
				continue;
			}

			const double T = FMath::Clamp(FVector2D::DotProduct(CentrePlanCm - A, Along) / LengthSq, 0.0, 1.0);
			const FVector2D Closest = A + Along * T;
			const double DistanceCm = FVector2D::Distance(CentrePlanCm, Closest);

			const double Length = FMath::Sqrt(LengthSq);
			const double AlongCm = T * Length;
			const bool bFits = (AlongCm - WidthCm * 0.5 >= -OpeningEdgeTieToleranceCm)
				&& (AlongCm + WidthCm * 0.5 <= Length + OpeningEdgeTieToleranceCm);

			// Strictly nearer always wins; an equal-distance edge only takes the place of the
			// incumbent by being one the opening actually fits on.
			if (OutPlacement.EdgeIndex != INDEX_NONE)
			{
				const bool bNearer = DistanceCm < OutPlacement.DistanceCm - OpeningEdgeTieToleranceCm;
				const bool bTiedAndBetter =
					FMath::Abs(DistanceCm - OutPlacement.DistanceCm) <= OpeningEdgeTieToleranceCm
					&& bFits && !OutPlacement.bFitsOnEdge;
				if (!bNearer && !bTiedAndBetter)
				{
					continue;
				}
			}

			OutPlacement.EdgeIndex = EdgeIndex;
			OutPlacement.AlongCm = AlongCm;
			OutPlacement.DistanceCm = DistanceCm;
			OutPlacement.EdgeLengthCm = Length;
			OutPlacement.bFitsOnEdge = bFits;
		}

		return OutPlacement.EdgeIndex != INDEX_NONE;
	}

	/**
	 * Rasterise a room footprint into an 8-bit coverage mask spanning its own bounding box.
	 *
	 * Texel (i, j) is sampled at its centre, mapped into the bbox as
	 * `u = (i + 0.5) / Resolution`, `v = (j + 0.5) / Resolution`, so a shader reading the mask at
	 * the same normalised UV needs no world transform at all — the grid and the mask share the
	 * bounding box by construction. Row j = 0 is the bbox minimum Y.
	 *
	 * 255 inside, 0 outside, by IsPointInRing per texel - the same predicate that decides which
	 * room doses an agent, so the smoke a voxel renders and the dose an agent standing there
	 * receives agree on the wall line by construction. A room with no polygon produces an all-255
	 * mask, so consumers never need a "has footprint" branch.
	 *
	 * This is the smoke mask: the Niagara volume stays a box over the bbox, and the mask decides
	 * which voxels render. A loop-based point-in-polygon in the shader was rejected because
	 * Niagara's own scratch-module template warns against loops in custom HLSL.
	 */
	FORCEINLINE void RasteriseFootprintMask(
		const FRoomFootprintCm& Footprint,
		int32 Resolution,
		TArray<uint8>& OutMask)
	{
		Resolution = FMath::Max(Resolution, 1);
		OutMask.Reset();
		OutMask.SetNumUninitialized(Resolution * Resolution);

		const int32 VertexCount = Footprint.Polygon.Num();
		if (VertexCount < 3 || !Footprint.Bounds.IsValid)
		{
			FMemory::Memset(OutMask.GetData(), 0xFF, OutMask.Num());
			return;
		}

		const double MinX = Footprint.Bounds.Min.X;
		const double MinY = Footprint.Bounds.Min.Y;
		const double SizeX = Footprint.Bounds.Max.X - MinX;
		const double SizeY = Footprint.Bounds.Max.Y - MinY;
		if (SizeX <= 0.0 || SizeY <= 0.0)
		{
			FMemory::Memset(OutMask.GetData(), 0xFF, OutMask.Num());
			return;
		}

		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			const double SampleY = MinY + ((Row + 0.5) / Resolution) * SizeY;

			for (int32 Column = 0; Column < Resolution; ++Column)
			{
				const double SampleX = MinX + ((Column + 0.5) / Resolution) * SizeX;

				const bool bInside = IsPointInRing(Footprint.Polygon, FVector2D(SampleX, SampleY));
				OutMask[Row * Resolution + Column] = bInside ? 0xFF : 0x00;
			}
		}
	}

	/**
	 * Convert a room-local B-Risk offset (fire origin, sprinkler head) into a world point in the
	 * B-Risk frame (metres), correcting for equivalent-rectangle transposition.
	 *
	 * WHY THIS IS NEEDED. B-Risk expresses in-room positions against the equivalent rectangle,
	 * whose local X runs along `L` - and `L` is by definition the LARGER root of the area/perimeter
	 * equations (SR282 eq. 1-2), not an oriented axis. When a room's long side runs along world Y,
	 * the rectangle arrives transposed and the local offsets are transposed with it.
	 *
	 * Measured on 12 RoomTest room 1 (Lobby 14, equivalent rectangle 5.0 x 2.786, real footprint
	 * 2.786 x 5.0 - the bbox exactly transposed): sprinkler sprx/spry 2.8464/1.3876 used as
	 * authored lands 6 cm OUTSIDE the room; swapped it lands within 5 mm of the room's X centre.
	 * The fire likewise moves from 1.257 m off-centre in a 2.786 m-wide room to (+0.15, +0.15) of
	 * dead centre. Two independent markers, both only sensible one way round.
	 *
	 * The swap is applied only when the equivalent rectangle matches the footprint bounding box
	 * transposed to within a millimetre and does NOT match it as-is - an exact data test, not a
	 * guess. A non-rectangular room (12 RoomTest room 2: rectangle 22.986 x 1.0 against a
	 * 17.8 x 6.186 footprint) matches neither, so its local frame is genuinely unrecoverable;
	 * that reports Unmappable and the offsets are left alone for the caller to warn about.
	 */
	FORCEINLINE FVector RoomLocalToWorld(
		const FBRiskRoomGeometry& Room,
		const FVector& RoomLocalM,
		ERoomLocalAxes& OutAxes)
	{
		OutAxes = ERoomLocalAxes::Unverified;

		if (Room.FootprintPolygon.Num() >= 3)
		{
			FVector2D MinCorner = Room.FootprintPolygon[0];
			FVector2D MaxCorner = Room.FootprintPolygon[0];
			for (const FVector2D& Vertex : Room.FootprintPolygon)
			{
				MinCorner.X = FMath::Min(MinCorner.X, Vertex.X);
				MinCorner.Y = FMath::Min(MinCorner.Y, Vertex.Y);
				MaxCorner.X = FMath::Max(MaxCorner.X, Vertex.X);
				MaxCorner.Y = FMath::Max(MaxCorner.Y, Vertex.Y);
			}

			const double ExtentX = MaxCorner.X - MinCorner.X;
			const double ExtentY = MaxCorner.Y - MinCorner.Y;

			const bool bAligned = FMath::IsNearlyEqual(Room.Size.X, ExtentX, RoomAxisMatchToleranceM)
				&& FMath::IsNearlyEqual(Room.Size.Y, ExtentY, RoomAxisMatchToleranceM);
			const bool bTransposed = FMath::IsNearlyEqual(Room.Size.X, ExtentY, RoomAxisMatchToleranceM)
				&& FMath::IsNearlyEqual(Room.Size.Y, ExtentX, RoomAxisMatchToleranceM);

			// A square room satisfies both; prefer Aligned, where the swap is a no-op anyway.
			if (bAligned)
			{
				OutAxes = ERoomLocalAxes::Aligned;
			}
			else if (bTransposed)
			{
				OutAxes = ERoomLocalAxes::Transposed;
				return Room.Origin + FVector(RoomLocalM.Y, RoomLocalM.X, RoomLocalM.Z);
			}
			else
			{
				OutAxes = ERoomLocalAxes::Unmappable;
			}
		}

		return Room.Origin + RoomLocalM;
	}
}

/**
 * Describes the location of a fire source inside a specific room.
 * Parsed from the FIRE keyword block in the B-Risk SMV manifest.
 */
struct MOBIUSDATAIMPORTER_API FBRiskFireGeometry
{
	/** Room that contains this fire source (matches FBRiskRoomGeometry::RoomId). */
	int32 RoomId = INDEX_NONE;

	/** Position of the fire base within the room, in metres (B-Risk room-local space). */
	FVector Location = FVector::ZeroVector;
};

/** Sprinkler location and response metadata parsed from a B-Risk sprinklers.xml companion file. */
struct MOBIUSDATAIMPORTER_API FBRiskSprinklerGeometry
{
	/** Sprinkler identifier from sprinklers.xml. */
	int32 SprinklerId = INDEX_NONE;

	/** Room that contains this sprinkler. */
	int32 RoomId = INDEX_NONE;

	/** Sprinkler head position in room-local metres. Z is measured from the room floor. */
	FVector Location = FVector::ZeroVector;

	/** Activation/response time in seconds. Negative means no known activation time. */
	double ActivationTimeSeconds = -1.0;

	/** Nominal spray radius in metres. */
	double SprayRadius = 0.0;

	/** Nominal sprinkler density. */
	double SprayDensity = 0.0;

	/** Actuation temperature in Celsius. */
	double ActuationTemperatureC = 0.0;
};

/**
 * What kind of opening a vent is, from Zones-data.json openings[].type.
 *
 * Unknown is what every vent parsed from a .smv VENTGEOM block gets: the .smv records geometry and
 * connectivity but never says what the opening IS, so this cannot be inferred there. Leakage vents
 * are a real modelling device, not noise - a fire engineer adds them to represent smoke passing
 * non-fire-rated construction, and the ones marked leakageOf="door" are the gap round a closed leaf,
 * i.e. the path that still exists after the door shuts.
 *
 * Plain enum class, not UENUM: MobiusDataImporter has no UHT and does not need it for this.
 */
enum class EBRiskVentKind : uint8
{
	Unknown,
	Door,
	Window,
	Leakage
};

/**
 * B-Risk's default <cd>, used for any opening whose vents.xml record could not be matched.
 *
 * Namespace scope rather than a static member of FBRiskVentGeometry on purpose: that struct is
 * dll-exported, and a static constexpr member of an exported struct is ODR-used the moment anything
 * binds it to a const& (every TestEqual overload does), which is the one MSVC/dllimport combination
 * that fails to link. An inline variable has no such interaction.
 */
inline constexpr double BRiskDefaultDischargeCoefficient = 0.68;

/**
 * Ambient temperature used when input1.xml does not supply one. Same value the vent-flow routine
 * hardcoded before 2026-08-14; kept only as a fallback so a scenario without the tag behaves as it
 * always did rather than silently jumping.
 */
inline constexpr double BRiskFallbackAmbientTempC = 20.0;

/**
 * Interior ambient used when input1.xml does not supply <temp_interior>: the temperature a room
 * starts at before the fire touches it.
 *
 * 24 C is both B-Risk's own default (every zone CSV in this tree opens with every room at exactly
 * 24) and the value the smoke-visual routine hardcoded until 2026-08-14, so a scenario without the
 * tag is unchanged. Deliberately a DIFFERENT constant from BRiskFallbackAmbientTempC: B-Risk models
 * an interior and an exterior ambient as two separate user inputs (SR282 Table 2, printed p.14), and
 * collapsing them is exactly the mistake that made a 2026-08-07 investigation test 297 K on an
 * exterior vent side and wrongly conclude ambient was not the problem.
 */
inline constexpr double BRiskFallbackInteriorTempC = 24.0;

/**
 * B-Risk's OWN kelvin/Celsius offset, which is 273 and not 273.15.
 *
 * This looks like a bug and is not ours to fix. B-Risk's input screen takes ambient temperatures in
 * Celsius (SR282 Table 2, printed p.14) and writes whole kelvin to input1.xml. Evidence it uses 273:
 * distributions.xml declares <units>K</units> with whole-kelvin bounds, none of which end in .15;
 * the string "273.15" appears in no file of any export in this tree; and every zone CSV starts its
 * rooms at EXACTLY 24 C while input1.xml carries <temp_interior>297</temp_interior>, which
 * 297 - 273.15 = 23.85 cannot produce. That "24" is not display rounding - the next row of the same
 * CSV prints 23.9795126181685.
 *
 * So a user who typed 15 C gets <temp_exterior>288</temp_exterior>, and reading it back as 14.85
 * would put a 0.15 C bias between the ambient and the ULT/LLT channels it is subtracted from - which
 * come out of that same CSV on this same convention. Measured against B-Risk's own wallventflows.txt
 * the 273 reading is also simply closer: worst median vent error 0.5% against 2.3% at 273.15.
 *
 * B-Risk's SOLVER does treat the stored number as a true absolute temperature (SR282 nomenclature:
 * "T = reference temperature of ambient air (K)"). The 0.15 K gap is therefore internal to B-Risk,
 * between its UI/CSV convention and its physics. Disclose it; do not silently "correct" it.
 */
inline constexpr double BRiskKelvinToCelsiusOffset = 273.0;

/** Ambient conditions B-Risk was configured with, read from input1.xml. */
struct MOBIUSDATAIMPORTER_API FBRiskAmbientConditions
{
	/** <temp_exterior> converted to Celsius. This is the outside of any room-to-exterior opening. */
	double ExteriorTempC = BRiskFallbackAmbientTempC;

	/** True when input1.xml actually supplied temp_exterior, so the fallback is never mistaken for it. */
	bool bHasExteriorTemp = false;

	/**
	 * <temp_interior> converted to Celsius - the temperature B-Risk starts every room at.
	 *
	 * Captured for reporting and for future use; the vent-flow routine does NOT consume it. An
	 * earlier investigation (2026-08-07) tested this value as the vent-flow ambient, measured that
	 * it made room-to-exterior flow worse, and concluded "ambient is not the problem". It was the
	 * right idea applied to the wrong one of the two fields - the exterior side needs temp_exterior.
	 */
	double InteriorTempC = BRiskFallbackInteriorTempC;

	/** True when input1.xml actually supplied temp_interior. */
	bool bHasInteriorTemp = false;
};

/** Horizontal vent/opening geometry parsed from a B-Risk VENTGEOM block. */
struct MOBIUSDATAIMPORTER_API FBRiskVentGeometry
{
	/** Room containing this side of the opening. */
	int32 FromRoomId = INDEX_NONE;

	/** Connected room id. B-Risk uses external/outside room ids for exterior openings. */
	int32 ToRoomId = INDEX_NONE;

	/**
	 * B-Risk wall face id from VENTGEOM, 1-based: 1 = front (-Y), 2 = right (+X),
	 * 3 = rear (+Y), 4 = left (-X). SR282 p.17 names these "front, right, rear or left".
	 * Note vents.xml stores the same face 0-based, so .smv face == vents.xml face + 1.
	 */
	int32 Face = INDEX_NONE;

	/** Opening width along the wall, in metres. */
	double Width = 0.0;

	/** Opening offset along the wall, in metres. */
	double Offset = 0.0;

	/** Height of the bottom of the opening above the room floor, in metres. */
	double SillHeight = 0.0;

	/**
	 * Opening height (head minus sill), in metres.
	 *
	 * NOTE: the .smv VENTGEOM record's last token is the HEAD height (sill + opening
	 * height), not the opening height - see zone.csv HVENT_n, which reports
	 * Width * (head - sill). The parser subtracts the sill so this field means what
	 * its name says; consumers may safely compute head as SillHeight + Height.
	 */
	double Height = 0.0;

	// --- From Zones-data.json openings[]. Absent (bHasPlacement false) for a .smv-only scenario. ---

	/** openings[].ventId, matching the <id> in the companion vents.xml. INDEX_NONE when unknown. */
	int32 VentId = INDEX_NONE;

	/** Door / window / leakage. Unknown for anything that came from a VENTGEOM block. */
	EBRiskVentKind Kind = EBRiskVentKind::Unknown;

	/**
	 * True when CentreMetres is real. This is the ONLY trustworthy placement Mobius has.
	 *
	 * Face and Offset cannot substitute for it and are not a fallback. Both are coordinates in
	 * B-Risk's area/perimeter-equivalent rectangle (SR282 eq. 1-2), which for a non-rectangular room
	 * is a shape that does not exist: Corridor 15's real walls are 17.8 m and 5.2 m, and vents.xml
	 * reports offsets up to 22.3 m along a "23 m wall" that is those two unrolled end to end. It is
	 * not even a per-wall id - measured over all 34 openings in the 12-room test, .smv face 2 and
	 * face 3 each map to three different wall normals.
	 */
	bool bHasPlacement = false;

	/**
	 * Opening centre in the Revit frame, metres - the same frame and units as
	 * FBRiskRoomGeometry::FootprintPolygon, so BRiskCoord::FootprintToUnreal converts it.
	 *
	 * Sits on the WALL CENTRELINE, roughly half a wall thickness outside the room's footprint
	 * polygon (a constant 0.100 m across all 34 openings in the 12-room test, for a 200 mm wall).
	 * That is correct and must not be projected onto the polygon: for a wall shared by two rooms it
	 * is the only position both agree on. Measured - the Lobby/Corridor door sits at x 3.1365,
	 * exactly midway between Corridor's face at 3.0365 and Lobby's at 3.2365.
	 *
	 * Z is NOT used for placement. SillHeight/Height are floor-relative and already say where the
	 * opening sits vertically; centre z would need to be resolved against floorElevation, which is
	 * 0 for every room in the only sample available, so absolute and floor-relative are
	 * indistinguishable there. Deriving Z the existing way keeps one code path and guesses nothing.
	 */
	FVector CentreMetres = FVector::ZeroVector;

	/**
	 * True physical opening size in metres, from openings[].width / .height - what a person would
	 * measure at the door.
	 *
	 * Distinct from Width/Height, which are the MODELLED figures B-Risk actually simulated: a 0.9 m
	 * door is commonly modelled at 0.45 m. Renderers want these; anything reasoning about flow area
	 * wants Width/Height, whose product is the zone CSV's HVENT. 0 when unknown.
	 *
	 * Both are carried, not just width, so the modelled/physical split stays symmetric. In every
	 * export seen so far modelledHeight == height, so this changes no number today - but Width is
	 * modelled while Height was being filled from the REAL height, and the first export that models
	 * a reduced height would have made Width * Height silently mix the two.
	 */
	double PhysicalWidth = 0.0;
	double PhysicalHeight = 0.0;

	/**
	 * When the opening opens and shuts, in seconds. Negative when unknown.
	 *
	 * Parsed from openings[].openTimeS / closeTimeS, then OVERWRITTEN from B-Risk's own vents.xml
	 * <opentime>/<closetime> wherever a record can be matched - see ParseVentsXml for why B-Risk
	 * wins and why the match is not always possible.
	 */
	double OpenTimeSeconds = -1.0;
	double CloseTimeSeconds = -1.0;

	/**
	 * True once a vents.xml record has been matched to this opening.
	 *
	 * Distinct from "the times are non-zero": B-Risk writes 0/0 for an opening with no scheduled
	 * change, which is a real schedule meaning "always open", and is what every leakage path in the
	 * 12-room export carries. Without this flag that is indistinguishable from "we never found a
	 * record", and the two must not be conflated - one is a fact, the other is an absence.
	 */
	bool bHasSchedule = false;

	/**
	 * True when vents.xml has <autoopenvent>True</autoopenvent> for this opening - B-Risk's
	 * "open automatically when a trigger is reached" mode, which is a DIFFERENT mechanism from
	 * <opentime>/<closetime> and overrides them. See IsOpenAtTime for why that forces closed.
	 */
	bool bAutoOpenVent = false;

	/**
	 * Discharge coefficient for this opening, from vents.xml <cd>. Dimensionless, 0..1.
	 *
	 * Multiplies the Bernoulli slab flux in ComputeWallVentFlow: the jet contracts as it passes
	 * through the opening (vena contracta), so the effective flow area is smaller than the
	 * geometric one. B-Risk exposes this PER VENT and its users edit it, so it must be read
	 * rather than assumed.
	 *
	 * The default is B-Risk's own default, which is what a real door or window carries. It is NOT
	 * universal: the 12-room export's three wall-leakage paths (ids 32/33/34) carry 1.0. SR282
	 * §4.6.2 (printed p.19) gives the rule - "In cases, where the top of the vent is flush with the
	 * ceiling, then a value of 1.0 is recommended" - and the geometry agrees exactly: those three are
	 * 3.999 m tall with a zero sill in rooms rooms.xml declares as 4.000 m. §7.12.2 (printed p.65)
	 * says the same thing again for the spill-plume case. Those three are permanently open while the
	 * doors shut at 60 s, so they are the dominant path for most of that run - assuming 0.68 there
	 * ran them at 68 % of B-Risk's own flow.
	 *
	 * ZERO IS A VALUE, NOT AN ABSENCE. SR282 §4.6.2(d) (printed p.20) closes a vent sampled shut by
	 * "setting the discharge coefficient to zero", and adds that "if using this option causes the
	 * initial state of the vent to be closed, then using the vent opening options will not
	 * subsequently open the vent". So cd = 0 means SHUT FOR THE WHOLE RUN and IsOpenAtTime honours
	 * it. Substituting the 0.68 default there would draw full flow through an opening B-Risk has
	 * shut - the same failure de87f506 fixed for the schedule path. The manual states no valid range
	 * at all, only that 0.6-0.7 is "typical" and that "the user is responsible for entering an
	 * appropriate value", so the bounds enforced on import are ours: [0, 1].
	 *
	 * Populated by the same vents.xml join that sets bHasSchedule; when that flag is false no
	 * record was matched and this is the default rather than a value read from the file.
	 */
	double DischargeCoefficient = BRiskDefaultDischargeCoefficient;

	/** True when cd is exactly zero, i.e. B-Risk holds this opening shut for the entire run. */
	bool IsShutByDischargeCoefficient() const { return DischargeCoefficient == 0.0; }

	/**
	 * Is the opening open at this simulation time?
	 *
	 * B-Risk semantics: <opentime> is when it opens, <closetime> when it shuts, so a door with
	 * 10/60 is SHUT for the first ten seconds. Both zero means no scheduled change - always open -
	 * which is the common case and the default for anything with no record. A close time at or
	 * before the open time is read as "opens and never shuts" rather than as a zero-length window,
	 * because B-Risk writes closetime 0 for openings it never closes.
	 *
	 * An auto-opening vent is reported SHUT for the whole run, and its zeros are ignored. Three
	 * reasons, in order of weight:
	 *   1. SR282 §4.6.2 lists the initial state as open "except ... (b) when the vent is set to open
	 *      at flashover, when the ventilation limit is reached or when a detector responds". So the
	 *      manual's own initial state for this mode is CLOSED, whatever the times say.
	 *   2. We cannot evaluate B-Risk's triggers. triggerFR/FO/VL/HRR/SD/HD are not reproducible from
	 *      the exported results, so there is no honest moment at which to open it.
	 *   3. It matches the only ground truth that exists. B-Risk's optional wallventflows.txt lists a
	 *      vent only while it is open, and the 12-room model's auto-opening window (vent 27) is
	 *      absent from all 61 timesteps of a 600 s run.
	 * Reporting it open was the old behaviour and was wrong in the worst direction for an evacuation
	 * model: a window drawn open all run, with flow through it, that B-Risk had shut throughout.
	 *
	 * A cd = 0 vent is likewise SHUT for the whole run, for the same shape of reason: SR282 §4.6.2(d)
	 * says that is how B-Risk closes a vent sampled shut, and that the opening options "will not
	 * subsequently open" it. Its schedule is therefore irrelevant and must not be consulted - see
	 * DischargeCoefficient. Both closures are checked before the times, not after.
	 *
	 * NOTE: an earlier version of this comment claimed "every trigger is False across the test data".
	 * That is false - vent 27 of the 12-room export carries autoopenvent=True + triggerFR=True.
	 */
	bool IsOpenAtTime(double TimeSeconds) const
	{
		if (bAutoOpenVent || IsShutByDischargeCoefficient())
		{
			return false;
		}
		if (!bHasSchedule || (OpenTimeSeconds <= 0.0 && CloseTimeSeconds <= 0.0))
		{
			return true;
		}
		if (TimeSeconds < OpenTimeSeconds)
		{
			return false;
		}
		return CloseTimeSeconds <= OpenTimeSeconds || TimeSeconds < CloseTimeSeconds;
	}

	/**
	 * Thickness of the wall this opening is cut through, in metres, from openings[].hostThickness
	 * (added in the v2 export). 0 when the add-in did not supply it.
	 *
	 * This is the depth of the opening, so it is what a marker should be drawn with rather than a
	 * made-up constant. It also corroborates CentreMetres independently: the centre sits exactly
	 * hostThickness/2 outside the room's footprint polygon for 31 of the 34 openings in the 12-room
	 * model - measured 0.100 m against a declared 0.200 m wall. The three that do not are the
	 * wall-leakage vents, which sit at 0.000 because the add-in derives those from the room boundary
	 * rather than the wall centreline. That is an inconsistency in the source data, not here.
	 */
	double HostThicknessMetres = 0.0;

	/** True when openings[] marked this opening as giving onto the outside rather than another room. */
	bool bExterior = false;
};

/**
 * A single named time-series channel from a B-Risk zone CSV.
 * Each element of Values corresponds to the matching index in FBRiskZoneTable::TimeSeconds.
 * Example channels: ULT_1 (upper-layer temperature), HRR_1 (heat-release rate), HGT_1 (layer height).
 */
struct MOBIUSDATAIMPORTER_API FBRiskSeries
{
	/** Column header name as written in the CSV header row (e.g. "ULT_1", "HRR_1"). */
	FString Name;

	/** Physical unit string from the units row directly above the header (e.g. "C", "kW", "m"). */
	FString Unit;

	/** Sampled values aligned 1-to-1 with FBRiskZoneTable::TimeSeconds. */
	TArray<double> Values;
};

/**
 * All time-series data parsed from a single B-Risk zone CSV file.
 * Row 0 of the CSV holds unit strings; row 1 holds column names; rows 2+ are numeric samples.
 * The Time column is identified by name and stored separately in TimeSeconds.
 */
struct MOBIUSDATAIMPORTER_API FBRiskZoneTable
{
	/** Absolute path to the source CSV file on disk. */
	FString SourceCsvPath;

	/** Simulation time (seconds) for each sample row, in ascending order. */
	TArray<double> TimeSeconds;

	/** All non-Time series channels, in column order (excluding the Time column itself). */
	TArray<FBRiskSeries> Series;
};

/**
 * One room/time row of B-Risk's *calculated* tenability output (output1.xml).
 *
 * Unlike the raw zone CSV channels, these are values B-Risk itself computed at
 * the configured monitor height / egress path: FEDSum and FEDRadSum are
 * cumulative-since-t0 dose curves for the whole room; Visibility, layer height,
 * heat release and the two layer temperatures are instantaneous. Each bHas*
 * flag records whether the source tag was present so consumers never substitute
 * a fabricated value for a missing one.
 */
struct MOBIUSDATAIMPORTER_API FBRiskTenabilitySample
{
	/** Simulation time of this output sample, in seconds. */
	double SampleTimeSeconds = 0.0;

	/** Room heat release rate (kW) from <HeatRelease>. */
	double HeatReleaseKW = 0.0;

	/** Smoke layer interface height above the floor (m) from <layerheight>. */
	double LayerHeightM = 0.0;

	/** Upper-layer temperature (C) from <uppertemp>. */
	double UpperTemperatureC = 24.0;

	/** Lower-layer temperature (C) from <lowertemp>. */
	double LowerTemperatureC = 24.0;

	/** Visibility (m) from <Visibility>. */
	double VisibilityM = 20.0;

	/** Cumulative toxic FED (dimensionless) from <FEDSum>. */
	double FEDSum = 0.0;

	/** Cumulative thermal/radiant FED (dimensionless) from <FEDRadSum>. */
	double FEDRadSum = 0.0;

	bool bHasHeatRelease = false;
	bool bHasLayerHeight = false;
	bool bHasUpperTemperature = false;
	bool bHasLowerTemperature = false;
	bool bHasVisibility = false;
	bool bHasFEDSum = false;
	bool bHasFEDRadSum = false;
};

/**
 * All B-Risk calculated tenability output samples for a single room, parsed
 * from the <room id="..."> block of output1.xml. Samples are stored in
 * ascending time order, matching the <time> sub-blocks.
 */
struct MOBIUSDATAIMPORTER_API FBRiskTenabilityRoomTable
{
	/** Room identifier from <room id="...">, matching FBRiskRoomGeometry::RoomId. */
	int32 RoomId = INDEX_NONE;

	/** One entry per <time> block, ascending by SampleTimeSeconds. */
	TArray<FBRiskTenabilitySample> Samples;
};

/**
 * B-Risk analysis endpoints parsed from input1.xml. These are the tenability
 * limits B-Risk itself was configured with, used as defaults for the egress
 * tenability analysis. A bHas* flag is false when the tag was absent, so the
 * consumer logs a warning and falls back to a documented default rather than
 * silently trusting a zero.
 *
 * NOTE: EndpointTempRaw is the raw <endpoint_temp> value and is NOT a layer
 * temperature in Celsius (observed values are O(1000)); do not map it to a
 * Celsius criterion without resolving its semantics.
 */
struct MOBIUSDATAIMPORTER_API FBRiskTenabilityEndpoints
{
	double MonitorHeightM = 2.0;
	double EndpointVisibilityM = 10.0;
	double EndpointFED = 0.3;
	double EndpointRadiation = 0.3;
	double EndpointTempRaw = 0.0;

	bool bHasMonitorHeight = false;
	bool bHasEndpointVisibility = false;
	bool bHasEndpointFED = false;
	bool bHasEndpointRadiation = false;
	bool bHasEndpointTemp = false;
};

/**
 * Top-level container for everything parsed from a B-Risk scenario.
 * Created by FBRiskDataImporter::ImportScenarioFromSmv and passed to consumers.
 */
struct MOBIUSDATAIMPORTER_API FBRiskScenarioData
{
	/** Absolute path to the .smv manifest that was the import entry point. */
	FString SourceSmvPath;

	/** Absolute paths of every companion file (CSV etc.) resolved and loaded during import. */
	TArray<FString> ReferencedFiles;

	/**
	 * Absolute paths of RESULTS files the scenario expected but that are not on disk, in the order
	 * they were looked for. Non-empty means B-Risk has not been run for this model yet: the .smv and
	 * its geometry companions describe the building fine, but nothing simulated it.
	 *
	 * Deliberately distinct from a parse failure. A results file that is PRESENT and unreadable is
	 * still a hard error, because a corrupted run must not be able to present itself as a model that
	 * was simply never run.
	 */
	TArray<FString> MissingResultFiles;

	/**
	 * True when at least one zone table carrying time samples was loaded, i.e. this scenario can
	 * answer questions about what happened over time.
	 *
	 * False is a legitimate, fully-imported state - geometry only. Rooms, footprints, vents, fires
	 * and sprinklers are all populated; ZoneTables and TenabilityTables are empty. Consumers that
	 * need a time series must gate on this (or on UBRiskDataSubsystem::HasScenarioData, which is the
	 * same condition) rather than assuming a successful import implies results.
	 */
	bool bHasResultsData = false;

	/**
	 * Which XY mapping every consumer of this scenario's geometry must use. Decided ONCE here by
	 * the importer - Revit when Zones-data.json supplied footprints, SmokeviewSwap otherwise - and
	 * passed to BRiskCoord::MakeRoomFootprint and the vent solver.
	 *
	 * Scenario-level on purpose. It is not a per-room fact, and storing it per room would make a
	 * half-rotated scene expressible.
	 */
	BRiskCoord::ERoomFrame RoomFrame = BRiskCoord::ERoomFrame::SmokeviewSwap;

	/**
	 * input1.xml <soot_yield> (g of soot per g of fuel; 0.07 for pre-flashover VM2).
	 *
	 * Captured for reporting only. It is deliberately NOT used to derive the render albedo:
	 * single-scattering albedo is a property of the particulate and is roughly constant, whereas
	 * soot yield is a combustion parameter. SR282 gives no bridge between the two, and soot
	 * *concentration* already drives extinction via ULOD -> kappa, so feeding it into albedo as
	 * well would double-count density.
	 */
	double SootYieldGPerG = 0.0;

	/** True when input1.xml actually supplied soot_yield, so 0.0 is not mistaken for a real value. */
	bool bHasSootYield = false;

	/**
	 * input1.xml's ambient temperatures. ExteriorTempC is what the vent-flow routine uses for the
	 * outside of a room-to-exterior opening; it hardcoded 20 C until 2026-08-14, which cost up to
	 * 63.7% of the mass flow on the openings that matter most in a real model.
	 */
	FBRiskAmbientConditions Ambient;

	/** All rooms declared in ROOM blocks, in declaration order. */
	TArray<FBRiskRoomGeometry> Rooms;

	/** All fire sources declared in FIRE blocks, in declaration order. */
	TArray<FBRiskFireGeometry> Fires;

	/** All sprinklers declared in the companion sprinklers.xml, if present. */
	TArray<FBRiskSprinklerGeometry> Sprinklers;

	/** All horizontal vents/openings declared in VENTGEOM blocks, in declaration order. */
	TArray<FBRiskVentGeometry> Vents;

	/** One entry per zone CSV referenced by the ZONE block(s) in the SMV manifest. */
	TArray<FBRiskZoneTable> ZoneTables;

	/**
	 * B-Risk calculated tenability output (FEDSum/FEDRadSum/Visibility/...), one
	 * table per room, parsed from the companion output1.xml when present. Empty
	 * when the scenario has no output1.xml sibling.
	 */
	TArray<FBRiskTenabilityRoomTable> TenabilityTables;

	/** Analysis endpoints parsed from companion input1.xml. Defaults when absent. */
	FBRiskTenabilityEndpoints TenabilityEndpoints;
};

/**
 * Stateless facade for importing B-Risk fire-simulation scenario data.
 *
 * Usage:
 *   FBRiskScenarioData Data;
 *   FString Error;
 *   if (FBRiskDataImporter::ImportScenarioFromSmv(TEXT("path/to/scenario.smv"), Data, &Error))
 *   {
 *       // Data.Rooms, Data.Fires, Data.ZoneTables are populated.
 *   }
 */
class MOBIUSDATAIMPORTER_API FBRiskDataImporter
{
public:
	/**
	 * Parse a B-Risk SMV manifest and all companion files it references.
	 *
	 * The function:
	 *  1. Validates that SmvFilePath exists and has a .smv extension.
	 *  2. Reads and iterates keyword blocks (ZONE, ROOM, LABEL, FIRE).
	 *  3. Resolves each ZONE CSV path relative to the .smv directory and parses it.
	 *  4. Populates OutData with rooms, fire geometry, and zone time-series tables.
	 *
	 * Succeeds with GEOMETRY ONLY when the results files are absent rather than broken - a model
	 * that has not been run through B-Risk yet still describes a building worth showing. Those paths
	 * are listed in OutData.MissingResultFiles and OutData.bHasResultsData is false; callers should
	 * surface that to the user rather than treating it as a silent success. A results file that
	 * exists but does not parse remains a hard failure.
	 *
	 * @param SmvFilePath   Absolute (or engine-relative) path to the .smv manifest.
	 * @param OutData       Receives the parsed scenario on success, results or not.
	 * @param OutError      Optional; receives a human-readable error message on failure.
	 * @return true on success; false on a malformed .smv or an unparseable companion file.
	 */
	static bool ImportScenarioFromSmv(const FString& SmvFilePath, FBRiskScenarioData& OutData, FString* OutError = nullptr);
};
