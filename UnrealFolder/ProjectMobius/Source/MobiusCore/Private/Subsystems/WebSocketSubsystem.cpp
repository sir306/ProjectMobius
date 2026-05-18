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

#include "Subsystems//WebSocketSubsystem.h"

#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"

namespace
{
        constexpr bool bEnableWebSocketQtApps = false;
}

// Utility: map UE platform to the folder your superbuild uses
static const TCHAR* GetWebSocketPlatformFolder()
{
#if PLATFORM_WINDOWS
	return TEXT("Win64");
#elif PLATFORM_MAC
	return TEXT("Mac");
#elif PLATFORM_LINUX
	return TEXT("Linux");
#else
	return TEXT("Unknown");
#endif
}

/**
 * Helper function to read the WebSocket port from Tools/NodeJS/config.json.
 * Returns 9090 if the file can't be read or parsed.
 */
static int32 LoadWebSocketPort()
{
	const FString ConfigPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/NodeJS/config.json"));
	FString JsonString;
	if (FFileHelper::LoadFileToString(JsonString, *ConfigPath))
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			int32 Port;
			if (JsonObject->TryGetNumberField(TEXT("port"), Port))
			{
				return Port;
			}
		}
	}
	return 9090;
}

void UWebSocketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// before we attempt to create a socket and launch the server, we need to ensure that the WebSockets module is loaded
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
	{
		FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	}

	// DEPRECEATED - we no longer start a server - and use an IPC protocol now, this is legacy code for others to use if they want to -- see MobiusIPCSubsystem
	
	// // if another instance of this application is already running then we don't want to start the server again
	// if (FPlatformProcess::IsProcRunning(WebSocketServerProcHandle))
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("WebSocket server is already running."));
	// 	
	// }
	// else // first application launched so need to start the server
	// {
	// 	// Start the WebSocket server
	// 	StartWebSocketServer();
	// }
	//
	// // Create and hook up your socket. The port is read from Tools/NodeJS/config.json,
	// // falling back to 9090 if the file cannot be parsed.
	// const int32 Port = LoadWebSocketPort();
	// const FString Url = FString::Printf(TEXT("ws://127.0.0.1:%d"), Port);
	// Socket = FWebSocketsModule::Get().CreateWebSocket(Url);
	//
	// Socket->OnConnected().AddLambda([this]()
	// {
	// 	UE_LOG(LogTemp, Log, TEXT("WebSocket Connected"));
	// 	
	// 	// once we have the ID, we can send back the role of this
	// 	TSharedPtr<FJsonObject> Cmd = MakeShared<FJsonObject>();
	// 	Cmd->SetStringField(TEXT("type"), TEXT("register"));
	// 	Cmd->SetStringField(TEXT("role"), TEXT("unreal"));
	//
	// 	// Send the command to the server
	// 	SendJsonMessage(Cmd);
	// });
	// Socket->OnConnectionError().AddLambda([](const FString& Err)
	// {
	// 	UE_LOG(LogTemp, Error, TEXT("WebSocket Error: %s"), *Err);
	// });
	// Socket->OnMessage().AddLambda([this](const FString& Msg)
	// {
	// 	UE_LOG(LogTemp, Log, TEXT("Received WS Message -► %s"), *Msg);
	//
	// 	TSharedPtr<FJsonObject> Json;
	// 	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Msg);
	// 	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	// 	{
	// 		// look for the message that will assign us a MobiusAppID
	// 		const FString Type = Json->GetStringField(TEXT("type"));
	// 		if (Type == TEXT("assignId"))
	// 		{
	// 			// get the unique ID
	// 			UniqueMobiusAppID  = Json->GetStringField(TEXT("id"));
	//
	// 			UE_LOG(LogTemp, Log, TEXT("➔ Assigned MobiusAppID = %s"),
	// 			       *UniqueMobiusAppID);
	//
	// 			
	// 		
	// 		}
	//
	// 		// If we need more message handling, we can add it here
	// 	}
	// });
	//
	// Socket->Connect();
	
}

