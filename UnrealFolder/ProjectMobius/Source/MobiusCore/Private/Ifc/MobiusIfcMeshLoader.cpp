// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#include "Ifc/MobiusIfcMeshLoader.h"

#include "AsyncAssimpMeshLoader.h" // FAssimpSubmeshBuffers -- the shape the whole mesh path speaks
#include "Ifc/MobiusIfcRenderableClasses.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

#if MOBIUS_WITH_IFC_BRIDGE
	#include "MobiusIfcBridge.h" // the extern "C" ABI. Private to this translation unit on purpose.
#endif

namespace
{
	/**
	 * Filename of our shim shared library, staged beside the binary by MobiusIfcLibrary.Build.cs.
	 * Platform-specific: Windows produces MobiusIfcBridge.dll; macOS produces libMobiusIfcBridge.dylib
	 * (CMake's default lib prefix + .dylib). Only referenced where MOBIUS_WITH_IFC_BRIDGE is set.
	 */
#if PLATFORM_MAC
	const TCHAR* const GMobiusIfcBridgeDllName = TEXT("libMobiusIfcBridge.dylib");
#else
	const TCHAR* const GMobiusIfcBridgeDllName = TEXT("MobiusIfcBridge.dll");
#endif

	/**
	 * How much of the file head to scan for FILE_SCHEMA. The STEP header (ISO-10303-21 HEADER
	 * section) is the first thing in the file and is a few hundred bytes in every export we have;
	 * 64 KB is generous enough to survive a verbose FILE_DESCRIPTION/FILE_NAME block without ever
	 * reading a 400 KB (or 400 MB) model just to learn its schema.
	 */
	constexpr int64 GIfcHeaderScanBytes = 64 * 1024;

#if MOBIUS_WITH_IFC_BRIDGE
	/**
	 * Frees the scene on every exit path, including the early returns. MobiusIfc_Free is documented
	 * NULL-safe and never throws, so this needs no null guard of its own.
	 */
	struct FScopedIfcScene
	{
		MobiusIfcScene* Scene = nullptr;
		~FScopedIfcScene() { MobiusIfc_Free(Scene); }
	};
#endif
}

bool FMobiusIfcMeshLoader::EnsureBridgeLoaded(FString& OutError)
{
	OutError.Reset();

#if !MOBIUS_WITH_IFC_BRIDGE
	OutError = TEXT("IFC import is Win64-only in this build (MobiusIfcBridge.dll is not built for this platform).");
	return false;
#else
	// One attempt per process, cached both ways. A second failing attempt would produce the same
	// error and pay another filesystem walk for it.
	static FCriticalSection HandleLock;
	static void* Handle = nullptr;
	static bool bAttempted = false;
	static FString CachedError;

	FScopeLock Lock(&HandleLock);

	if (bAttempted)
	{
		OutError = CachedError;
		return Handle != nullptr;
	}

	bAttempted = true;

	// Candidate 1: next to the running executable. This is where MobiusIfcLibrary.Build.cs's
	// RuntimeDependencies stage it -- $(BinaryOutputDir) and $(TargetOutputDir) for a packaged
	// target, $(EngineDir)/Binaries/Win64 for an editor build (which is also BaseDir there).
	// Candidate 2: the bare name, letting the OS search order find it (covers a layout we did not
	// anticipate, e.g. a DLL placed alongside a different loaded module).
	const FString BesideExe = FPaths::Combine(FString(FPlatformProcess::BaseDir()), GMobiusIfcBridgeDllName);

	if (FPaths::FileExists(BesideExe))
	{
		Handle = FPlatformProcess::GetDllHandle(*BesideExe);
	}

	if (Handle == nullptr)
	{
		Handle = FPlatformProcess::GetDllHandle(GMobiusIfcBridgeDllName);
	}

	if (Handle == nullptr)
	{
		CachedError = FString::Printf(
			TEXT("Could not load %s. Looked beside the executable (%s) and on the default DLL search path. ")
			TEXT("If this is a fresh checkout, run Source\\ThirdParty\\IfcBridgeSource\\Build-MobiusIfcBridge.ps1; ")
			TEXT("the DLL is gitignored and is staged by the build, not committed."),
			GMobiusIfcBridgeDllName, *BesideExe);
		OutError = CachedError;
		return false;
	}

	// Version guard BEFORE any real call. A stale DLL next to a newer header is a struct-layout
	// mismatch that would otherwise surface as a crash several calls later -- this project has
	// shipped a stale DLL and gotten a false-green result out of it before.
	const uint32 DllAbi = MobiusIfc_GetAbiVersion();
	if (DllAbi != MOBIUSIFC_ABI_VERSION)
	{
		CachedError = FString::Printf(
			TEXT("%s ABI mismatch: DLL reports version %u, this build was compiled against %u. ")
			TEXT("Rebuild the DLL with Source\\ThirdParty\\IfcBridgeSource\\Build-MobiusIfcBridge.ps1."),
			GMobiusIfcBridgeDllName, DllAbi, static_cast<uint32>(MOBIUSIFC_ABI_VERSION));

		// Drop the handle: refusing to use a mismatched DLL is the point, and holding it open would
		// let the delay-load thunk resolve symbols from it anyway on a later call.
		FPlatformProcess::FreeDllHandle(Handle);
		Handle = nullptr;
		OutError = CachedError;
		return false;
	}

	return true;
#endif
}

