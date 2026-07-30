// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// SlateVectorArtQuadTest.cpp
//
// Guards every precondition a USlateVectorArtData asset must meet for SMeshWidget to actually draw it.
// All of them fail SILENTLY today: SMeshWidget::OnPaint skips a mesh whose RenderingResourceHandle is
// invalid or whose vertex/index arrays are empty, and while it does log a warning for those two, the
// worst case logs nothing at all.
//
// That worst case is the reason this test exists. StaticMeshToSlateRenderData
// (Engine/Source/Runtime/UMG/Private/Slate/SlateVectorArtData.cpp) builds each Slate vertex as
//
//     FVector2f(Position.X, Position.Y)
//
// discarding Z entirely. A source quad authored as an upright billboard - in the XZ or YZ plane, which
// is the natural way to author one for a camera-facing marker - therefore collapses to a zero-area
// line. The asset still passes every validity check the engine makes (material present, 4 vertices,
// 6 indices, no warning emitted) and draws absolutely nothing, which is indistinguishable on screen
// from a data bug much further upstream. The 2D extent is the only place that shows it, so assert on it.
//
// Covers both SVADs the in-world tenability overlay registers, because they are drawn by the SAME
// SMeshWidget and a difference between them is exactly what a marker-not-drawing bug looks like:
//   SVAD_AgentEgressTenability   - the per-agent tenability bar
//   SVAD_TenabilityFailMarker    - the fail marker icon quad
//
// Deliberately reads only serialised properties (GetVertexData / GetIndexData / GetMaterial /
// GetDesiredSize). It does NOT call EnsureValidData(), which would re-derive from the source static
// mesh and needs render data that does not exist under -nullrhi.
//
// Run from the Session Frontend (search "ProjectMobius.Render") or:
//   MobiusPerf\RunTests.ps1
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Materials/Material.h"
#include "MaterialDomain.h" // enum EMaterialDomain / MD_UI (Material.h only forward-declares it)
#include "Materials/MaterialInterface.h"
#include "Slate/SlateVectorArtData.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSlateVectorArtQuadDrawableTest,
	"ProjectMobius.Render.SlateVectorArtQuadIsDrawable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSlateVectorArtQuadDrawableTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Label;
		const TCHAR* AssetPath;
	};

	static const FCase Cases[] = {
		{
			TEXT("tenability bar"),
			TEXT("/Game/01_Dev/Widgets/LevelComponents/EgressMetrics/Agent-Tenability/"
				"SVAD_AgentEgressTenability.SVAD_AgentEgressTenability")
		},
		{
			TEXT("fail marker quad"),
			TEXT("/Game/01_Dev/Widgets/WidgetMaterials/WorldWidgetMats/TenabilityFailMarkers/"
				"SVAD_TenabilityFailMarker.SVAD_TenabilityFailMarker")
		},
	};

	for (const FCase& Case : Cases)
	{
		USlateVectorArtData* Asset = LoadObject<USlateVectorArtData>(nullptr, Case.AssetPath);
		if (!TestNotNull(*FString::Printf(TEXT("%s asset loads"), Case.Label), Asset))
		{
			continue;
		}

		// SMeshWidget::AddMesh only builds a Brush - and therefore only acquires a valid
		// RenderingResourceHandle - when GetMaterial() is non-null. Without it OnPaint skips the mesh.
		TestNotNull(
			*FString::Printf(TEXT("%s has a material (else SMeshWidget acquires no resource handle)"),
				Case.Label),
			Asset->GetMaterial());

		const int32 NumVerts = Asset->GetVertexData().Num();
		const int32 NumIndices = Asset->GetIndexData().Num();

		// OnPaint requires VertexData.Num() > 0 && IndexData.Num() > 0. Empty means the bake never ran,
		// or the source mesh had more than one section (StaticMeshToSlateRenderData skips ALL population
		// in that case and warns on LogUMG).
		TestTrue(
			*FString::Printf(TEXT("%s has vertices (got %d)"), Case.Label, NumVerts),
			NumVerts > 0);
		TestTrue(
			*FString::Printf(TEXT("%s has indices (got %d)"), Case.Label, NumIndices),
			NumIndices > 0);
		TestTrue(
			*FString::Printf(TEXT("%s index count is whole triangles (got %d)"), Case.Label, NumIndices),
			NumIndices % 3 == 0);

		// The silent one. See the file header: Z is discarded, so an upright quad has zero extent on one
		// axis and cannot rasterise. Reported with actual values so a failure names the collapsed axis.
		const FVector2D DesiredSize = Asset->GetDesiredSize();
		TestTrue(
			*FString::Printf(
				TEXT("%s spans the X axis - a quad authored in the YZ plane collapses to a line "
					"because Slate keeps only Position.X/Y (extent %.3f x %.3f)"),
				Case.Label, DesiredSize.X, DesiredSize.Y),
			DesiredSize.X > UE_KINDA_SMALL_NUMBER);
		TestTrue(
			*FString::Printf(
				TEXT("%s spans the Y axis - a quad authored in the XZ plane collapses to a line "
					"because Slate keeps only Position.X/Y (extent %.3f x %.3f)"),
				Case.Label, DesiredSize.X, DesiredSize.Y),
			DesiredSize.Y > UE_KINDA_SMALL_NUMBER);

		// The other silent one, and the reason a material's THUMBNAIL is no evidence it will draw here.
		// Slate renders only Material Domain "User Interface" (MD_UI). An MD_Surface material still gets
		// a valid brush and resource handle, still thumbnails correctly, and still draws nothing at all
		// through SMeshWidget. Materials created programmatically default to MD_Surface, so this is the
		// easy mistake to make and the hardest to see.
		if (const UMaterialInterface* MaterialInterface = Asset->GetMaterial())
		{
			const UMaterial* BaseMaterial = MaterialInterface->GetMaterial();
			if (TestNotNull(
				*FString::Printf(TEXT("%s material resolves a base UMaterial"), Case.Label),
				BaseMaterial))
			{
				TestEqual(
					*FString::Printf(
						TEXT("%s material domain must be MD_UI - Slate does not render any other "
							"domain, however correct the thumbnail looks"),
						Case.Label),
					static_cast<int32>(BaseMaterial->MaterialDomain.GetValue()),
					static_cast<int32>(MD_UI));
			}

			// THE one that matters for an INSTANCED Slate mesh, and the least obvious of the lot.
			// SlateVertexShader.usf does NOT apply the per-instance position or scale itself. It hands the
			// instance FVector4 to the material as UV channels only - InstanceParam.xy as UV2 (position),
			// InstanceParam.zw as UV3 (scale, base address) - and then at line 119 does
			//
			//     WorldPosition.xyz = GetMaterialWorldPositionOffsetRaw(VertexParameters);
			//
			// REPLACING the vertex position outright. So the material's World Position Offset is the only
			// thing that can place an instance. A material without one draws every instance at the raw
			// mesh position: one quad at the widget's coordinate origin, half of it off-screen, while the
			// instance buffer reports the correct count and every C++-side check passes. Indistinguishable
			// from "nothing was emitted" unless you know to look here.
			TestTrue(
				*FString::Printf(
					TEXT("%s material must have World Position Offset connected - Slate applies the "
						"per-instance position/scale ONLY through WPO (UV2 = position, UV3 = scale), so "
						"without it every instance collapses onto the widget origin"),
					Case.Label),
				BaseMaterial != nullptr && BaseMaterial->HasVertexPositionOffsetConnected());

			// Masked is legal here but wrong for mask-only art, and it fails in a way that looks like a
			// material bug rather than a blend bug. SlateRHIRenderingPolicy::GetMaterialBlendState maps
			// BLEND_Masked to TStaticBlendState<> - fully opaque, no alpha blending - so a marker whose
			// art is a coverage mask over transparency paints its whole quad as a solid block instead of
			// compositing over the scene. The tenability bar alongside it uses Translucent. Warned rather
			// than failed: it needs a material asset edit in the editor, and it cannot make a marker
			// invisible, only visibly wrong.
			if (MaterialInterface->GetBlendMode() == BLEND_Masked)
			{
				AddWarning(FString::Printf(
					TEXT("%s material '%s' is BLEND_Masked. Slate maps that to an opaque blend state, so "
						"mask-only art will paint as a solid block. Prefer BLEND_Translucent with the "
						"coverage driving Opacity instead of Opacity Mask."),
					Case.Label,
					*MaterialInterface->GetName()));
			}

			AddInfo(FString::Printf(
				TEXT("%s: extent %.3f x %.3f, %d verts, %d indices, material '%s', domain %d, blend %d, WPO %s"),
				Case.Label,
				DesiredSize.X,
				DesiredSize.Y,
				NumVerts,
				NumIndices,
				*MaterialInterface->GetName(),
				BaseMaterial ? static_cast<int32>(BaseMaterial->MaterialDomain.GetValue()) : -1,
				static_cast<int32>(MaterialInterface->GetBlendMode()),
				(BaseMaterial && BaseMaterial->HasVertexPositionOffsetConnected()) ? TEXT("yes") : TEXT("NO")));
		}
	}

	return true;
}

#endif // !UE_BUILD_SHIPPING