void UWebSocketSubsystem::Deinitialize()
{
	Super::Deinitialize();
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
	const TCHAR* Plat = GetWebSocketPlatformFolder();

	// Where the superbuild stages Node binaries now:
	//   Tools/bin/<Platform>/NodeJS/...
	FString NodeDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/bin"), Plat, TEXT("NodeJS"));

	FString WsExePath;

#if PLATFORM_WINDOWS
	// Superbuild renames/copies to mobius-server.exe under Tools/bin/Win64/NodeJS/
	WsExePath = FPaths::Combine(NodeDir, TEXT("mobius-server.exe"));
#elif PLATFORM_MAC
	// Superbuild emits both archs. Prefer arm64, fallback to x64.
	const FString Arm = FPaths::Combine(NodeDir, TEXT("mobius-server-macos-arm64"));
	const FString X64 = FPaths::Combine(NodeDir, TEXT("mobius-server-macos-x64"));
	WsExePath = FPaths::FileExists(Arm) ? Arm : X64; // fix this logic mac - could make both but may not run both correctly
#else
	// If you add Linux in superbuild, it’ll be Tools/bin/Linux/NodeJS/mobius-server-linux-x64
	WsExePath = FPaths::Combine(NodeDir, TEXT("mobius-server-linux-x64"));
#endif

	if (FPaths::FileExists(WsExePath))
	{
		// Launch detached
		WebSocketServerProcHandle = FPlatformProcess::CreateProc(
			*WsExePath, TEXT(""), /*bLaunchDetached*/ true,
			/*bLaunchHidden*/ false, /*bLaunchReallyHidden*/ false,
			&WebSocketServerProcessID, 0, nullptr, nullptr);

		UE_LOG(LogTemp, Log, TEXT("Launched Web Socket Server at: %s"), *WsExePath);
		FPlatformProcess::Sleep(0.25f);
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("WebSocket Error"),
				FText::FromString("Server executable missing"),
				FText::FromString("mobius-server executable not found in Tools/bin."),
				FText::FromString("WebSocketSubsystem"));
		}
		UE_LOG(LogTemp, Error, TEXT("mobius-server not found at: %s"), *WsExePath);
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
        if (!bEnableWebSocketQtApps)
        {
                return;
        }
        // If already running, kill it (unchanged behavior)
	if (QtProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(QtProcessHandle))
	{
		FPlatformProcess::TerminateProc(QtProcessHandle);
		FPlatformProcess::CloseProc(QtProcessHandle);
		QtProcessHandle.Reset();
		return;
	}

	const TCHAR* Plat = GetWebSocketPlatformFolder();

	// Where the superbuild stages PlotUE_Data now:
	//   Windows: Tools/bin/Win64/PlotUE_Data/appPlotUE_Data.exe
	//   macOS:   Tools/bin/Mac/PlotUE_Data.app/Contents/MacOS/appPlotUE_Data
	FString QtExePath;

#if PLATFORM_WINDOWS
	QtExePath = FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("Tools/bin"), Plat, TEXT("PlotUE_Data"),
		TEXT("appPlotUE_Data.exe"));
#elif PLATFORM_MAC
	QtExePath = FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("Tools/bin"), Plat, TEXT("PlotUE_Data.app/Contents/MacOS/appPlotUE_Data"));
#else
	QtExePath = FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("Tools/bin"), Plat, TEXT("PlotUE_Data/appPlotUE_Data"));
#endif

	if (FPaths::FileExists(QtExePath))
	{
		const FString Args = FString::Printf(TEXT("--pairId=%s"), *UniqueMobiusAppID);

		QtProcessHandle = FPlatformProcess::CreateProc(
			*QtExePath, *Args,
			/*bLaunchDetached*/ true,
			/*bLaunchHidden*/ false,
			/*bLaunchReallyHidden*/ false,
			&QtProcessID, 0, nullptr, nullptr);

		UE_LOG(LogTemp, Log, TEXT("Launched Qt stats app at: %s"), *QtExePath);
		FPlatformProcess::Sleep(0.25f);
	}
	else
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Qt App Error"),
				FText::FromString("PlotUE_Data executable missing"),
				FText::FromString("Qt stats application not found in Tools/bin."),
				FText::FromString("WebSocketSubsystem"));
		}
		UE_LOG(LogTemp, Error, TEXT("Qt app not found at: %s"), *QtExePath);
	}
}
