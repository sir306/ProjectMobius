// SPDX-License-Identifier: MIT
//
// Where the tests look for the PRIVATE datasets that are not part of this repository.
//
// Several automation tests exercise real exports (multi-room B-RISK cases, large agent files,
// IFC4X3 models) that are too large, or not ours, to commit. Those tests SKIP when the fixture is
// absent, so a contributor sees them pass without ever having the data.
//
// Every site used to carry its own list of absolute drive paths -- D:/NickWork/..., E:/00_Work/...,
// F:/... -- which published one maintainer's drive layout in a public repository and worked on
// exactly one machine. Roots are derived relative to the project instead, with an environment
// variable for anything that does not fit that shape.
//
// Resolution order (first hit wins):
//   1. %MOBIUS_INTERNAL_DATA%       explicit override; the only thing to set on CI or an unusual layout
//   2. <workspace>/Mobius_InternalData    beside the repo -- the documented layout
//   3. <repo>/Mobius_InternalData         inside the repo checkout
//   4. <project>/Mobius_InternalData      beside the .uproject
//
// Adding a drive letter back here is a regression. If a machine needs a path none of these cover,
// set MOBIUS_INTERNAL_DATA.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"

namespace MobiusTestData
{
	/** Candidate roots for the private dataset folder, most likely first. Never empty. */
	inline TArray<FString> GetInternalDataRoots()
	{
		TArray<FString> Roots;

		const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("MOBIUS_INTERNAL_DATA"));
		if (!Override.IsEmpty())
		{
			Roots.Add(Override);
		}

		// FPaths::ProjectDir() is <workspace>/ProjectMobius/UnrealFolder/ProjectMobius/, so:
		//   ../../..  -> <workspace>       (Mobius_InternalData sits beside the repo)
		//   ../..     -> <repo>
		const FString ProjectDir = FPaths::ProjectDir();
		const TCHAR* RelativeRoots[] = { TEXT("../../.."), TEXT("../.."), TEXT(".") };
		for (const TCHAR* Relative : RelativeRoots)
		{
			Roots.Add(FPaths::ConvertRelativePathToFull(
				FPaths::Combine(ProjectDir, Relative, TEXT("Mobius_InternalData"))));
		}

		return Roots;
	}

	/**
	 * Resolve RelativePath under the first root that actually contains it.
	 * Returns an empty string when the fixture is not on this machine -- callers are expected to
	 * skip (and AddInfo saying so), not to fail.
	 */
	inline FString FindInternalFixture(const FString& RelativePath)
	{
		for (const FString& Root : GetInternalDataRoots())
		{
			const FString Candidate = FPaths::Combine(Root, RelativePath);
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return FString();
	}

	/** One line naming what to set, for the AddInfo a skipping test should emit. */
	inline FString DescribeMissingFixture(const FString& RelativePath)
	{
		return FString::Printf(
			TEXT("SKIPPED: private fixture '%s' is not on this machine. It is not part of the ")
			TEXT("repository; set MOBIUS_INTERNAL_DATA to the folder holding it, or place that ")
			TEXT("folder beside the repo as <workspace>/Mobius_InternalData."),
			*RelativePath);
	}
}
