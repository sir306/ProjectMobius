// DeformableQuadComponent.cpp
// UE 5.5-safe: manual-vertex-fetch with stable SRVs + RT-safe updates.

#include "Components/DeformableQuadComponent.h"

#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"
#include "LocalVertexFactory.h"
#include "RenderUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "GlobalRenderResources.h" // for null fallbacks if needed (GNullColorVertexBuffer)
#include "MaterialDomain.h"

// --------------------------- Buffer bundle ---------------------------

struct FDeformableQuadBuffers
{
	FPositionVertexBuffer    PositionBuffer;   // positions (SRV used by MVF)
	FStaticMeshVertexBuffer  StaticBuffer;     // tangents + UVs (SRVs used by MVF)
	FColorVertexBuffer       ColorBuffer;      // color (SRV used by MVF)
	FRawStaticIndexBuffer    IndexBuffer;

	FLocalVertexFactory      VertexFactory;

	explicit FDeformableQuadBuffers(ERHIFeatureLevel::Type FeatureLevel)
		: VertexFactory(FeatureLevel, "DeformableQuadVF")
	{}
};

// ---------------------------- Scene Proxy ----------------------------

class FDeformableQuadSceneProxy final : public FPrimitiveSceneProxy
{
public:
	explicit FDeformableQuadSceneProxy(const UDeformableQuadComponent* Comp)
		: FPrimitiveSceneProxy(Comp)
		, MaterialRelevance(Comp->GetMaterialRelevance(GetScene().GetFeatureLevel()))
		, FeatureLevel(GetScene().GetFeatureLevel())
	{
		Buffers = MakeUnique<FDeformableQuadBuffers>(FeatureLevel);

		// ---- CPU vertex data
		const FVector3f A = (FVector3f)Comp->Positions[0];
		const FVector3f B = (FVector3f)Comp->Positions[1];
		const FVector3f C = (FVector3f)Comp->Positions[2];
		const FVector3f D = (FVector3f)Comp->Positions[3];

		FVector3f Normal; FVector4f TangentX(1,0,0,1);
		UDeformableQuadComponent::BuildTangents(A,B,C,D, Normal, TangentX);
		const FVector3f TangentY =
			FVector3f::CrossProduct(Normal, FVector3f(TangentX.X,TangentX.Y,TangentX.Z)).GetSafeNormal();

		Buffers->PositionBuffer.Init(8);
		Buffers->StaticBuffer.SetUseFullPrecisionUVs(true);
		Buffers->StaticBuffer.Init(8, 1);
		Buffers->ColorBuffer.InitFromSingleColor(FColor::White, 8);

		static const FVector2f UVs[4] = { {0,0},{1,0},{1,1},{0,1} };
		const FVector3f Pos[4] = { A,B,C,D };

		// front (0..3)
		for (int32 i=0;i<4;++i)
		{
			Buffers->PositionBuffer.VertexPosition(i) = Pos[i];
			Buffers->StaticBuffer.SetVertexTangents(i,
				FVector3f(TangentX.X,TangentX.Y,TangentX.Z), TangentY, Normal);
			Buffers->StaticBuffer.SetVertexUV(i, 0, UVs[i]);
		}

		// back (4..7) – same positions/UVs, flipped basis for proper lighting
		const FVector3f NormalB   = -Normal;
		const FVector3f TangentXB = -FVector3f(TangentX.X,TangentX.Y,TangentX.Z);
		const FVector3f TangentYB = FVector3f::CrossProduct(NormalB, TangentXB).GetSafeNormal();

		for (int32 i=0;i<4;++i)
		{
			const int32 j = i + 4;
			Buffers->PositionBuffer.VertexPosition(j) = Pos[i];
			Buffers->StaticBuffer.SetVertexTangents(j, TangentXB, TangentYB, NormalB);
			Buffers->StaticBuffer.SetVertexUV(j, 0, UVs[i]);
		}

		TArray<uint32> Indices;
		//UDeformableQuadComponent::BuildIndices(Indices); //TODO:Update this method
		Indices = { 0,1,2, 0,2,3,    // front
			6,5,4, 7,6,4 };  // back (reversed)
		Buffers->IndexBuffer.AppendIndices(Indices.GetData(), Indices.Num());

		// ---- GPU resource init (GT async) — do NOT init VF yet
		BeginInitResource(&Buffers->PositionBuffer);
		BeginInitResource(&Buffers->StaticBuffer);
		BeginInitResource(&Buffers->ColorBuffer);
		BeginInitResource(&Buffers->IndexBuffer);

		// Ensure SRVs exist before we capture them (InitRHI executed on RT)
		{ FRenderCommandFence Fence; Fence.BeginFence(); Fence.Wait(); }

		// ---- Build FLocalVertexFactory::FDataType, including SRVs (MVF path)
		FLocalVertexFactory::FDataType Data;
		Buffers->PositionBuffer.BindPositionVertexBuffer(&Buffers->VertexFactory, Data);
		Buffers->StaticBuffer.BindTangentVertexBuffer(&Buffers->VertexFactory, Data);
		Buffers->StaticBuffer.BindPackedTexCoordVertexBuffer(&Buffers->VertexFactory, Data);
		Buffers->ColorBuffer.BindColorVertexBuffer(&Buffers->VertexFactory, Data);

		Data.NumTexCoords              = Buffers->StaticBuffer.GetNumTexCoords();
		Data.PositionComponentSRV      = Buffers->PositionBuffer.GetSRV();
		Data.TangentsSRV               = Buffers->StaticBuffer.GetTangentsSRV();
		Data.TextureCoordinatesSRV     = Buffers->StaticBuffer.GetTexCoordsSRV();
		Data.ColorComponentsSRV        = Buffers->ColorBuffer.GetColorComponentsSRV();

		// Defensive fallback (shouldn't be needed, but avoids editor flakiness)
		// if (!Data.ColorComponentsSRV)
		// {
		// 	extern ENGINE_API TGlobalResource<FNullColorVertexBuffer> GNullColorVertexBuffer;
		// 	Data.ColorComponentsSRV = GNullColorVertexBuffer.VertexBufferSRV;
		// }

		ensureAlwaysMsgf(Data.PositionComponentSRV,      TEXT("Position SRV null"));
		ensureAlwaysMsgf(Data.TangentsSRV,               TEXT("Tangents SRV null"));
		ensureAlwaysMsgf(Data.TextureCoordinatesSRV,     TEXT("TexCoord SRV null"));
		ensureAlwaysMsgf(Data.ColorComponentsSRV,        TEXT("Color SRV null"));

		// // ---- 1) Bind streams/SRVs on RT ...
		// ENQUEUE_RENDER_COMMAND(SetDeformableQuadVFData)(
		// 	[VF=&Buffers->VertexFactory, Data](FRHICommandListImmediate& RHICmdList)
		// 	{
		// 		VF->SetData(RHICmdList, Data);
		// 	});
		//
		// // ---- ... 2) Wait so SetData has executed ...
		// { FRenderCommandFence Fence; Fence.BeginFence(); Fence.Wait(); }
		//
		// // ---- ... 3) Now init the VF resource (UBO will see valid SRVs)
		// BeginInitResource(&Buffers->VertexFactory);
		//
		// // Optional: fence so first draw can happen immediately
		// { FRenderCommandFence Fence; Fence.BeginFence(); Fence.Wait(); }
		ENQUEUE_RENDER_COMMAND(InitDeformableQuadResources)(
			[B = Buffers.Get()](FRHICommandListImmediate& RHICmdList)
			{
				if (!B) return;

				FLocalVertexFactory::FDataType Data;

				B->PositionBuffer.BindPositionVertexBuffer(&B->VertexFactory, Data);
				B->StaticBuffer.BindTangentVertexBuffer(&B->VertexFactory, Data);
				B->StaticBuffer.BindPackedTexCoordVertexBuffer(&B->VertexFactory, Data);
				B->ColorBuffer.BindColorVertexBuffer(&B->VertexFactory, Data);

				Data.NumTexCoords          = B->StaticBuffer.GetNumTexCoords();

				// If you still want the MVF / SRV path, you can safely grab SRVs here
				Data.PositionComponentSRV  = B->PositionBuffer.GetSRV();
				Data.TangentsSRV           = B->StaticBuffer.GetTangentsSRV();
				Data.TextureCoordinatesSRV = B->StaticBuffer.GetTexCoordsSRV();
				Data.ColorComponentsSRV    = B->ColorBuffer.GetColorComponentsSRV();

				B->VertexFactory.SetData(RHICmdList, Data);
				B->VertexFactory.InitResource(RHICmdList);
			});
		
		Material = Comp->GetMaterial(0);
		if (!Material) { Material = UMaterial::GetDefaultMaterial(MD_Surface); }
	}

