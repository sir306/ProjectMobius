// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WebSocketSubsystem.generated.h"

class IWebSocket;
/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UWebSocketSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Called once GameInstance is up
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// Called during GameInstance shutdown
	virtual void Deinitialize() override;

	/**
	 * Start the Websocket Executable so we can communicate with it
	 */
	UFUNCTION(BlueprintCallable)
	void StartWebSocketServer();

	/**
	 * Helper to send a JSON payload
	 * @param[FJsonObject&] JsonObject The JSON object to send to the server
	 */
	void SendJsonMessage(const TSharedPtr<FJsonObject>& JsonObject) const;

	UFUNCTION(BlueprintCallable)
	void SendAgentDataCount(float CurrentSimTime, int32 AgentCount);

	/** Open or Close the QT Stat app */
	UFUNCTION(BlueprintCallable)
	void OpenOrCloseQtStatApp();

private:
	// The WebSocket
	TSharedPtr<IWebSocket> Socket;

	// Qt App ProcHandle
	FProcHandle QtProcessHandle;
	
	// Process ID of the Qt App
	uint32 QtProcessID;

	// WebSocket Server ProcHandle
	FProcHandle WebSocketServerProcHandle;

	// Process ID of the WebSocket Server
	uint32 WebSocketServerProcessID;

	// Unique ID for this application instance - so we can talk to the correct qt apps
	FString UniqueMobiusAppID = "";
};
