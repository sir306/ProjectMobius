# MobiusIfcBridge -- design notes, deviations, disagreements, unverified list

Written 2026-08-11 authoring the four files in this directory (`MobiusIfcBridge.h`,
`MobiusIfcBridge.cpp`, `CMakeLists.txt`, this file). **I did not compile, link, or run any of it
myself** -- no cmake, no build, per this task's own rules. Every claim below is either "read from
the real IFC++ header" (cited by file) or "reasoned from that", never "assumed to work like the
sketch said."

**Addendum, same session, ~2 minutes after finishing the four files above:** `install/bin/
MobiusIfcBridge.dll` (6,364,672 bytes, verified PE32+ x86-64 DLL), `install/lib/MobiusIfcBridge.lib`,
and `install/include/MobiusIfcBridge.h` (byte-identical to the header in this directory) now exist.
I did not put them there -- this must be a concurrent workstream (per HANDOFF_IFC_2026-08-11.md
§12.1, the "vendor IFC++ and build it" step runs serially alongside the A-D fan-out this shim is
part of) that picked up these exact files and successfully built them. That is real evidence the
header and .cpp at least compile and link against a real IfcPlusPlus.lib -- it is NOT evidence the
coordinate conversion, winding, or normals are correct, or that loading an actual .ifc file through
this DLL produces sane output. Nobody has run `MobiusIfc_Load` against a real file yet as far as I
know. Treat "it links" and "it's correct" as two separate, still-mostly-open claims.

## API surface settled on

```c
typedef enum MobiusIfcResult { MOBIUSIFC_OK = 0, ... MOBIUSIFC_ERR_UNKNOWN = 7 } MobiusIfcResult;
const char*    MobiusIfc_ErrorString(int32_t resultCode);
uint32_t       MobiusIfc_GetAbiVersion(void);

typedef struct MobiusIfcScene MobiusIfcScene;
int32_t  MobiusIfc_Load(const char* utf8Path, MobiusIfcScene** outScene, char* errBuf, int32_t errBufLen);
void     MobiusIfc_Free(MobiusIfcScene* scene);

typedef struct MobiusIfcAabb { float minX,minY,minZ,maxX,maxY,maxZ; } MobiusIfcAabb;   // 24 bytes
typedef struct MobiusIfcProduct {
    const float* vertices; const float* normals; const int32_t* indices;
    const char* guid; const char* ifcClass;
    int32_t vertCount, indexCount, triCount, bRenderable;
    MobiusIfcAabb aabb;
} MobiusIfcProduct;   // 80 bytes, static_assert'd in the .cpp

int32_t MobiusIfc_GetProductCount(const MobiusIfcScene*);
int32_t MobiusIfc_GetProducts(const MobiusIfcScene*, const MobiusIfcProduct** outProducts, int32_t* outCount);
int32_t MobiusIfc_GetProductsWithoutGeometryCount(const MobiusIfcScene*);
```

## Deviations from the §7.2 sketch, with reasons

1. **`int` → `int32_t` everywhere.** The sketch used plain `int`. The constraints doc says "never
   `bool` (use int32_t)" for the same reason `int`'s width isn't ABI-guaranteed across compilers
   the way `int32_t` is. Applied the same logic to every integer crossing the boundary, not just
   the ones the constraint literally named.

2. **Per-index `MobiusIfc_GetProduct(scene, index, ...)` → single `MobiusIfc_GetProducts(scene,
   &arr, &count)` returning one POD array.** The task asked me to pick and justify. One call is
   cache-friendlier for UE's mesh-section build loop over what can be hundreds of products (the
   IFC4X3 test file alone has 205), and there's no reason to pay N FFI-call overhead when the
   whole array can be handed over as one pointer+length. Kept `MobiusIfc_GetProductCount` too,
   since it's a trivial wrapper and some callers just want a count for pre-sizing a `TArray`
   without touching the product data yet.

3. **Added `MobiusIfcResult` enum + `MobiusIfc_ErrorString`, `MobiusIfc_GetAbiVersion` +
   `MOBIUSIFC_ABI_VERSION`, per-product `bRenderable` + `ifcClass`, per-product `MobiusIfcAabb` +
   `triCount`.** All of these were explicitly requested as "must additionally provide" in the
   task, not really deviations from §7.2 so much as the sketch being deliberately incomplete.
   Listed here only for completeness against "refine it if you find something better."

