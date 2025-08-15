#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "DeformableQuadComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MOBIUSCORE_API UDeformableQuadComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UDeformableQuadComponent();

	/** Build a centered quad in local space */
	UFUNCTION(BlueprintCallable, Category="DeformableQuad")
	void Initialize(float Width = 100.f, float Height = 100.f);

	/** Set corner positions in local space (A,B,C,D clockwise) */
	UFUNCTION(BlueprintCallable, Category="DeformableQuad")
	void SetCorners(const FVector& A, const FVector& B, const FVector& C, const FVector& D);

	/** Simple height tweak per UV corner (0..1) */
	UFUNCTION(BlueprintCallable, Category="DeformableQuad")
	void SetHeights(float H00, float H10, float H11, float H01, float ZScale = 100.f);

	// UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return 1; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float, ELevelTick, FActorComponentTickFunction*) override;

private:
	UPROPERTY()
	FVector4f Positions[4];            // A,B,C,D

	FBox   LocalBounds;
	bool   bInitialized = false;

	static void BuildIndices(TArray<uint32>& Out);
	static void BuildTangents(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D,
							  FVector3f& OutNormal, FVector4f& OutTangentX);

	/** Enqueue a render-thread update for the position buffer */
	void UpdateRenderPositions_RT(const FVector4f NewPositions[4]);
	friend class FDeformableQuadSceneProxy;
};

