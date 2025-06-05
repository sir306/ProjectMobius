// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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