4. **Did NOT add a DLL-side "dropped by allowlist" counter.** The task allowed one
   (`MobiusIfc_GetDroppedCount`-style) but was explicit that the DLL must not encode allowlist
   policy. Since there's no allowlist logic inside this DLL at all (every product with geometry
   is returned, `IfcSpace` and `IfcOpeningElement` included), there is nothing for the DLL to have
   "dropped" in that sense — that count belongs entirely to the UE-side caller, keyed off
   `ifcClass`. What I did add is `MobiusIfc_GetProductsWithoutGeometryCount`, which is a different,
   orthogonal thing: entities IFC++ itself gave zero triangles (spatial containers, `*Type`/
   `*Style` definitions — see handoff 5.1's own "NO geometry" list). That's a parity check against
   the harness's own "products WITH/WITHOUT geometry" split, not a rendering decision.

5. **`bRenderable` redefined as a purely geometric flag ("triCount > 0"), not a semantic
   allowlist result.** The task's own wording ("per-product `bRenderable` ... so the caller's
   allowlist can filter" but also "DLL must NOT hardcode the allowlist policy") is only
   consistent if `bRenderable` means "has geometry to draw at all" rather than "IFC++ says this
   class should be drawn" — the latter would be exactly the policy the task says the DLL must not
   own. Documented this distinction explicitly and repeatedly in the header, since it's the one
   field in the whole API most likely to be misread as "the DLL already filtered this for you."
   It has NOT: `IfcSpace` and `IfcOpeningElement` both come back with `bRenderable == 1`.

6. **Normals: DLL emits them, computed from the post-conversion, post-winding-reversal
   triangle**, not carried through a separate IFC-space-normal conversion. Justified in the header
   and `.cpp` comments: computing the normal from the SAME triangle that was already
   converted+rewound means there is no second parallel computation that could disagree with the
   winding — the normal is a pure function of that triangle's final emitted corners. The
   consequence is flat (faceted) shading with vertices NOT welded across faces at all — every
   triangle owns three unique vertex slots. IFC++ exposes no smoothing-group information to weld
   correctly by, and BIM geometry (walls, slabs, extrusions) is overwhelmingly hard-edged, so
   faceted-by-default is the safe choice; a shared/welded scheme done wrong produces visibly wrong
   shading at every corner, which is worse than a larger vertex buffer. Not a scope item to fix
   later, but worth knowing this is where the vertex-count cost lives if size ever matters.

7. **`errBuf`/`errBufLen` semantics tightened**: cleared unconditionally at entry, written to only
   on a non-OK return, silently ignored if null/non-positive. The sketch didn't specify this;
   made it explicit since it's exactly the kind of thing that gets argued about differently by
   whoever writes the caller later.

## Real-header disagreements found (vs. the handoff prose and vs. assumptions)

1. **`carve::exception` does NOT derive from `std::exception`.** Confirmed by reading
   `IfcPlusPlus/src/external/Carve/src/include/carve/carve.hpp`: `struct exception` there is a
   free-standing type with `str()`/`operator<<`, no base class at all. `GeometryConverter.h`'s own
   internal `convertGeometry()` already catches it as a separate case
   (`catch (BuildingException&) ... catch (carve::exception&) ... catch (std::exception&) ...`),
   which is what tipped me off to check. Every catch block in `MobiusIfcBridge.cpp` has an
   explicit `catch (const carve::exception& e)` ahead of `catch (const std::exception& e)` because
   of this — a naive "catch std::exception, then catch(...)" would still be memory-safe (constraint
   2's actual requirement) but would silently lose the message on every carve-thrown error and
   fall through to the generic "unknown exception" text.

2. **No class literally named `GeometryException` exists.** The task's own prompt (and, by
   inference, the handoff) refers to `GeometryException` as one of IFC++'s three throwing types.
   `geometry/GeometryException.h` exists but defines `class UnhandledRepresentationException :
   public std::exception` — there is no `GeometryException` type anywhere in the tree (grepped
   for `class.*Exception` across the whole `ifcpp/` source, excluding the generated `IFC4X3/
   include` entity headers: only `BuildingException`, `UnknownEntityException`, and
   `UnhandledRepresentationException` exist). Doesn't change anything functionally here — a
   `catch (const std::exception&)` still catches `UnhandledRepresentationException` since it does
   derive from `std::exception` — but the name in the constraints doc doesn't correspond to a real
   type, worth knowing if a future session goes looking for it.

3. **IfcPlusPlus's own include directories are `PRIVATE`, not `PUBLIC`/`INTERFACE`.**
   `IfcPlusPlus/CMakeLists.txt` line 84's `TARGET_INCLUDE_DIRECTORIES(IfcPlusPlus PRIVATE ...)`
   means linking the `IfcPlusPlus` CMake target does NOT transitively hand a consumer any of its
   include paths — confirmed not just by reading that file but by the fact that
   `examples/LoadFileExample/CMakeLists.txt` (a target that DOES successfully build against
   `IfcPlusPlus`, per the handoff's proven harness) re-declares the *identical* include directory
   list itself rather than relying on the target. `CMakeLists.txt` in this directory copies that
   same list verbatim in both its subdirectory and standalone modes. This is a real, load-bearing
   constraint on anyone who tries to link against `IfcPlusPlus` the "normal" CMake way and gets
   `ifcpp/model/BuildingModel.h: No such file or directory` despite `target_link_libraries` having
   apparently succeeded.

4. **`IfcPlusPlus`'s own `install()` step cannot supply what a consumer of `carve::mesh::*` types
   needs.** `install(DIRECTORY src/ifcpp DESTINATION include FILES_MATCHING PATTERN "*.h")` only
   captures `ifcpp/**/*.h` — it does not install `src/external/Carve` or `src/external/glm` at
   all, and Carve's own headers are `.hpp` (the glob pattern is `*.h`, so even a corrected install
   path would miss them). Also, `IFCPPConfig.cmake` generation is commented out in the root
   `CMakeLists.txt`, so there's no `find_package(IFCPP)` story either — only a bare
   `IfcPlusPlus-targets.cmake` with no wrapper `Config.cmake`, which CMake's `find_package()`
   requires. Consequence for `CMakeLists.txt` in this directory: "standalone" mode points at the
   IfcPlusPlus **source tree** (not its install output) for headers, using the identical private
   include list from finding #3, and only uses a caller-supplied lib directory to locate the
   already-built `.lib` file. This is documented at length in this directory's `CMakeLists.txt`
   itself, since it's the single most likely thing to surprise whoever tries to wire up standalone
   mode.

5. **Consumers must define `IFCQUERY_STATIC_LIB` themselves, or link fails/misbehaves.**
   `ifcpp/model/GlobalDefines.h`'s `IFCQUERY_EXPORT` macro is `__declspec(dllexport)` if
   `IFCQUERY_LIB` is defined (building the library itself), empty if `IFCQUERY_STATIC_LIB` is
   defined, else `__declspec(dllimport)`. `IfcPlusPlus/CMakeLists.txt` builds
   `add_library(IfcPlusPlus STATIC ...)` and defines `IFCQUERY_STATIC_LIB` for itself via a
   directory-scoped `ADD_DEFINITIONS` — which does NOT propagate to a sibling target the way a
   target-scoped `target_compile_definitions(... PUBLIC ...)` would. Confirmed this matters, not
   just in theory, because `examples/LoadFileExample/CMakeLists.txt` — again, a target that
   provably links successfully — independently defines the exact same macro for itself. This
   directory's `CMakeLists.txt` does the same for the `MobiusIfcBridge` target.

6. **`StatusCallback::messageCallback()` writes to `std::wcout` under `#ifdef _DEBUG` whenever no
   message callback is installed** (`ifcpp/model/StatusCallback.h`). Constraint 5 ("no iostream
   printing in any path") is unconditional, not scoped to Release builds, so `MobiusIfc_Load`
   installs a no-op lambda callback on both the `BuildingModel` and the `ReaderSTEP` (and later the
   `GeometryConverter`) before calling anything that could emit a message, regardless of whether
   the DLL is ultimately built Debug or Release. This is a real trap a plain "just call
   `loadModelFromFile`" implementation would hit under a Debug config.

7. **`GeometryConverter`'s constructor signature and `getShapeInputData()` return type match the
   harness exactly, not the repo's own broken `examples/LoadFileExample/src/main.cpp`** — this was
   already known from the handoff (§4's "known repo defect"), re-confirmed directly against
   `geometry/GeometryConverter.h`: two-arg constructor taking `shared_ptr<BuildingModel>&` +
   `shared_ptr<GeometrySettings>&`, `getShapeInputData()` returns
   `std::unordered_map<std::string, shared_ptr<ProductShapeData>>&`. No further disagreement found
   here beyond what the handoff already flagged.

## Things read directly from source before use (not guessed)

`BuildingModel.h`, `BuildingException.h`, `StatusCallback.h`, `UnknownEntityException.h`,
`reader/ReaderSTEP.h`, `geometry/GeometryConverter.h`, `geometry/GeometrySettings.h`,
`geometry/GeometryException.h`, `geometry/GeometryInputData.h`, `model/BasicTypes.h`,
`geometry/IncludeCarveHeaders.h`, `IFC4X3/include/IfcRoot.h`, `IFC4X3/EntityFactory.h`,
`IFC4X3/include/IfcGloballyUniqueId.h`, `model/GlobalDefines.h`, the top-level and
`IfcPlusPlus/CMakeLists.txt`, `examples/LoadFileExample/CMakeLists.txt`, and
`external/Carve/src/include/carve/{carve.hpp,mesh.hpp,face_decl.hpp,edge_decl.hpp}` for the exact
`Face<3>`/`Edge<3>`/`Vertex<3>`/`Mesh<3>`/`MeshSet<3>` member names (`n_edges`, `edge`, `vert`,
`next`, `v`, `faces`, `meshes`, `vertex_storage`, `open_edges`) used in `EmitFace`/`EmitItem`. All
of these matched what `ifcvalidate_main.cpp` already uses, which is one more independent
confirmation that harness's call sequence is correct, not just "it compiled once."

## UNVERIFIED — be blunt about this

- **Nothing in this directory has been compiled.** No syntax error, no missing-include, no
  template-instantiation failure, no linker error has been ruled out. The .cpp is long enough
  (recursive lambdas capturing `carve::mesh::MeshSet<3>` shared_ptrs, nested try/catch across two
  stages) that I'd expect at least one build-fix pass before it links clean.
- **`static_assert(sizeof(MobiusIfcProduct) == 80)` is a size prediction, not a measurement.**
  Reasoned from x64 MSVC's standard alignment rules (8-byte pointers, no padding needed between
  members whose sizes already divide evenly into the running offset) but never checked with an
  actual compiler. If it's wrong, the build fails loudly on that line rather than silently — which
  is the point of having it — but "loudly" still means the first build attempt needs a look.
- **The `IfcToUe` formula and the fan-triangulation winding swap have not been re-run through the
  handoff's own asymmetry proof (5.6 step 7)** as part of authoring this file. The formula is
  copied exactly from the task's own constraint 3 and the handoff's proven procedure, and the
  winding-reversal is applied at the one place all triangles are assembled (not as a separate pass
  that could be skipped or duplicated), but "matches the written proof procedure" and "passes that
  proof when actually run" are different claims — only the first one is true right now.
  **This is the single most important open item before this shim is trusted for anything visual.**
- **Recursion depth in `EmitItem` (via `ItemShapeData::m_child_items`) is unbounded.** Fine for
  every file this project has tested (handoff Level 0-4), unverified against a pathological
  assembly with deep nesting (handoff Level 5, "scale and robustness", is explicitly still TODO
  upstream of this file too).
- **IFC++ thread-safety is exactly as unverified here as the handoff already says it is
  (7.4)** — this shim adds nothing new on that front and doesn't attempt to; `MobiusIfc_Load`'s
  header doc says outright not to call it concurrently.
- **The CMakeLists.txt's standalone mode has never been configured, let alone built.**
  Subdirectory mode is reasoned from two files that provably work together already
  (`IfcPlusPlus/CMakeLists.txt` + `examples/LoadFileExample/CMakeLists.txt`); standalone mode is
  new code with no working precedent in the tree to copy from, only reasoning about what
  `IfcPlusPlus`'s own `install()` step is missing. If it's going to have a mistake, this is where.
- **`MOBIUSIFC_ABI_VERSION == 1` is a starting value, not something cross-checked against any
  other file.** The staged `MobiusIfcLibrary.Build.cs` in this same directory doesn't reference an
  ABI version at all (it doesn't need to — the Build.cs never calls `MobiusIfc_GetAbiVersion`
  itself; that's a runtime check the C++ caller inside `RuntimeMeshBuilder.cpp` would need to add).
- **`EntityFactory::getStringForClassID`'s storage duration was read as a signature only**
  (`static const char* getStringForClassID(uint32_t ifcClassID);` in `IFC4X3/EntityFactory.h`) —
  its implementation (`EntityFactory.cpp`) was not read, so whether it returns a string literal or
  something else was never confirmed. Mitigated, not just flagged: this bridge always copies the
  result into its own `std::string` before handing out a `const char*`, so the lifetime contract
  of `MobiusIfcProduct::ifcClass` depends only on this file's own storage, never on that
  unconfirmed assumption. Listed here anyway for completeness.

## Out of scope, not attempted

- The renderable-class allowlist itself (handoff workstream D) — deliberately not in this DLL; see
  deviation #4/#5 above for why.
- `Build.cs` (workstream C) — already exists in this same directory as
  `MobiusIfcLibrary.Build.cs`, authored by a different, apparently concurrent session. Read it (to
  confirm this shim's header/lib/DLL naming matches what it expects — it does: `MobiusIfcBridge.h`
  / `.lib` / `.dll`, `install/{include,lib,bit}`, and it correctly does not define
  `MOBIUSIFC_BUILD_DLL`) but did not modify it — out of this task's four-file scope.
- Vendoring IFC++, running CMake, building the DLL, or touching `RuntimeMeshBuilder.cpp` — all
  explicitly the main thread's job per this task's rules, not this session's.
