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
	 * @param SmvFilePath   Absolute (or engine-relative) path to the .smv manifest.
	 * @param OutData       Receives the fully parsed scenario on success.
	 * @param OutError      Optional; receives a human-readable error message on failure.
	 * @return true on success with OutData fully populated; false on any parse error.
	 */
	static bool ImportScenarioFromSmv(const FString& SmvFilePath, FBRiskScenarioData& OutData, FString* OutError = nullptr);
};