	virtual ~FDeformableQuadSceneProxy() override
	{
		auto ReleaseAllRT = [](FDeformableQuadBuffers* B)
		{
			if (!B) return;
			if (B->VertexFactory.IsInitialized())   B->VertexFactory.ReleaseResource();
			if (B->IndexBuffer.IsInitialized())     B->IndexBuffer.ReleaseResource();
			if (B->ColorBuffer.IsInitialized())     B->ColorBuffer.ReleaseResource();
			if (B->StaticBuffer.IsInitialized())    B->StaticBuffer.ReleaseResource();
			if (B->PositionBuffer.IsInitialized())  B->PositionBuffer.ReleaseResource();
		};

		if (IsInRenderingThread() || IsInParallelRenderingThread() || !GIsThreadedRendering)
		{
			// Renderer-owned path: release immediately on RT/PRT. No flushing, no fence.
			ReleaseAllRT(Buffers.Get());
		}
		else if (IsInGameThread())
		{
			// GT path: enqueue releases then fence so they finish before Buffers is destroyed.
			if (Buffers->VertexFactory.IsInitialized())   BeginReleaseResource(&Buffers->VertexFactory);
			if (Buffers->IndexBuffer.IsInitialized())     BeginReleaseResource(&Buffers->IndexBuffer);
			if (Buffers->ColorBuffer.IsInitialized())     BeginReleaseResource(&Buffers->ColorBuffer);
			if (Buffers->StaticBuffer.IsInitialized())    BeginReleaseResource(&Buffers->StaticBuffer);
			if (Buffers->PositionBuffer.IsInitialized())  BeginReleaseResource(&Buffers->PositionBuffer);

			FRenderCommandFence Fence;
			Fence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread); // stricter than RT; fine for teardown
			Fence.Wait();
		}
		else
		{
			// Unknown thread (rare: e.g., module shutdown). Hand buffers to RT for safe teardown.
			FDeformableQuadBuffers* Orphan = Buffers.Release(); // don't destroy on this thread
			ENQUEUE_RENDER_COMMAND(DestroyDeformableQuadBuffers)(
				[Orphan, ReleaseAllRT](FRHICommandListImmediate&){ ReleaseAllRT(Orphan); delete Orphan; });
		}

