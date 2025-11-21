#include "Materials/MaterialCache.h"

#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Engine/Texture.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FMaterialCache::FMaterialCache(UObject* InOwner)
        : Owner(InOwner)
{
}

void FMaterialCache::SetOwner(UObject* InOwner)
{
        Owner = InOwner;
}

UMaterialInstanceConstant* FMaterialCache::GetOrLoadMasterMaterial(const FString& MaterialPath)
{
        // Use the path as a key. FName is cheap to compare/hash.
        const FName Key(*MaterialPath);

        // check cache to see if already loaded
        if (TObjectPtr<UMaterialInstanceConstant>* Found = MasterMaterialCache.Find(Key))
        {
                return Found->Get();
        }

        // not loaded so attempt to load the material
        UMaterialInstanceConstant* LoadedMaterial =
                LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);

        if (!LoadedMaterial)
        {
                UE_LOG(LogTemp, Error, TEXT("Failed to load master material: %s"), *MaterialPath);
                return nullptr;
        }

        // add to cache
        MasterMaterialCache.Add(Key, LoadedMaterial);

        return LoadedMaterial;
}

TArray<TObjectPtr<UMaterialInstanceDynamic>> FMaterialCache::CreateMaterialInstancesUsingCache(
        UMaterialInterface* InMaterial,
        const FString&      MaterialPath,
        bool                bIsOpaque)
{
        TArray<TObjectPtr<UMaterialInstanceDynamic>> Out;

        TRACE_CPUPROFILER_EVENT_SCOPE_STR("CreateMaterialInstancesUsingCache - called");

        // ---- 1) Resolve the params for the incoming material (with optional cache reuse) ----
        FResolvedMaterialParams* CachedParams = MaterialParamsCache.Find(InMaterial);

        FResolvedMaterialParams ResolvedParams = CachedParams ? *CachedParams : FResolvedMaterialParams{};
        if (!CachedParams)
        {
                if (!ResolveMaterialParams(InMaterial, ResolvedParams))
                {
                        return Out;
                }
                MaterialParamsCache.Add(InMaterial, ResolvedParams);
        }

        // ---- 2) Check per-type cache (opaque/translucent) first ----
        TMap<TWeakObjectPtr<UMaterialInterface>, FMaterialMIDKey>& PerTypeCache = bIsOpaque
                                                                                           ? MaterialToOpaqueKeyCache
                                                                                           : MaterialToTranslucentKeyCache;

        if (const FMaterialMIDKey* ExistingKey = PerTypeCache.Find(InMaterial))
        {
                // Already mapped – return that MID
                if (TObjectPtr<UMaterialInstanceDynamic>* ExistingMID = SharedMIDByKey.Find(*ExistingKey))
                {
                        Out.Add(ExistingMID->Get());
                        return Out;
                }
        }

        // ---- 3) Build the key for this master + type using the shared params ----
        FMaterialMIDKey Key;
        Key.MasterPathKey = FName(*MaterialPath);
        Key.BaseMaterial  = InMaterial->GetMaterial();
        Key.bIsOpaque     = bIsOpaque;
        Key.ParamsHash    = ComputeParamsHash(ResolvedParams);

        // See if some *other* material resolved to the same parameter set already
        if (TObjectPtr<UMaterialInstanceDynamic>* ExistingMID = SharedMIDByKey.Find(Key))
        {
                PerTypeCache.Add(InMaterial, Key);
                Out.Add(ExistingMID->Get());
                return Out;
        }

        // ---- 4) Create the new shared MID for this parameter set ----
        UMaterialInstanceConstant* TemplateMIC = GetOrLoadMasterMaterial(MaterialPath);
        if (!TemplateMIC)
        {
                return Out;
        }

        UMaterialInstanceDynamic* DynamicMaterial =
                UMaterialInstanceDynamic::Create(TemplateMIC, Owner.Get());
        if (!DynamicMaterial)
        {
                return Out;
        }

        // Apply the cached params (one place to maintain)
        for (const TPair<FName, float>& Pair : ResolvedParams.ScalarParams)
        {
                DynamicMaterial->SetScalarParameterValue(Pair.Key, Pair.Value);
        }

        for (const TPair<FName, FLinearColor>& Pair : ResolvedParams.VectorParams)
        {
                DynamicMaterial->SetVectorParameterValue(Pair.Key, Pair.Value);
        }

        for (const TPair<FName, TObjectPtr<UTexture>>& Pair : ResolvedParams.TextureParams)
        {
                DynamicMaterial->SetTextureParameterValue(Pair.Key, Pair.Value);
        }

        // ---- 5) Cache it ----
        SharedMIDByKey.Add(Key, DynamicMaterial);
        PerTypeCache.Add(InMaterial, Key);
        Out.Add(DynamicMaterial);

        return Out;
}

