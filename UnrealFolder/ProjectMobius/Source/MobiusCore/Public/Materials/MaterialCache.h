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

/**
 * Computes a hash value for the given object.
 *
 * @param Key The object for which the hash value will be calculated.
 * @return The computed hash value as a size_t.
 */
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

/**
 * Encapsulates the resolved parameters of a material, including textures, scalars, and vectors.
 * Represents the final evaluated values used to render the material.
 */
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
        /**
         * Cache system for managing and storing materials.
         */
        explicit FMaterialCache(UObject* InOwner = nullptr);

        /**
         * Sets the owner of the current instance.
         *
         * @param InOwner The new owner to be assigned.
         */
        void SetOwner(UObject* InOwner);

        /**
         * Retrieves a master material instance by its path. If the material is already loaded, it is returned from the cache.
         * Otherwise, the material is loaded, added to the cache, and then returned.
         *
         * @param MaterialPath The path to the master material to be loaded or fetched from the cache.
         * @return A pointer to the loaded or cached UMaterialInstanceConstant. Returns nullptr if loading fails.
         */
        UMaterialInstanceConstant* GetOrLoadMasterMaterial(const FString& MaterialPath);

        /**
         * Creates material instance dynamics (MIDs) using a cache mechanism. If a matching material instance
         * already exists in the cache, it is reused. Otherwise, a new instance is created, parameter settings
         * are applied, and it is added to the cache.
         *
         * @param InMaterial The base material interface to be used for creating the dynamic material instances.
         * @param MaterialPath The path to the associated master material used for constructing material instances.
         * @param bIsOpaque Specifies whether the material is opaque (true) or translucent (false), affecting type-specific caches.
         * @return An array of created or reused material instance dynamics matching the input material and parameters.
         */
        TArray<TObjectPtr<UMaterialInstanceDynamic>> CreateMaterialInstancesUsingCache(
                UMaterialInterface* InMaterial,
                const FString&      MaterialPath,
                bool                bIsOpaque);

        /**
         * Compares two material interfaces to determine if they are equivalent for the purpose of MID (Material Instance Dynamic) reuse.
         *
         * @param A The first material interface to compare.
         * @param B The second material interface to compare.
         * @param Tolerance The allowable difference for scalar parameter values between the two materials.
         * @return True if the two materials can be considered equivalent for MID reuse, otherwise false.
         */
        bool AreMaterialsEquivalentForMIDReuse(UMaterialInterface* A,
                                               UMaterialInterface* B,
                                               float               Tolerance = KINDA_SMALL_NUMBER) const;

        /**
         * Constructs a material instance dynamic (MID) key for the specified material, enabling reuse of shared
         * material instances based on a composite key system. Optionally returns resolved material parameters for cache optimization.
         *
         * @param InMaterial The base material interface for which the MID key is to be built.
         * @param MaterialPath The master material's path used in constructing the key.
         * @param bIsOpaque Specifies whether the material is opaque (true) or translucent (false), influencing the cache mapping.
         * @param OutKey The resulting composite key that uniquely identifies the material within the cache.
         * @param OutResolvedParams Optionally receives resolved material parameters used for parameter hashing.
         *                          Passing nullptr skips the population of resolved parameters.
         * @return True if the MID key was successfully built, otherwise false (e.g., if the input material is null
         *         or fails parameter resolution).
         */
        bool BuildMaterialMIDKey(
                UMaterialInterface*       InMaterial,
                const FString&            MaterialPath,
                bool                      bIsOpaque,
                FMaterialMIDKey&          OutKey,
                FResolvedMaterialParams* OutResolvedParams = nullptr) const;

        /** Clears all cached master materials and MIDs. */
        void Reset();

        /**
         * Computes a stable hash value for a set of resolved material parameters, ensuring that the order
         * of parameters does not affect the resulting hash. The method processes scalar, vector, and
         * texture parameters, combining their hashes into a single 64-bit value.
         *
         * @param Params The resolved material parameters, including scalar, vector, and texture values,
         *               used to compute the hash.
         * @return A 64-bit hash value representing the material parameters.
         */
        uint64 ComputeParamsHash(const FResolvedMaterialParams& Params) const;

private:
        /**
         * Resolves material parameter values from a given material and populates the provided structure with the results.
         *
         * @param Material The material interface from which to retrieve parameter values. If null, the operation will fail.
         * @param OutParams The structure that will be populated with scalar, vector, and texture parameter values.
         * @return True if the material parameters were successfully resolved; false otherwise.
         */
        bool ResolveMaterialParams(UMaterialInterface* Material, FResolvedMaterialParams& OutParams) const;

private:
        /** Represents the owner or controller of a specific object or resource. */
        TObjectPtr<UObject> Owner = nullptr;

        /** Cache mapping material names to corresponding constant material instance objects. */
        TMap<FName, TObjectPtr<UMaterialInstanceConstant>> MasterMaterialCache;

        /** Map storing reusable dynamic material instance references, keyed by a composite identifier. */
        TMap<FMaterialMIDKey, TObjectPtr<UMaterialInstanceDynamic>> SharedMIDByKey;

        /** Map that associates weak pointers to material interfaces with their corresponding opaque material key. */
        TMap<TWeakObjectPtr<UMaterialInterface>, FMaterialMIDKey> MaterialToOpaqueKeyCache;
        
        /** Mapping of weak material interface references to their corresponding translucent material instance keys. */
        TMap<TWeakObjectPtr<UMaterialInterface>, FMaterialMIDKey> MaterialToTranslucentKeyCache;

        /**
         * Cache that maps weak pointers to material interfaces with their resolved parameter data.
         * This allows for efficient reuse of resolved material parameters, reducing redundant computations.
         */
        TMap<TWeakObjectPtr<UMaterialInterface>, FResolvedMaterialParams> MaterialParamsCache;
};

