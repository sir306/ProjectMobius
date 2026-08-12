// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

/**
 * Compiles the vendored simdjson amalgamation (Source/ThirdParty/simdjson/simdjson.cpp,
 * Apache-2.0) as part of this module — the plugin ships no separate third-party binary. This TU
 * must stay out of unity builds (bUseUnity = false in MobiusDataImporter.Build.cs) so no UE header
 * macros leak into the third-party source.
 */

#include "MobiusSimdJson.h"

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
#include <simdjson.cpp>
THIRD_PARTY_INCLUDES_END

#pragma pop_macro("PI")
#pragma pop_macro("TEXT")
#pragma pop_macro("ensure")
#pragma pop_macro("verify")
#pragma pop_macro("check")