bool FMaterialCache::ResolveMaterialParams(UMaterialInterface* Material, FResolvedMaterialParams& OutParams) const
{
        OutParams.ScalarParams.Reset();
        OutParams.VectorParams.Reset();
        OutParams.TextureParams.Reset();

        if (!Material)
        {
                return false;
        }

        TArray<FMaterialParameterInfo> ScalarInfos;
        TArray<FMaterialParameterInfo> VectorInfos;
        TArray<FMaterialParameterInfo> TextureInfos;
        TArray<FGuid> ScalarGuids;
        TArray<FGuid> VectorGuids;
        TArray<FGuid> TextureGuids;

        Material->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);
        Material->GetAllVectorParameterInfo(VectorInfos, VectorGuids);
        Material->GetAllTextureParameterInfo(TextureInfos, TextureGuids);

        for (const FMaterialParameterInfo& Info : ScalarInfos)
        {
                float Value = 0.0f;
                if (Material->GetScalarParameterValue(Info, Value))
                {
                        OutParams.ScalarParams.Add(Info.Name, Value);
                }
        }

        for (const FMaterialParameterInfo& Info : VectorInfos)
        {
                FLinearColor Value;
                if (Material->GetVectorParameterValue(Info, Value))
                {
                        OutParams.VectorParams.Add(Info.Name, Value);
                }
        }

        for (const FMaterialParameterInfo& Info : TextureInfos)
        {
                UTexture* Value = nullptr;
                if (Material->GetTextureParameterValue(Info, Value))
                {
                        OutParams.TextureParams.Add(Info.Name, Value);
                }
        }

        return true;
}

bool FMaterialCache::AreMaterialsEquivalentForMIDReuse(UMaterialInterface* A, UMaterialInterface* B,
                                                       float Tolerance) const
{
        if (A == B)
        {
                return true; // exactly the same object
        }

        if (!A || !B)
        {
                return false;
        }

        // Strong guard: base material must match.
        if (A->GetMaterial() != B->GetMaterial())
        {
                return false;
        }

        FResolvedMaterialParams ParamsA;
        FResolvedMaterialParams ParamsB;

        if (!ResolveMaterialParams(A, ParamsA) ||
                !ResolveMaterialParams(B, ParamsB))
        {
                return false;
        }

        // Quick size checks
        if (ParamsA.ScalarParams.Num()  != ParamsB.ScalarParams.Num() ||
                ParamsA.VectorParams.Num()  != ParamsB.VectorParams.Num() ||
                ParamsA.TextureParams.Num() != ParamsB.TextureParams.Num())
        {
                return false;
        }

        // Scalars
        for (const TPair<FName, float>& PairA : ParamsA.ScalarParams)
        {
                const float* BValue = ParamsB.ScalarParams.Find(PairA.Key);
                if (!BValue)
                {
                        return false;
                }

                if (FMath::Abs(PairA.Value - *BValue) > Tolerance)
                {
                        return false;
                }
        }

        // Vectors
        for (const TPair<FName, FLinearColor>& PairA : ParamsA.VectorParams)
        {
                const FLinearColor* BValue = ParamsB.VectorParams.Find(PairA.Key);
                if (!BValue)
                {
                        return false;
                }

                const FLinearColor& CA = PairA.Value;
                const FLinearColor& CB = *BValue;

                if (FMath::Abs(CA.R - CB.R) > Tolerance ||
                        FMath::Abs(CA.G - CB.G) > Tolerance ||
                        FMath::Abs(CA.B - CB.B) > Tolerance ||
                        FMath::Abs(CA.A - CB.A) > Tolerance)
                {
                        return false;
                }
        }

        // Textures – pointer equality is fine here
        for (const TPair<FName, TObjectPtr<UTexture>>& PairA : ParamsA.TextureParams)
        {
                const TObjectPtr<UTexture>* BValue = ParamsB.TextureParams.Find(PairA.Key);
                if (!BValue)
                {
                        return false;
                }

                if (PairA.Value.Get() != BValue->Get())
                {
                        return false;
                }
        }

        return true;
}

