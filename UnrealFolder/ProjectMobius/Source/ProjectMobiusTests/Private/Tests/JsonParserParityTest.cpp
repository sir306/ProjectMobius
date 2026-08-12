// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// JsonParserParityTest.cpp
//
// Perf task A7 contract: the two JSON parse engines behind FMobiusAgentDataImporter — simdjson
// (raw UTF-8 SAX) and the engine TJsonReader pull-parser (fallback) — must produce BIT-IDENTICAL
// FMobiusAgentSimulationData for the same document. Floats are compared by bit pattern, strings
// case-sensitively. Also locks the shared shape semantics (skip rules, canonical-key preference,
// timestep indexing, MaxNumEntities fallback) against ground-truth values, and the encoding split:
// UTF-8 BOM stays on simdjson, UTF-16 fails simdjson and lands on the pull-parser via
// ImportAgentFile's retry.
//
// Run: UnrealEditor ProjectMobius.uproject -ExecCmds="Automation RunTests ProjectMobius.SimData" -log
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MobiusAgentDataImporter.h"

namespace
{
	uint32 FloatBits(float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	/**
	 * Edge-case document. Deliberate traps:
	 *  - metadata: "is_si" BEFORE "isSI" (canonical spelling must win in both parsers), unknown
	 *    scalar/object/array keys that must be skipped, wrong-typed "duration" duplicate is absent —
	 *    strict typing is covered by "isDeg": 1 (number, must NOT set the bool).
	 *  - entities: simTimeS as string AND as number; escaped text (quote, backslash, \u00e9);
	 *    id 2.6 (RoundHalfFromZero -> 3); map out of int32 range (ignored, stays 0); a bare number
	 *    and a null in the array (skipped, no entity added); unknown nested container in an entity.
	 *  - simulation: element 1 is a string (skipped but still advances the timestep index), element
	 *    2 has no "samples", element 3 has a wrong-typed "samples" -> sample TimestepIndex sequence
	 *    must be 0,0,4; wrong-typed "position" leaves zeros; high-precision doubles for the float
	 *    bit-compare.
	 */
	const TCHAR* GetEdgeCaseJson()
	{
		return TEXT(R"({
	"format_version": 3,
	"vendor": { "tool": "synthetic", "revs": [1, 2, 3] },
	"metadata": {
		"is_si": false,
		"isSI": true,
		"isDeg": 1,
		"duration": 12.3456789,
		"sampling_rate": 0.1,
		"max_num_entities": 5000,
		"calibration": { "matrix": [[1.0, 0.0], [0.0, 1.0]] }
	},
	"entities": [
		{ "id": 1, "name": "walker \"one\" \u00e9\\", "simTimeS": "12.5", "max_speed": 1.42, "m_plane": "floor_0", "map": 2 },
		42,
		{ "id": 2.6, "simTimeS": 3.75, "map": 3000000000, "tags": { "vip": true } },
		null,
		{ "name": "minimal" }
	],
	"simulation": [
		{ "note": "ts0", "samples": [
			{ "entity": 1, "rotation": -273.1500001, "speed": 1e-7, "mode": "walk", "position": { "x": 0.1, "y": -0.2, "z": 3.14159265358979 } },
			{ "entity": 2, "position": "not an object", "extra": [1, 2] }
		] },
		"not a timestep",
		{ "no_samples_here": true },
		{ "samples": { "wrong": "type" } },
		{ "samples": [
			{ "entity": 2, "rotation": 150, "speed": -0.0, "mode": "run \u00e9", "position": { "x": 1.5e2, "y": 42, "z": -1.0e-40 } }
		] }
	]
})");
	}

