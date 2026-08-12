/**
* MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "MobiusCore.h"
#include "IMobiusErrorReporter.h"
#include "Ifc/MobiusIfcMeshLoader.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Util/MemoryTraceHelper.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY(LogMobiusMemory);
#endif

#define LOCTEXT_NAMESPACE "FMobiusCoreModule"

// Getter function for the error reporter interface
static IMobiusErrorReporter* GetMobiusErrorReporterImpl(const UObject* WorldContextObject)
{
	return UMobiusUserFeedbackSubsystem::Get(WorldContextObject);
}

void FMobiusCoreModule::StartupModule()
{
	// Register our error reporter getter with the logging module
	IMobiusErrorReporter::RegisterGetterFunc(&GetMobiusErrorReporterImpl);

	// Load MobiusIfcBridge.dll up front. MobiusIfcLibrary uses PublicDelayLoadDLLs, so the first
	// MobiusIfc_* call would otherwise trigger MSVC's delay-load thunk — and if the DLL is missing at
	// that moment the thunk raises a Win32 SEH exception (ERROR_MOD_NOT_FOUND) at the call site, which
	// is not a C++ exception and cannot be caught in a module built with bEnableExceptions = false.
	// Doing it here turns a staging mistake into one log line at startup instead of a crash the first
	// time a user opens an .ifc. This is the same pattern the UE4_Assimp integration uses.
	//
	// A failure is deliberately NOT fatal: every other supported format still loads without this DLL.
	FString IfcBridgeError;
	if (!FMobiusIfcMeshLoader::EnsureBridgeLoaded(IfcBridgeError))
	{
		UE_LOG(LogTemp, Warning, TEXT("IFC import unavailable: %s"), *IfcBridgeError);
	}
}

void FMobiusCoreModule::ShutdownModule()
{
	// Unregister the getter
	IMobiusErrorReporter::RegisterGetterFunc(nullptr);
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FMobiusCoreModule, MobiusCore)