bool FMobiusIfcMeshLoader::ReadFileSchema(const FString& PathToIfc, FString& OutSchema, FString& OutError)
{
	OutSchema.Reset();
	OutError.Reset();

	TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*PathToIfc));
	if (!File)
	{
		OutError = FString::Printf(TEXT("Could not open '%s' for reading."), *PathToIfc);
		return false;
	}

	const int64 BytesToRead = FMath::Min(File->Size(), GIfcHeaderScanBytes);
	if (BytesToRead <= 0)
	{
		OutError = FString::Printf(TEXT("'%s' is empty."), *PathToIfc);
		return false;
	}

	TArray<ANSICHAR> Head;
	Head.SetNumUninitialized(static_cast<int32>(BytesToRead) + 1);
	if (!File->Read(reinterpret_cast<uint8*>(Head.GetData()), BytesToRead))
	{
		OutError = FString::Printf(TEXT("Failed reading the first %lld bytes of '%s'."), BytesToRead, *PathToIfc);
		return false;
	}
	Head[static_cast<int32>(BytesToRead)] = '\0';

	// A STEP physical file's header is 7-bit ASCII by specification, so a byte-wise scan is correct
	// here and avoids decoding the whole head as text just to find one token.
	const FString HeadText(UTF8_TO_TCHAR(Head.GetData()));

	int32 SchemaTokenIndex = HeadText.Find(TEXT("FILE_SCHEMA"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 0);
	if (SchemaTokenIndex == INDEX_NONE)
	{
		OutError = FString::Printf(
			TEXT("'%s' has no FILE_SCHEMA entry in its first %lld bytes -- this is not a STEP/IFC file."),
			*PathToIfc, BytesToRead);
		return false;
	}

	// The schema name is the first single-quoted token after FILE_SCHEMA, e.g.
	//   FILE_SCHEMA(('IFC4X3_ADD2'));
	// Whitespace and newlines between the token and the quote are legal and are simply skipped by
	// searching forward for the quote characters.
	const int32 OpenQuote = HeadText.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SchemaTokenIndex);
	const int32 CloseQuote = (OpenQuote == INDEX_NONE)
		                         ? INDEX_NONE
		                         : HeadText.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenQuote + 1);

	if (OpenQuote == INDEX_NONE || CloseQuote == INDEX_NONE || CloseQuote <= OpenQuote + 1)
	{
		OutError = FString::Printf(TEXT("'%s' has a malformed FILE_SCHEMA entry (no quoted schema name)."), *PathToIfc);
		return false;
	}

	OutSchema = HeadText.Mid(OpenQuote + 1, CloseQuote - OpenQuote - 1).TrimStartAndEnd();

	if (!OutSchema.StartsWith(TEXT("IFC"), ESearchCase::IgnoreCase))
	{
		OutError = FString::Printf(
			TEXT("'%s' declares schema '%s', which is not an IFC schema."), *PathToIfc, *OutSchema);
		return false;
	}

	return true;
}