bool FMaterialCache::BuildMaterialMIDKey(UMaterialInterface* InMaterial, const FString& MaterialPath,
                                         bool bIsOpaque, FMaterialMIDKey& OutKey,
                                         FResolvedMaterialParams* OutResolvedParams) const
{
        if (!InMaterial)
        {
                return false;
        }

        FResolvedMaterialParams Params;
        if (OutResolvedParams)
        {
                *OutResolvedParams = FResolvedMaterialParams{}; // reset the cache
        }

        // Try to reuse cached parameters if available to avoid re-querying the material
        if (const FResolvedMaterialParams* CachedParams = MaterialParamsCache.Find(InMaterial))
        {
                Params = *CachedParams;
        }
        else if (!ResolveMaterialParams(InMaterial, Params))
        {
                return false;
        }

        OutKey.MasterPathKey = FName(*MaterialPath);
        OutKey.BaseMaterial  = InMaterial->GetMaterial();
        OutKey.bIsOpaque     = bIsOpaque;
        OutKey.ParamsHash    = ComputeParamsHash(Params);

        if (OutResolvedParams)
        {
                *OutResolvedParams = MoveTemp(Params);
        }

        return true;
}

uint64 FMaterialCache::ComputeParamsHash(const FResolvedMaterialParams& Params) const
{
        auto Mix = [](uint64& Hash, uint64 Value)
        {
                // Simple 64-bit hash mixing function similar to boost::hash_combine
                Hash ^= Value + 0x9e3779b97f4a7c15ull + (Hash << 6) + (Hash >> 2);
        };

        auto MixName = [&Mix](uint64& Hash, const FName& Name)
        {
                Mix(Hash, (uint64)GetTypeHash(Name));
        };

        auto MixFloat = [&Mix](uint64& Hash, float Value)
        {
                uint32 Bits = 0;
                FMemory::Memcpy(&Bits, &Value, sizeof(float));
                Mix(Hash, (uint64)Bits);
        };

        uint64 Hash = 1469598103934665603ull; // FNV offset basis

        // We'll sort parameter names to make hashing order-independent.
        TArray<FName> Names;

        // --- Scalars ---
        Names.Reset();
        Names.Reserve(Params.ScalarParams.Num());
        Params.ScalarParams.GenerateKeyArray(Names);
        Names.Sort(FNameLexicalLess());

        for (FName Name : Names)
        {
                MixName(Hash, Name);
                if (const float* Val = Params.ScalarParams.Find(Name))
                {
                        MixFloat(Hash, *Val);
                }
        }

        // --- Vectors ---
        Names.Reset();
        Names.Reserve(Params.VectorParams.Num());
        Params.VectorParams.GenerateKeyArray(Names);
        Names.Sort(FNameLexicalLess());

        for (FName Name : Names)
        {
                MixName(Hash, Name);
                if (const FLinearColor* Color = Params.VectorParams.Find(Name))
                {
                        MixFloat(Hash, Color->R);
                        MixFloat(Hash, Color->G);
                        MixFloat(Hash, Color->B);
                        MixFloat(Hash, Color->A);
                }
        }

        // --- Textures ---
        Names.Reset();
        Names.Reserve(Params.TextureParams.Num());
        Params.TextureParams.GenerateKeyArray(Names);
        Names.Sort(FNameLexicalLess());

        for (FName Name : Names)
        {
                MixName(Hash, Name);

                // NOTE: map value type is TObjectPtr<UTexture>
                if (const TObjectPtr<UTexture>* TexPtr = Params.TextureParams.Find(Name))
                {
                        if (UTexture* Texture = TexPtr->Get())
                        {
                                // Hash the pointer value – good enough for identity within this process.
                                const uint64 PtrBits = (uint64)(UPTRINT)Texture;
                                Mix(Hash, PtrBits);
                        }
                        else
                        {
                                // Distinguish explicit null from "no entry"
                                Mix(Hash, 0ull);
                        }
                }
        }

        return Hash;
}

