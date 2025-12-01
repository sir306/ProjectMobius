// Lightweight file logger to capture early begin play hitches without blocking the game thread.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Subsystems/EngineSubsystem.h"
#include "MobiusCustomLoggerSubsystem.generated.h"

/**
 * Engine-level logger that opens a text file next to the launched executable and writes messages via a queue.
 * Designed to be cheap to call from BeginPlay (including blueprints) to spot startup stalls.
 */
UCLASS()
class MOBIUSCORE_API UMobiusCustomLoggerSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	UMobiusCustomLoggerSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Queue a line of text for the startup log (thread-safe, blueprint callable). */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logging")
	void EnqueueLogMessage(const FString& Message);

	/** Queue a timing entry in milliseconds for long-running operations. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logging")
	void EnqueueTimedMessage(const FString& EventName, float DurationMilliseconds);

	/** Force an immediate flush of any queued messages to disk. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logging")
	void FlushQueuedMessages();

	/** Convenience accessor so blueprints can fetch the subsystem directly. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logging", meta = (WorldContext = "WorldContextObject"))
	static UMobiusCustomLoggerSubsystem* Get(const UObject* WorldContextObject);

private:
	FString LogFilePath;
	FCriticalSection QueueMutex;
	TQueue<FString, EQueueMode::Mpsc> PendingMessages;
	FTSTicker::FDelegateHandle FlushTickerHandle;
	FThreadSafeBool bIsFlushing;

	bool PumpLogs(float DeltaTime);
	void FlushToDisk();
	FString BuildTimestampedLine(const FString& Message) const;
};