		// Never call FlushRenderingCommands() here.
	}


	// Dynamic path → submit FMeshBatch each frame
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!Buffers->VertexFactory.IsInitialized())
			return;

		FMaterialRenderProxy* MatProxy = Material->GetRenderProxy();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if (!(VisibilityMap & (1 << ViewIndex))) continue;

			FMeshBatch& Mesh = Collector.AllocateMesh();
			Mesh.VertexFactory = &Buffers->VertexFactory;
			Mesh.MaterialRenderProxy = MatProxy;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.CastShadow = true;
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;

			FMeshBatchElement& Batch = Mesh.Elements[0];
			Batch.IndexBuffer    = &Buffers->IndexBuffer;
			Batch.FirstIndex     = 0;
			Batch.NumPrimitives  = 4;
			Batch.MinVertexIndex = 0;
			Batch.MaxVertexIndex = 7;

			// Batch.PrimitiveUniformBuffer = CreatePrimitiveUniformBufferImmediate(
			// 	GetLocalToWorld(), GetBounds(), GetLocalBounds(), GetLocalBounds(),
			// 	ReceivesDecals(), false);
			
			Batch.PrimitiveUniformBuffer = GetUniformBuffer();
			Collector.AddMesh(ViewIndex, Mesh);
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance R;
		R.bDrawRelevance = IsShown(View);
		R.bShadowRelevance = IsShadowCast(View);
		R.bDynamicRelevance = true;
		R.bRenderInMainPass = ShouldRenderInMainPass();
		R.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		MaterialRelevance.SetPrimitiveViewRelevance(R);
		return R;
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + GetAllocatedSize();
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static int32 Unique = 0;
		return reinterpret_cast<SIZE_T>(&Unique);
	}

	// ---- Runtime deformation (RT): lock/unlock the SAME VB (no SRV churn)
	void UpdatePositions_RT(const FVector4f NewPos[4], FRHICommandListImmediate& RHICmdList)
	{
		// Convert to the layout used by FPositionVertexBuffer (FVector3f)
		FVector3f Tmp[8] = {
			FVector3f(NewPos[0].X, NewPos[0].Y, NewPos[0].Z),
			FVector3f(NewPos[1].X, NewPos[1].Y, NewPos[1].Z),
			FVector3f(NewPos[2].X, NewPos[2].Y, NewPos[2].Z),
			FVector3f(NewPos[3].X, NewPos[3].Y, NewPos[3].Z),
			// back side duplicates
			FVector3f(NewPos[0].X, NewPos[0].Y, NewPos[0].Z),
			FVector3f(NewPos[1].X, NewPos[1].Y, NewPos[1].Z),
			FVector3f(NewPos[2].X, NewPos[2].Y, NewPos[2].Z),
			FVector3f(NewPos[3].X, NewPos[3].Y, NewPos[3].Z),
		};
		
		// Keep the CPU copy in sync (optional, if you use it)
		for (int i=0;i<8;++i)
		{
			Buffers->PositionBuffer.VertexPosition(i) = Tmp[i];
		}

		// Write into the *existing* vertex buffer so the SRV stays valid.
		const uint32 Stride = Buffers->PositionBuffer.GetStride();           // usually sizeof(FVector3f)
		uint8* Dest = static_cast<uint8*>(
			RHICmdList.LockBuffer(Buffers->PositionBuffer.VertexBufferRHI,
								  /*Offset*/ 0,
								  /*Size*/   Stride * 8,
								  RLM_WriteOnly));

		for (int32 i=0; i<8; ++i)
		{
			FMemory::Memcpy(Dest + i*Stride, &Tmp[i], sizeof(FVector3f));
		}

		RHICmdList.UnlockBuffer(Buffers->PositionBuffer.VertexBufferRHI);
	}