bool FMobiusIfcMeshLoader::LoadIfcFile(const FString& PathToIfc, TArray<FAssimpSubmeshBuffers>& OutSubmeshes,
                                       FMobiusIfcLoadStats& OutStats, FString& OutError)
{
	OutSubmeshes.Reset();
	OutStats = FMobiusIfcLoadStats();
	OutError.Reset();

#if !MOBIUS_WITH_IFC_BRIDGE
	OutError = TEXT("IFC import is Win64-only in this build (MobiusIfcBridge.dll is not built for this platform).");
	return false;
#else
	if (PathToIfc.IsEmpty())
	{
		OutError = TEXT("No IFC path supplied.");
		return false;
	}

	if (!FPaths::FileExists(PathToIfc))
	{
		OutError = FString::Printf(TEXT("IFC file does not exist: %s"), *PathToIfc);
		return false;
	}

	// Schema check first: it is cheap, it is the only trustworthy source of the source schema (the
	// library's own accessor lies -- handoff 8.2), and it rejects a non-IFC file before the parser
	// is handed it at all.
	if (!ReadFileSchema(PathToIfc, OutStats.SourceSchema, OutError))
	{
		return false;
	}

	if (!EnsureBridgeLoaded(OutError))
	{
		return false;
	}

	// The bridge documents its path parameter as UTF-8 and handles non-ASCII itself (it probes with
	// MultiByteToWideChar + GetFileAttributesW and hands IFC++ an 8.3 short path when it has to,
	// because MSVC's std::ifstream(const char*) would otherwise decode the path with the process
	// ANSI codepage). So a straight UTF-8 conversion is correct here -- do not "helpfully" convert
	// to ANSI.
	ANSICHAR ErrBuf[512];
	ErrBuf[0] = '\0';

	FScopedIfcScene Scoped;
	const int32 LoadResult = MobiusIfc_Load(TCHAR_TO_UTF8(*PathToIfc), &Scoped.Scene, ErrBuf, UE_ARRAY_COUNT(ErrBuf));
	if (LoadResult != MOBIUSIFC_OK)
	{
		OutError = FString::Printf(TEXT("IFC load failed (%d: %s): %s"),
		                           LoadResult,
		                           UTF8_TO_TCHAR(MobiusIfc_ErrorString(LoadResult)),
		                           UTF8_TO_TCHAR(ErrBuf));
		return false;
	}

	const MobiusIfcProduct* Products = nullptr;
	int32 ProductCount = 0;
	const int32 GetResult = MobiusIfc_GetProducts(Scoped.Scene, &Products, &ProductCount);
	if (GetResult != MOBIUSIFC_OK || (ProductCount > 0 && Products == nullptr))
	{
		OutError = FString::Printf(TEXT("IFC product enumeration failed (%d: %s)."),
		                           GetResult, UTF8_TO_TCHAR(MobiusIfc_ErrorString(GetResult)));
		return false;
	}

	OutStats.ProductsWithGeometry = ProductCount;
	OutStats.ProductsWithoutGeometry = MobiusIfc_GetProductsWithoutGeometryCount(Scoped.Scene);

	OutSubmeshes.Reserve(ProductCount);

	MobiusIfc::FMobiusIfcClassStats ClassStats;

	for (int32 ProductIndex = 0; ProductIndex < ProductCount; ++ProductIndex)
	{
		const MobiusIfcProduct& Product = Products[ProductIndex];

		const int32 VertCount = Product.vertCount;
		const int32 IndexCount = Product.indexCount;
		OutStats.TotalTriangles += Product.triCount;

		// ifcClass/guid are documented never-NULL and UTF-8. The conversion uses a stack buffer for
		// strings this short, so classification stays allocation-free on this path (see the
		// allowlist header's data-structure note).
		const FUTF8ToTCHAR ClassNameConv(static_cast<const ANSICHAR*>(Product.ifcClass));
		const FStringView ClassName(ClassNameConv.Get(), ClassNameConv.Length());

		const MobiusIfc::ERenderVerdict Verdict = MobiusIfc::ClassifyClass(ClassName);
		ClassStats.Record(Verdict, ClassName);

		// IfcSpace: never rendered, always captured. Deliberately keyed off IsRoomVolumeClass rather
		// than the VolumeOnly verdict so a future edit to the VolumeOnly array cannot silently close
		// the B-RISK room door (see the allowlist header).
		if (MobiusIfc::IsRoomVolumeClass(ClassName))
		{
			FMobiusIfcRoomVolume& Room = OutStats.RoomVolumes.AddDefaulted_GetRef();
			Room.Guid = UTF8_TO_TCHAR(Product.guid);
			Room.TriangleCount = Product.triCount;
			Room.Bounds = FBox(
				FVector(Product.aabb.minX, Product.aabb.minY, Product.aabb.minZ),
				FVector(Product.aabb.maxX, Product.aabb.maxY, Product.aabb.maxZ));
		}

		// Semantic materials are captured for EVERY product with geometry, renderable or not: an
		// IfcSpace's material is as interesting to the B-RISK side as a wall's, and an IfcOpeningElement
		// that names a material is worth recording even though nothing draws it.
		if (Product.layerCount > 0 || (Product.materialName && Product.materialName[0] != '\0'))
		{
			FMobiusIfcProductMaterial& ProductMaterial = OutStats.ProductMaterials.AddDefaulted_GetRef();
			ProductMaterial.Guid = UTF8_TO_TCHAR(Product.guid);
			ProductMaterial.IfcClass = FString(ClassName);
			ProductMaterial.MaterialName = UTF8_TO_TCHAR(Product.materialName);
			ProductMaterial.Layers.Reserve(Product.layerCount);
			for (int32 LayerIndex = 0; LayerIndex < Product.layerCount; ++LayerIndex)
			{
				const MobiusIfcMaterialLayer& SourceLayer = Product.layers[LayerIndex];
				FMobiusIfcMaterialLayer& Layer = ProductMaterial.Layers.AddDefaulted_GetRef();
				Layer.Name = UTF8_TO_TCHAR(SourceLayer.name);
				Layer.ThicknessCm = SourceLayer.thicknessCm;
			}
		}

		const bool bRender = (Verdict == MobiusIfc::ERenderVerdict::Render)
			|| (Verdict == MobiusIfc::ERenderVerdict::Annotation && MobiusIfc::bMobiusIfcRenderAnnotationClasses);

		if (!bRender || Product.sectionCount <= 0 || Product.bRenderable == 0)
		{
			continue;
		}

		// One submesh per SECTION, not per product: the DLL splits a product by distinct appearance, so
		// a window's frame and its glazing arrive as separate draw ranges with their own colours.
		bool bAnySectionEmitted = false;
		for (int32 SectionIndex = 0; SectionIndex < Product.sectionCount; ++SectionIndex)
		{
			const MobiusIfcSection& Section = Product.sections[SectionIndex];
			const int32 SectionVerts = Section.vertCount;
			const int32 SectionIndices = Section.indexCount;

			if (SectionVerts <= 0 || SectionIndices <= 0 || !Section.vertices || !Section.normals || !Section.indices)
			{
				continue;
			}

			// Defensive index validation. The DLL's own pure-C test measured zero out-of-range indices on
			// both test files, but an out-of-range index reaches CreateMeshSection_LinearColor and then
			// the render thread, where it is a crash rather than a bad triangle. One pass over ints is
			// nothing next to the parse, so validate rather than trust.
			bool bIndicesValid = true;
			for (int32 i = 0; i < SectionIndices; ++i)
			{
				const int32 Index = Section.indices[i];
				if (Index < 0 || Index >= SectionVerts)
				{
					bIndicesValid = false;
					break;
				}
			}

			if (!bIndicesValid)
			{
				++OutStats.MalformedProducts;
				continue;
			}

			FAssimpSubmeshBuffers& Sub = OutSubmeshes.AddDefaulted_GetRef();
			Sub.SourceGuid = UTF8_TO_TCHAR(Product.guid);
			Sub.SourceIfcClass = FString(ClassName);
			Sub.SourceMaterialName = UTF8_TO_TCHAR(Product.materialName);

			// Appearance straight from the source style. Alpha carries opacity so a single FLinearColor
			// covers both channels the renderer needs; bHasMaterial keeps "unstyled" distinguishable
			// from "styled black", which matters because plenty of real IFC files style only some products.
			Sub.Material.bHasMaterial = (Section.appearance.bHasAppearance != 0);
			if (Sub.Material.bHasMaterial)
			{
				Sub.Material.Name = Sub.SourceMaterialName;
				Sub.Material.BaseColour = FLinearColor(
					Section.appearance.diffuseR, Section.appearance.diffuseG, Section.appearance.diffuseB,
					Section.appearance.opacity);
				Sub.Material.SpecularExponent = Section.appearance.specularExponent;
			}

			Sub.Vertices.SetNumUninitialized(SectionVerts);
			Sub.Normals.SetNumUninitialized(SectionVerts);
			for (int32 i = 0; i < SectionVerts; ++i)
			{
				const int32 Base = i * 3;
				// Already UE space: left-handed, Z-up, centimetres, source-order winding. No scale, no
				// mirror, no index flip here -- see this file's header comment.
				Sub.Vertices[i] = FVector(Section.vertices[Base], Section.vertices[Base + 1], Section.vertices[Base + 2]);
				Sub.Normals[i] = FVector(Section.normals[Base], Section.normals[Base + 1], Section.normals[Base + 2]);
			}

			Sub.Faces.SetNumUninitialized(SectionIndices);
			static_assert(sizeof(int32) == sizeof(int32_t), "Face index copy assumes int32 and int32_t are the same width");
			FMemory::Memcpy(Sub.Faces.GetData(), Section.indices, static_cast<SIZE_T>(SectionIndices) * sizeof(int32));

			// UV is left EMPTY on purpose, matching the Assimp path exactly: FillDataFromScene never
			// populates FAssimpSubmeshBuffers::UV either, SplitSubmeshByTriCap treats a size mismatch as
			// "no UVs", and CreateMeshSection_LinearColor accepts an empty array. Inventing a projection
			// here would make IFC geometry texture differently from every other supported format with no
			// specification for what it should look like. Tangents are not part of this struct at all --
			// the emit pump passes an empty tangent array for every format.

			OutStats.RenderedTriangles += Section.triCount;
			++OutStats.RenderedSections;
			bAnySectionEmitted = true;
		}

		if (bAnySectionEmitted)
		{
			++OutStats.RenderedProducts;
		}
	}

	OutStats.FilterSummary = ClassStats.Summarize();

	if (OutStats.RenderedProducts == 0)
	{
		OutError = FString::Printf(
			TEXT("IFC file '%s' (schema %s) parsed with %d products carrying geometry, but none of them ")
			TEXT("are renderable classes. Filter: %s"),
			*PathToIfc, *OutStats.SourceSchema, OutStats.ProductsWithGeometry, *OutStats.FilterSummary);
		return false;
	}

	return true;
#endif // MOBIUS_WITH_IFC_BRIDGE
}
