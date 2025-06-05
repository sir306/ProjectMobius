// Fill out your copyright notice in the Description page of Project Settings.
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

#include "GameInstances/WebSocketSubsystem.h"

#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

void UWebSocketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// before we attempt to create a socket and launch the server, we need to ensure that the WebSockets module is loaded
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
	{
		FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	}

	// if another instance of this application is already running then we don't want to start the server again
	if (FPlatformProcess::IsProcRunning(WebSocketServerProcHandle))
	{
		UE_LOG(LogTemp, Warning, TEXT("WebSocket server is already running."));
		
	}
	else // first application launched so need to start the server
	{
		// Start the WebSocket server
		StartWebSocketServer();
	}
	
	// Create and hook up your socket
	const FString Url = TEXT("ws://127.0.0.1:9090");// TODO: We have json config file in server read it from there for port
	Socket = FWebSocketsModule::Get().CreateWebSocket(Url);

	Socket->OnConnected().AddLambda([this]()
	{
		UE_LOG(LogTemp, Log, TEXT("WebSocket Connected"));
		
		// once we have the ID, we can send back the role of this
		TSharedPtr<FJsonObject> Cmd = MakeShared<FJsonObject>();
		Cmd->SetStringField(TEXT("type"), TEXT("register"));
		Cmd->SetStringField(TEXT("role"), TEXT("unreal"));

		// Send the command to the server
		SendJsonMessage(Cmd);
	});
	Socket->OnConnectionError().AddLambda([](const FString& Err)
	{
		UE_LOG(LogTemp, Error, TEXT("WebSocket Error: %s"), *Err);
	});
	Socket->OnMessage().AddLambda([this](const FString& Msg)
	{
		UE_LOG(LogTemp, Log, TEXT("Received WS Message -► %s"), *Msg);

		TSharedPtr<FJsonObject> Json;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg);
		if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
		{
			// look for the message that will assign us a MobiusAppID
			const FString Type = Json->GetStringField(TEXT("type"));
			if (Type == TEXT("assignId"))
			{
				// get the unique ID
				UniqueMobiusAppID  = Json->GetStringField(TEXT("id"));

				UE_LOG(LogTemp, Log, TEXT("➔ Assigned MobiusAppID = %s"),
				       *UniqueMobiusAppID);

				
			
			}

			// If we need more message handling, we can add it here
		}
	});

	Socket->Connect();
	
}

void UWebSocketSubsystem::Deinitialize()
{
	if (Socket.IsValid() && Socket->IsConnected())
	{
		// Ensure that we send the command to close the socket
		TSharedPtr<FJsonObject> Cmd = MakeShared<FJsonObject>();
		Cmd->SetStringField(TEXT("command"), TEXT("shutdown"));
		
		// send the command to the server
		SendJsonMessage(Cmd);

		// give Node a moment to close gracefully
		FPlatformProcess::Sleep(0.1f);

		// close the socket
		Socket->Close();
		Socket.Reset();

		//FPlatformProcess::CloseProc(WebSocketServerProcHandle);
	}
	
	Super::Deinitialize();
}

void UWebSocketSubsystem::StartWebSocketServer()
{
	// Setup the correct file path for the Web Socket server executable
	FString WsExePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/NodeJS/mobius-server.exe"));

	//FString WsExePath = FPaths::Combine(FPaths::LaunchDir(), TEXT("Tools/NodeJS/mobius-server.exe"));


	if (FPaths::FileExists(WsExePath))
	{
		// Store the process handle to check if it's still running
		WebSocketServerProcHandle = FPlatformProcess::CreateProc(*WsExePath, TEXT(""), true, false, false, &WebSocketServerProcessID, 0, nullptr, nullptr);
		UE_LOG(LogTemp, Log, TEXT("Launched Web Socket Server at: %s"), *WsExePath);
        
		// Give the Qt app a moment to start up
		FPlatformProcess::Sleep(0.5f);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("mobius-server.exe not found at: %s"), *WsExePath);
	}
}

void UWebSocketSubsystem::SendJsonMessage(const TSharedPtr<FJsonObject>& JsonObject) const
{
	if (!Socket.IsValid() || !Socket->IsConnected() && !JsonObject.IsValid()) return;

	FString Output;
	auto Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	Socket->Send(Output);

	// Log the sent message
	//UE_LOG(LogTemp, Log, TEXT("Sent WS Message: %s"), *Output);
}

void UWebSocketSubsystem::SendAgentDataCount(float CurrentSimTime, int32 AgentCount)
{
	{
		TSharedPtr<FJsonObject> Msg1 = MakeShared<FJsonObject>();
		Msg1->SetStringField(TEXT("action"), TEXT("appendPoint"));
		Msg1->SetNumberField(TEXT("x"), CurrentSimTime);
		Msg1->SetNumberField(TEXT("y"), AgentCount);

		SendJsonMessage(Msg1);
	}
}

void UWebSocketSubsystem::OpenOrCloseQtStatApp()
{
	// if the Qt app is running, close it
	if (QtProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(QtProcessHandle))
	{
		// 1) ask the OS to terminate the child
		FPlatformProcess::TerminateProc(QtProcessHandle); // not a nice way force kill -> set up socket cmd in qt

		// 2) close our handle on it
		FPlatformProcess::CloseProc(QtProcessHandle);

		// 3) clear out our struct
		QtProcessHandle.Reset();
	}
	else
	{
		FString QtExePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/QT_Apps/PlotUE_Data/executable/appPlotUE_Data.exe"));


		if (FPaths::FileExists(QtExePath))
		{
			// We meed too launch the QT app with args that allow it to connect to the right Unreal App instance
			FString QTAppArgs = FString::Printf(TEXT("--pairId=%s"), *UniqueMobiusAppID);

			// Note: below is an example of how you might pass additional arguments to the Qt app
			//QTAppArgs += FString::Printf(TEXT(" --mode=%s"), TEXT("live"));

			// Store the process handle to check if it's still running
			QtProcessHandle = FPlatformProcess::CreateProc(
				*QtExePath,
				*QTAppArgs, // Here we pass the unique Mobius App ID as an argument
				true,
				false,
				false,
				&QtProcessID,
				0,
				nullptr,
				nullptr);


			UE_LOG(LogTemp, Log, TEXT("Launched QT Stat app at: %s"), *QtExePath);
        
			// Give the Qt app a moment to start up
			FPlatformProcess::Sleep(0.5f);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("appPlotUE_Data.exe not found at: %s"), *QtExePath);
		}
	}
}