private:
	TUniquePtr<FDeformableQuadBuffers> Buffers;
	UMaterialInterface*   Material = nullptr;
	FMaterialRelevance    MaterialRelevance;
	ERHIFeatureLevel::Type FeatureLevel;
};

// ----------------------------- Component --------------------------------

UDeformableQuadComponent::UDeformableQuadComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Default 100x100 centered
	Positions[0] = FVector4f(-50, -50, 0, 1);
	Positions[1] = FVector4f( 50, -50, 0, 1);
	Positions[2] = FVector4f( 50,  50, 0, 1);
	Positions[3] = FVector4f(-50,  50, 0, 1);
	LocalBounds = FBox((FVector)Positions[0], (FVector)Positions[0]);
}

void UDeformableQuadComponent::Initialize(float Width, float Height)
{
	const float HW = Width  * 0.5f;
	const float HH = Height * 0.5f;

	Positions[0] = FVector4f(-HW, -HH, 0, 1);
	Positions[1] = FVector4f( HW, -HH, 0, 1);
	Positions[2] = FVector4f( HW,  HH, 0, 1);
	Positions[3] = FVector4f(-HW,  HH, 0, 1);

	LocalBounds = FBox(ForceInit);
	for (int32 i=0;i<4;++i) { LocalBounds += (FVector)Positions[i]; }

	bInitialized = true;
	MarkRenderStateDirty();
}

