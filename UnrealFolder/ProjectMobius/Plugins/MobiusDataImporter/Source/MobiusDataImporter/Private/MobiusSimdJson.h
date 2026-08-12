// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
#pragma once

/**
 * Single sanctioned include point for the vendored simdjson amalgamation
 * (Source/ThirdParty/simdjson, perf task A7). Include THIS header, never <simdjson.h> directly:
 * it shields the third-party code from UE's function-like macros and relaxes UE's
 * warnings-as-errors around it.
 *
 * SIMDJSON_EXCEPTIONS=0 is forced module-wide in MobiusDataImporter.Build.cs (UE compiles without
 * exception support), so ONLY the error-code API is usable here: every simdjson_result must be
 * unwrapped with .get(out) / .error() — accessors that throw do not exist in this configuration.
 */

#include "CoreMinimal.h"

// UE defines these as function-like macros; simdjson does not use them today (verified against
// v4.6.4), but push/undef keeps a future upgrade from breaking mysteriously mid-header.
#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("ensure")
#pragma push_macro("TEXT")
#pragma push_macro("PI")
#undef check
#undef verify
#undef ensure
#undef TEXT
#undef PI

THIRD_PARTY_INCLUDES_START
#ifdef _MSC_VER
// simdjson's ondemand iterators assign inside their loop conditions by design. MSVC evaluates
// C4706 for templates with the warning state of the DEFINITION site (this include), not the
// instantiation site, and THIRD_PARTY_INCLUDES does not cover 4706 — so it must be disabled here.
#pragma warning(disable: 4706)
#endif
#include <simdjson.h>
THIRD_PARTY_INCLUDES_END

#pragma pop_macro("PI")
#pragma pop_macro("TEXT")
#pragma pop_macro("ensure")
#pragma pop_macro("verify")
#pragma pop_macro("check")

#ifdef _MSC_VER
// simdjson's ondemand iterators assign inside their loop conditions by design. The warning fires
// at template-instantiation time in TUs that iterate objects/arrays — i.e. outside the
// THIRD_PARTY_INCLUDES relaxed region above — so it must stay disabled for any TU using this
// header (which is why including <simdjson.h> directly is not allowed).
#pragma warning(disable: 4706)
#endif