	/** No metadata block, numeric simTimeS only, empty simulation: MaxNumEntities must fall back to
	 *  the entity count in both parsers. */
	const TCHAR* GetNoMetadataJson()
	{
		return TEXT(R"({
	"entities": [ { "id": 7, "simTimeS": 0.25 }, { "id": 8 } ],
	"simulation": []
})");
	}

	FString GetTestDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobiusJsonParityTest"));
	}

	bool CompareParsed(FAutomationTestBase& Test, const TCHAR* Label,
	                   const FMobiusAgentSimulationData& A, const FMobiusAgentSimulationData& B)
	{
		bool bEqual = true;
		auto Fail = [&](const FString& Detail)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), Label, *Detail));
			bEqual = false;
		};

		if (A.SourceFormat != B.SourceFormat) { Fail(TEXT("SourceFormat differs")); }

		const FMobiusAgentSimulationMetadata& MA = A.Metadata;
		const FMobiusAgentSimulationMetadata& MB = B.Metadata;
		if (FloatBits(MA.Duration) != FloatBits(MB.Duration)) { Fail(FString::Printf(TEXT("Duration bits %08x vs %08x"), FloatBits(MA.Duration), FloatBits(MB.Duration))); }
		if (FloatBits(MA.SamplingRate) != FloatBits(MB.SamplingRate)) { Fail(TEXT("SamplingRate differs")); }
		if (MA.MaxNumEntities != MB.MaxNumEntities) { Fail(FString::Printf(TEXT("MaxNumEntities %d vs %d"), MA.MaxNumEntities, MB.MaxNumEntities)); }
		if (MA.bIsSI != MB.bIsSI) { Fail(TEXT("bIsSI differs")); }
		if (MA.bIsDeg != MB.bIsDeg) { Fail(TEXT("bIsDeg differs")); }
		if (MA.bHasRotationData != MB.bHasRotationData) { Fail(TEXT("bHasRotationData differs")); }
		if (MA.bHasSpeedData != MB.bHasSpeedData) { Fail(TEXT("bHasSpeedData differs")); }

		if (A.Entities.Num() != B.Entities.Num())
		{
			Fail(FString::Printf(TEXT("entity count %d vs %d"), A.Entities.Num(), B.Entities.Num()));
		}
		else
		{
			for (int32 i = 0; i < A.Entities.Num(); ++i)
			{
				const FMobiusAgentEntityData& EA = A.Entities[i];
				const FMobiusAgentEntityData& EB = B.Entities[i];
				if (EA.Id != EB.Id) { Fail(FString::Printf(TEXT("entity[%d].Id %d vs %d"), i, EA.Id, EB.Id)); }
				if (!EA.Name.Equals(EB.Name, ESearchCase::CaseSensitive)) { Fail(FString::Printf(TEXT("entity[%d].Name '%s' vs '%s'"), i, *EA.Name, *EB.Name)); }
				if (FloatBits(EA.SimTimeS) != FloatBits(EB.SimTimeS)) { Fail(FString::Printf(TEXT("entity[%d].SimTimeS bits differ"), i)); }
				if (FloatBits(EA.MaxSpeed) != FloatBits(EB.MaxSpeed)) { Fail(FString::Printf(TEXT("entity[%d].MaxSpeed bits differ"), i)); }
				if (!EA.MPlane.Equals(EB.MPlane, ESearchCase::CaseSensitive)) { Fail(FString::Printf(TEXT("entity[%d].MPlane differs"), i)); }
				if (EA.Map != EB.Map) { Fail(FString::Printf(TEXT("entity[%d].Map %d vs %d"), i, EA.Map, EB.Map)); }
			}
		}

		if (A.Samples.Num() != B.Samples.Num())
		{
			Fail(FString::Printf(TEXT("sample count %d vs %d"), A.Samples.Num(), B.Samples.Num()));
		}
		else
		{
			for (int32 i = 0; i < A.Samples.Num(); ++i)
			{
				const FMobiusAgentSampleData& SA = A.Samples[i];
				const FMobiusAgentSampleData& SB = B.Samples[i];
				if (SA.TimestepIndex != SB.TimestepIndex) { Fail(FString::Printf(TEXT("sample[%d].TimestepIndex %d vs %d"), i, SA.TimestepIndex, SB.TimestepIndex)); }
				if (SA.EntityId != SB.EntityId) { Fail(FString::Printf(TEXT("sample[%d].EntityId %d vs %d"), i, SA.EntityId, SB.EntityId)); }
				if (FloatBits(SA.PositionX) != FloatBits(SB.PositionX) ||
				    FloatBits(SA.PositionY) != FloatBits(SB.PositionY) ||
				    FloatBits(SA.PositionZ) != FloatBits(SB.PositionZ))
				{
					Fail(FString::Printf(TEXT("sample[%d] position bits differ"), i));
				}
				if (FloatBits(SA.Rotation) != FloatBits(SB.Rotation)) { Fail(FString::Printf(TEXT("sample[%d].Rotation bits differ"), i)); }
				if (FloatBits(SA.Speed) != FloatBits(SB.Speed)) { Fail(FString::Printf(TEXT("sample[%d].Speed bits %08x vs %08x"), i, FloatBits(SA.Speed), FloatBits(SB.Speed))); }
				if (!SA.Mode.Equals(SB.Mode, ESearchCase::CaseSensitive)) { Fail(FString::Printf(TEXT("sample[%d].Mode '%s' vs '%s'"), i, *SA.Mode, *SB.Mode)); }
			}
		}
		return bEqual;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FJsonParserParityTest,
	"ProjectMobius.SimData.JsonParserParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FJsonParserParityTest::RunTest(const FString& Parameters)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString TestDir = GetTestDir();
	FileManager.MakeDirectory(*TestDir, /*Tree*/ true);

	// Two ImportAgentFile calls below intentionally exercise the simdjson->pull-parser retry, each
	// logging one warning (UTF-16 doc, truncated doc).
	AddExpectedMessage(TEXT("simdjson JSON parse failed"), EAutomationExpectedMessageFlags::Contains, /*Occurrences*/ 2, /*IsRegex*/ false);

	// ---------- Edge-case document, UTF-8 without BOM (the canonical Mobius encoding) ----------
	const FString EdgePath = FPaths::Combine(TestDir, TEXT("EdgeCase.json"));
	TestTrue(TEXT("save edge-case utf8"), FFileHelper::SaveStringToFile(
		GetEdgeCaseJson(), *EdgePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FMobiusAgentSimulationData SimdData;
	FString SimdError;
	TestTrue(FString::Printf(TEXT("simdjson parses edge-case doc (%s)"), *SimdError),
	         FMobiusAgentDataImporter::ParseJsonWithSimdjson(EdgePath, SimdData, &SimdError));

	FMobiusAgentSimulationData PullData;
	FString PullError;
	TestTrue(FString::Printf(TEXT("pull-parser parses edge-case doc (%s)"), *PullError),
	         FMobiusAgentDataImporter::ParseJsonWithPullParser(EdgePath, PullData, &PullError));

	CompareParsed(*this, TEXT("edge-case simdjson vs pull-parser"), SimdData, PullData);

	// ---------- Ground truth (checked on the simdjson result; parity extends it to the pull side) ----------
	TestEqual(TEXT("SourceFormat"), static_cast<int32>(SimdData.SourceFormat), static_cast<int32>(EMobiusAgentFileFormat::Json));
	TestTrue(TEXT("canonical isSI beats earlier is_si"), SimdData.Metadata.bIsSI);
	TestTrue(TEXT("numeric isDeg must not set the bool (strict typing)"), SimdData.Metadata.bIsDeg); // default true
	TestEqual(TEXT("MaxNumEntities from metadata"), SimdData.Metadata.MaxNumEntities, 5000);
	TestTrue(TEXT("Duration bits"), FloatBits(SimdData.Metadata.Duration) == FloatBits(12.3456789f));

	TestEqual(TEXT("entity count (number + null elements skipped)"), SimdData.Entities.Num(), 3);
	if (SimdData.Entities.Num() == 3)
	{
		TestEqual(TEXT("entity[0].Id"), SimdData.Entities[0].Id, 1);
		TestTrue(TEXT("entity[0].Name unescape"), SimdData.Entities[0].Name.Equals(FString(TEXT("walker \"one\" \u00e9\\")), ESearchCase::CaseSensitive));
		TestTrue(TEXT("entity[0].SimTimeS from string"), FloatBits(SimdData.Entities[0].SimTimeS) == FloatBits(12.5f));
		TestEqual(TEXT("entity[1].Id round-half-from-zero 2.6 -> 3"), SimdData.Entities[1].Id, 3);
		TestTrue(TEXT("entity[1].SimTimeS from number"), FloatBits(SimdData.Entities[1].SimTimeS) == FloatBits(3.75f));
		TestEqual(TEXT("entity[1].Map out of int32 range ignored"), SimdData.Entities[1].Map, 0);
		TestTrue(TEXT("entity[2].Name"), SimdData.Entities[2].Name.Equals(FString(TEXT("minimal")), ESearchCase::CaseSensitive));
	}

	TestEqual(TEXT("sample count"), SimdData.Samples.Num(), 3);
	if (SimdData.Samples.Num() == 3)
	{
		TestEqual(TEXT("sample[0].TimestepIndex"), SimdData.Samples[0].TimestepIndex, 0);
		TestEqual(TEXT("sample[1].TimestepIndex"), SimdData.Samples[1].TimestepIndex, 0);
		TestEqual(TEXT("sample[2] lands on index 4 (invalid elements still count)"), SimdData.Samples[2].TimestepIndex, 4);
		TestTrue(TEXT("sample[0].PositionX"), FloatBits(SimdData.Samples[0].PositionX) == FloatBits(0.1f));
		TestTrue(TEXT("sample[1] wrong-typed position leaves zeros"), FloatBits(SimdData.Samples[1].PositionX) == FloatBits(0.0f));
		TestTrue(TEXT("sample[2].Mode"), SimdData.Samples[2].Mode.Equals(FString(TEXT("run \u00e9")), ESearchCase::CaseSensitive));
	}

	// ---------- No-metadata document: MaxNumEntities falls back to the entity count ----------
	const FString NoMetaPath = FPaths::Combine(TestDir, TEXT("NoMetadata.json"));
	FFileHelper::SaveStringToFile(GetNoMetadataJson(), *NoMetaPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	FMobiusAgentSimulationData SimdNoMeta, PullNoMeta;
	TestTrue(TEXT("simdjson parses no-metadata doc"), FMobiusAgentDataImporter::ParseJsonWithSimdjson(NoMetaPath, SimdNoMeta));
	TestTrue(TEXT("pull-parser parses no-metadata doc"), FMobiusAgentDataImporter::ParseJsonWithPullParser(NoMetaPath, PullNoMeta));
	CompareParsed(*this, TEXT("no-metadata simdjson vs pull-parser"), SimdNoMeta, PullNoMeta);
	TestEqual(TEXT("MaxNumEntities falls back to entity count"), SimdNoMeta.Metadata.MaxNumEntities, 2);

	// ---------- UTF-8 BOM: simdjson must skip it and produce the identical result ----------
	const FString BomPath = FPaths::Combine(TestDir, TEXT("EdgeCaseBom.json"));
	FFileHelper::SaveStringToFile(GetEdgeCaseJson(), *BomPath, FFileHelper::EEncodingOptions::ForceUTF8);

	FMobiusAgentSimulationData BomData;
	FString BomError;
	TestTrue(FString::Printf(TEXT("simdjson parses UTF-8 BOM doc (%s)"), *BomError),
	         FMobiusAgentDataImporter::ParseJsonWithSimdjson(BomPath, BomData, &BomError));
	CompareParsed(*this, TEXT("UTF-8 BOM vs no-BOM (simdjson)"), BomData, SimdData);

	// ---------- UTF-16: simdjson must refuse; ImportAgentFile must land on the pull-parser ----------
	const FString Utf16Path = FPaths::Combine(TestDir, TEXT("EdgeCaseUtf16.json"));
	FFileHelper::SaveStringToFile(GetEdgeCaseJson(), *Utf16Path, FFileHelper::EEncodingOptions::ForceUnicode);

	FMobiusAgentSimulationData Utf16Direct;
	TestFalse(TEXT("simdjson refuses UTF-16"), FMobiusAgentDataImporter::ParseJsonWithSimdjson(Utf16Path, Utf16Direct));

	AddExpectedMessage(TEXT("simdjson JSON parse failed"), EAutomationExpectedMessageFlags::Contains);
	FMobiusAgentSimulationData Utf16Imported;
	FString Utf16Error;
	TestTrue(FString::Printf(TEXT("ImportAgentFile falls back to pull-parser on UTF-16 (%s)"), *Utf16Error),
	         FMobiusAgentDataImporter::ImportAgentFile(Utf16Path, Utf16Imported, &Utf16Error));
	CompareParsed(*this, TEXT("UTF-16 fallback vs UTF-8 result"), Utf16Imported, SimdData);

	// ---------- Truncated document: both engines and the dispatcher must fail cleanly ----------
	const FString EdgeJson = GetEdgeCaseJson();
	const FString TruncatedPath = FPaths::Combine(TestDir, TEXT("Truncated.json"));
	FFileHelper::SaveStringToFile(EdgeJson.Left(EdgeJson.Len() / 2), *TruncatedPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	FMobiusAgentSimulationData TruncatedData;
	TestFalse(TEXT("simdjson rejects truncated doc"), FMobiusAgentDataImporter::ParseJsonWithSimdjson(TruncatedPath, TruncatedData));
	TestEqual(TEXT("simdjson failure leaves no partial samples"), TruncatedData.Samples.Num(), 0);
	TestFalse(TEXT("pull-parser rejects truncated doc"), FMobiusAgentDataImporter::ParseJsonWithPullParser(TruncatedPath, TruncatedData));
	AddExpectedMessage(TEXT("simdjson JSON parse failed"), EAutomationExpectedMessageFlags::Contains);
	TestFalse(TEXT("ImportAgentFile rejects truncated doc"), FMobiusAgentDataImporter::ImportAgentFile(TruncatedPath, TruncatedData));

	FileManager.DeleteDirectory(*TestDir, /*RequireExists*/ false, /*Tree*/ true);
	return true;
}

#endif // !UE_BUILD_SHIPPING
