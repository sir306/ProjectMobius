/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 *
 * Lightweight memory-snapshot helper for diagnosing allocation regressions.
 * The entire API is compiled out in Shipping builds — zero runtime overhead
 * in production.
 *
 * Usage:
 *   #if !UE_BUILD_SHIPPING
 *   FMobiusMemSnapshot Before = FMobiusMemSnapshot::Take(TEXT("BeforeLoad"));
 *   // ... work ...
 *   FMobiusMemSnapshot::Take(TEXT("AfterLoad")).LogDelta(Before);
 *   #endif
 */
#pragma once

#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"

PROJECTMOBIUS_API DECLARE_LOG_CATEGORY_EXTERN(LogMobiusMemory, Log, All);

struct FMobiusMemSnapshot
{
	FString  Label;
	uint64   UsedPhysical  = 0;
	uint64   UsedVirtual   = 0;
	double   TimestampSec  = 0.0;

	/** Capture a memory snapshot right now. */
	static FMobiusMemSnapshot Take(const FString& InLabel)
	{
		FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
		FMobiusMemSnapshot S;
		S.Label         = InLabel;
		S.UsedPhysical  = Stats.UsedPhysical;
		S.UsedVirtual   = Stats.UsedVirtual;
		S.TimestampSec  = FPlatformTime::Seconds();
		return S;
	}

	/** Log this snapshot's values relative to a previous one. */
	void LogDelta(const FMobiusMemSnapshot& Previous) const
	{
		const int64 DeltaPhysMB = (static_cast<int64>(UsedPhysical) - static_cast<int64>(Previous.UsedPhysical)) / (1024 * 1024);
		const int64 DeltaVirtMB = (static_cast<int64>(UsedVirtual)  - static_cast<int64>(Previous.UsedVirtual))  / (1024 * 1024);
		const double DeltaSec   = TimestampSec - Previous.TimestampSec;

		UE_LOG(LogMobiusMemory, Warning,
			TEXT("MEM [%s] Phys=%+lldMB Virt=%+lldMB (%.3fs since [%s])"),
			*Label, DeltaPhysMB, DeltaVirtMB, DeltaSec, *Previous.Label);
	}

	/** Log this snapshot's absolute values (useful for the very first snapshot). */
	void LogAbsolute() const
	{
		UE_LOG(LogMobiusMemory, Warning,
			TEXT("MEM [%s] Phys=%llumb Virt=%llumb"),
			*Label, UsedPhysical / (1024 * 1024), UsedVirtual / (1024 * 1024));
	}
};

#endif // !UE_BUILD_SHIPPING