void UDeformableQuadComponent::SetCorners(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
{
	Positions[0] = FVector4f((FVector3f)A, 1.f);
	Positions[1] = FVector4f((FVector3f)B, 1.f);
	Positions[2] = FVector4f((FVector3f)C, 1.f);
	Positions[3] = FVector4f((FVector3f)D, 1.f);

	LocalBounds = FBox(ForceInit);
	for (int32 i=0;i<4;++i) { LocalBounds += (FVector)Positions[i]; }
	UpdateBounds();
	MarkRenderTransformDirty();

	UpdateRenderPositions_RT(Positions);
}

void UDeformableQuadComponent::SetHeights(float H00, float H10, float H11, float H01, float ZScale)
{
	Positions[0].Z = H00 * ZScale;
	Positions[1].Z = H10 * ZScale;
	Positions[2].Z = H11 * ZScale;
	Positions[3].Z = H01 * ZScale;

	LocalBounds = FBox(ForceInit);
	for (int32 i=0;i<4;++i) { LocalBounds += (FVector)Positions[i]; }
	UpdateBounds();
	MarkRenderTransformDirty();

	UpdateRenderPositions_RT(Positions);
}

FPrimitiveSceneProxy* UDeformableQuadComponent::CreateSceneProxy()
{
	if (!bInitialized)
	{
		Initialize(100.f, 100.f);
	}
	return new FDeformableQuadSceneProxy(this);
}

FBoxSphereBounds UDeformableQuadComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(LocalBounds.TransformBy(LocalToWorld));
}

void UDeformableQuadComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDeformableQuadComponent::TickComponent(float X, ELevelTick LevelTick,
	FActorComponentTickFunction* ActorComponentTickFunction)
{
	Super::TickComponent(X, LevelTick, ActorComponentTickFunction);
}

void UDeformableQuadComponent::BuildIndices(TArray<uint32>& Out)
{
	Out = {0,1,2, 0,2,3};
}

void UDeformableQuadComponent::BuildTangents(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D,
                                             FVector3f& OutNormal, FVector4f& OutTangentX)
{
	const FVector3f E0 = (B - A);
	const FVector3f E1 = (D - A);
	OutNormal = FVector3f::CrossProduct(E0, E1).GetSafeNormal();
	FVector3f Tx = E0.GetSafeNormal();
	if (FMath::IsNearlyZero(Tx.SizeSquared())) { Tx = FVector3f(1,0,0); }
	OutTangentX = FVector4f(Tx.X, Tx.Y, Tx.Z, 1.f);
}

void UDeformableQuadComponent::UpdateRenderPositions_RT(const FVector4f NewPositions[4])
{
	if (!SceneProxy) return;

	struct FUpdateCmd { FPrimitiveSceneProxy* Proxy; FVector4f P[4]; };
	FUpdateCmd Cmd{ SceneProxy, { NewPositions[0], NewPositions[1], NewPositions[2], NewPositions[3] } };

	ENQUEUE_RENDER_COMMAND(UpdateDeformableQuadPositions)(
		[Cmd](FRHICommandListImmediate& RHICmdList)
		{
			auto* Proxy = static_cast<FDeformableQuadSceneProxy*>(Cmd.Proxy);
			Proxy->UpdatePositions_RT(Cmd.P, RHICmdList);
		});
}
