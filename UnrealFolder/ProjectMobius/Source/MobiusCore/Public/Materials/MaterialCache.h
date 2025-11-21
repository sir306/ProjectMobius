#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"        // FName + GetTypeHash(FName)
#include "UObject/WeakObjectPtr.h"    // TWeakObjectPtr hashing (for safety)
#include "MaterialCache.generated.h"

class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterial;
class UTexture;
class UObject;

/** Composite key for shared MID reuse. */
USTRUCT()
struct MOBIUSCORE_API FMaterialMIDKey
{
        GENERATED_BODY()

        /** which master (opaque/translucent) */
        UPROPERTY()
        FName MasterPathKey{ NAME_None };

        // Underlying base material – strong guard that we're comparing apples with apples
        UPROPERTY()
        TWeakObjectPtr<UMaterial> BaseMaterial;

        UPROPERTY()
        bool bIsOpaque = true;

        // Hash of all scalar / vector / texture parameters
        UPROPERTY()
        uint64 ParamsHash = 0;

        bool operator==(const FMaterialMIDKey& Other) const
        {
                return MasterPathKey == Other.MasterPathKey
                        && BaseMaterial == Other.BaseMaterial
                        && bIsOpaque == Other.bIsOpaque
                        && ParamsHash == Other.ParamsHash;
        }
};

// Global hash function so FMaterialMIDKey can be used as a TMap key.
FORCEINLINE uint32 GetTypeHash(const FMaterialMIDKey& Key)
{
        auto Mix = [](uint64& H, uint64 V)
        {
                H ^= V + 0x9e3779b97f4a7c15ull + (H << 6) + (H >> 2);
        };

        uint64 H = 1469598103934665603ull;

        Mix(H, GetTypeHash(Key.MasterPathKey));
        Mix(H, (uint64)(UPTRINT)Key.BaseMaterial.Get());
        Mix(H, (uint64)Key.bIsOpaque);
        Mix(H, Key.ParamsHash);

        return (uint32)(H ^ (H >> 32));
}

USTRUCT()
struct MOBIUSCORE_API FResolvedMaterialParams
{
        GENERATED_BODY()

        UPROPERTY()
        TMap<FName, float>                ScalarParams;

        UPROPERTY()
        TMap<FName, FLinearColor>         VectorParams;

        UPROPERTY()
        TMap<FName, TObjectPtr<UTexture>> TextureParams;
};

/**
 * Cache responsible for master material loading and sharing MID instances across equivalent inputs.
 */
class MOBIUSCORE_API FMaterialCache
{
public:
        explicit FMaterialCache(UObject* InOwner = nullptr);

        void SetOwner(UObject* InOwner);

        UMaterialInstanceConstant* GetOrLoadMasterMaterial(const FString& MaterialPath);

        TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateMaterialInstancesUsingCache(
                UMaterialInterface* InMaterial,
                const FString&      MaterialPath,
                bool                bIsOpaque);

        bool AreMaterialsEquivalentForMIDReuse(UMaterialInterface* A,
                                              UMaterialInterface* B,
                                              float               Tolerance = KINDA_SMALL_NUMBER) const;

        bool BuildMaterialMIDKey(
                UMaterialInterface*       InMaterial,
                const FString&            MaterialPath,
                bool                      bIsOpaque,
                FMaterialMIDKey&          OutKey,
                FResolvedMaterialParams* OutResolvedParams = nullptr) const;

        uint64 ComputeParamsHash(const FResolvedMaterialParams& Params) const;

private:
        bool ResolveMaterialParams(UMaterialInterface* Material, FResolvedMaterialParams& OutParams) const;

private:
        TObjectPtr<UObject> Owner = nullptr;

        // Cache of master MICs so we only LoadObject them once per path.
        TMap<FName, TObjectPtr<UMaterialInstanceConstant>> MasterMaterialCache;

        // New: map from parameter signature → shared MID
        TMap<FMaterialMIDKey, TObjectPtr<UMaterialInstanceDynamic>> SharedMIDByKey;

        // One cache per “family” of master materials
        TMap<TWeakObjectPtr<UMaterialInterface>, FMaterialMIDKey> MaterialToOpaqueKeyCache;
        TMap<TWeakObjectPtr<UMaterialInterface>, FMaterialMIDKey> MaterialToTranslucentKeyCache;

        // Optional: cache resolved params so we don’t recompute them twice
        TMap<TWeakObjectPtr<UMaterialInterface>, FResolvedMaterialParams> MaterialParamsCache;
};

