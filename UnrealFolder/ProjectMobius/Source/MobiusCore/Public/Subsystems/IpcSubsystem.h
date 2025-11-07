// MIT © ProjectMobius contributors
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IpcSubsystem.generated.h"

// Forward decl from your IPC module
class FMobiusIpcClient;

/**
 * GameInstance subsystem that talks to the local Qt app via IPC
 * (Named Pipe on Windows, Unix Domain Socket on macOS).
 *
 * It mirrors the old WebSocket subsystem API to minimize churn.
 */
UCLASS()
class MOBIUSCORE_API UIpcSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Subsystem lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Open or close the Qt stats app (unchanged external contract) */
	UFUNCTION(BlueprintCallable)
	void OpenOrCloseQtStatApp();

	/** Convenience: send a single “(time, count)” point as JSON */
	UFUNCTION(BlueprintCallable)
	void SendAgentDataCount(float CurrentSimTime, int32 AgentCount);

	/** Generic: send any JSON object */
	void SendJsonMessage(const TSharedPtr<FJsonObject>& JsonObject) const;

	/** Optional: set custom endpoint before Initialize runs (e.g. from config) */
	void SetEndpointName(const FString& InName) { EndpointName = InName; }

private:
	/** Starts the background IPC client thread and performs initial registration */
	void StartIpcClient();

	/** UE thread callback for new raw message frames from the client thread */
	void OnIpcMessage(const TArray<uint8>& Bytes);

	/** Launch the Qt app (reused from your old subsystem, path logic intact) */
	void LaunchQtStatsAppOrToggle();

private:
	// Client runs its own thread internally
	TSharedPtr<FMobiusIpcClient> IpcClient;

	// Optional state shared with the Qt side
	UPROPERTY()
	FString UniqueMobiusAppID;

	// Process handle for the Qt app (unchanged behavior)
	FProcHandle QtProcessHandle{};
	UPROPERTY() uint32 QtProcessID = 0;

	// IPC endpoint name. Default matches Qt’s QLocalServer name.
	UPROPERTY() FString EndpointName = TEXT("MobiusIpc");
};
