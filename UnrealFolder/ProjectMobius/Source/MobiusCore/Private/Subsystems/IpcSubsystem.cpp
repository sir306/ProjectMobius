// MIT © ProjectMobius contributors
#include "Subsystems/IpcSubsystem.h"
#include "Components/MobiusIpcClient.h" // FMobiusIpcClient
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"

namespace
{
        constexpr bool bEnableIpcQtApps = false;
}

// ------- Small cross-platform helper (kept from your old code style) -------
static const TCHAR* GetIpcPlatformFolder()
{
#if PLATFORM_WINDOWS
	return TEXT("Win64");
#elif PLATFORM_MAC
	return TEXT("Mac");
#else
	return TEXT("Unknown");
#endif
}

// ---------------------------------------------------------------------------

void UIpcSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Load endpoint name from Tools/IPC/config.json so artists/testers can change it
	// without rebuilding the game. Defaults to "MobiusIpc" if not present.
	// If you want the endpoint to be configurable:
	// Tools/IPC/config.json { "endpoint": "MobiusIpc" }
	{
		const FString ConfigPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/IPC/config.json"));
		FString JsonString;
		if (FFileHelper::LoadFileToString(JsonString, *ConfigPath))
		{
			TSharedPtr<FJsonObject> Obj;
			const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonString);
			if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
			{
				FString Ep;
				if (Obj->TryGetStringField(TEXT("endpoint"), Ep) && !Ep.IsEmpty())
				{
					EndpointName = Ep;
				}
			}
		}
	}

        if (bEnableIpcQtApps)
        {
                StartIpcClient();
        }
}

void UIpcSubsystem::Deinitialize()
{
	// Graceful notify (optional)
	{
		TSharedPtr<FJsonObject> Cmd = MakeShared<FJsonObject>();
		Cmd->SetStringField(TEXT("command"), TEXT("shutdown"));
		SendJsonMessage(Cmd);
	}

	if (IpcClient.IsValid())
	{
		IpcClient->Shutdown();
		IpcClient.Reset();
	}

	Super::Deinitialize();
}

void UIpcSubsystem::StartIpcClient()
{
        if (!bEnableIpcQtApps)
        {
                return;
        }
        // Bind message callback first so we can receive immediately after connect.
	// The client handles its own worker thread internally.
	IpcClient = MakeShared<FMobiusIpcClient>(
		EndpointName,
		FOnIpcMessage::CreateUObject(this, &UIpcSubsystem::OnIpcMessage));

	// Spin up the client thread
	if (!IpcClient->Start())
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("IPC Error"),
				FText::FromString("Client thread failed to start"),
				FText::FromString("Failed to start IPC client thread."),
				FText::FromString("IpcSubsystem"));
		}
		UE_LOG(LogTemp, Error, TEXT("IPC: Failed to start client thread for endpoint '%s'"), *EndpointName);
		return;
	}

	// Send a lightweight registration message so the Qt side knows Unreal is alive.
	// This mirrors the previous WebSocket handshake and keeps the protocols similar.
	{
		TSharedPtr<FJsonObject> Cmd = MakeShared<FJsonObject>();
		Cmd->SetStringField(TEXT("type"), TEXT("register"));
		Cmd->SetStringField(TEXT("role"), TEXT("unreal"));
		SendJsonMessage(Cmd);
	}

	// Optionally auto-launch the Qt app (if that’s your pattern)
	// LaunchQtStatsAppOrToggle(); // comment in if desired
}

void UIpcSubsystem::OnIpcMessage(const TArray<uint8>& Bytes)
{
    // Convert UTF-8 bytes to FString
    FString Str;
    FFileHelper::BufferToString(Str, Bytes.GetData(), Bytes.Num());

    // Parse JSON
    TSharedPtr<FJsonObject> JsonObj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Str);
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("IPC: Received invalid JSON (%d bytes): %s"),
               Bytes.Num(), *Str);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("IPC Received: %s"), *Str); // ✅ ADD THIS DEBUG LINE

    // Handle "type" messages (register, etc.)
    FString Type;
    if (JsonObj->TryGetStringField(TEXT("type"), Type))
    {
        if (Type == TEXT("register"))
        {
            FString Role;
            if (JsonObj->TryGetStringField(TEXT("role"), Role))
            {
                UE_LOG(LogTemp, Log, TEXT("IPC: Client registered with role = %s"), *Role);
            }
        }
        return; // ✅ Important: return here so we don't also check "action"
    }

    // // Handle "action" messages (getData, etc.)
    // FString Action;
    // if (JsonObj->TryGetStringField(TEXT("action"), Action))
    // {
    //     if (Action == TEXT("getData"))
    //     {
    //         UE_LOG(LogTemp, Log, TEXT("IPC: Qt requested data via getData"));
    //         
    //         // ✅ RESPOND WITH CURRENT STATE
    //         // This is critical - Qt is polling, so we must respond!
    //         if (UWorld* World = GetWorld())
    //         {
    //             if (UTimeDilationSubSystem* TimeDilationSys = World->GetSubsystem<UTimeDilationSubSystem>())
    //             {
    //                 float CurrentTime = TimeDilationSys->GetCurrentSimTime();
    //                 
    //                 // Send a simple acknowledgment with current time
    //                 TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    //                 Response->SetStringField(TEXT("action"), TEXT("heartbeat"));
    //                 Response->SetNumberField(TEXT("time"), CurrentTime);
    //                 SendJsonMessage(Response);
    //                 
    //                 UE_LOG(LogTemp, Log, TEXT("IPC: Responded to getData with time=%f"), CurrentTime);
    //             }
    //         }
    //     }
    // }
}



void UIpcSubsystem::SendJsonMessage(const TSharedPtr<FJsonObject>& JsonObject) const
{
        if (!bEnableIpcQtApps)
        {
                return;
        }
        if (!IpcClient.IsValid() || !JsonObject.IsValid())
                return;

	FString Out;
	auto Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	// UTF-8 encode + length-prefix sending
	TArray<uint8> Bytes;
	Bytes.Reset(Out.Len() * 3 + 8); // worst-case reserve
	FTCHARToUTF8 Utf8(*Out);
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

	IpcClient->Send(Bytes);
	// UE_LOG(LogTemp, Verbose, TEXT("IPC Sent: %s"), *Out);
}

void UIpcSubsystem::SendAgentDataCount(float CurrentSimTime, int32 AgentCount)  
{
        if (!bEnableIpcQtApps)
        {
                return;
        }
        TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
        Msg->SetStringField(TEXT("action"), TEXT("appendPoint"));
	Msg->SetNumberField(TEXT("x"), CurrentSimTime);
	Msg->SetNumberField(TEXT("y"), AgentCount);
	SendJsonMessage(Msg);
}

void UIpcSubsystem::OpenOrCloseQtStatApp()
{
        if (!bEnableIpcQtApps)
        {
                return;
        }
        LaunchQtStatsAppOrToggle();
}

void UIpcSubsystem::LaunchQtStatsAppOrToggle()
{
        if (!bEnableIpcQtApps)
        {
                return;
        }
        // If already running, kill it
	if (QtProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(QtProcessHandle))
	{
		FPlatformProcess::TerminateProc(QtProcessHandle);
		FPlatformProcess::CloseProc(QtProcessHandle);
		QtProcessHandle.Reset();
		return;
	}

	const TCHAR* Plat = GetIpcPlatformFolder();
	FString QtExePath;
    
#if PLATFORM_WINDOWS
	QtExePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/bin"), Plat, 
								TEXT("PlotUE_Data"), TEXT("appPlotUE_Data.exe"));
#elif PLATFORM_MAC
	QtExePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/bin"), Plat, 
								TEXT("PlotUE_Data.app/Contents/MacOS/appPlotUE_Data"));
#endif

	if (!FPaths::FileExists(QtExePath))
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(this))
		{
			Feedback->ReportError(
				FText::FromString("Qt App Error"),
				FText::FromString("PlotUE_Data executable missing"),
				FText::FromString("Qt stats application not found in Tools/bin."),
				FText::FromString("IpcSubsystem"));
		}
		UE_LOG(LogTemp, Error, TEXT("Qt app not found at: %s"), *QtExePath);
		return;
	}

	// ✅ Only pass endpoint, no pairId needed
	const FString Args = FString::Printf(TEXT("--endpoint=%s"), *EndpointName);

	QtProcessHandle = FPlatformProcess::CreateProc(
		*QtExePath, *Args,
		/*bLaunchDetached*/ true,
		/*bLaunchHidden*/ false,
		/*bLaunchReallyHidden*/ false,
		&QtProcessID, 0, nullptr, nullptr);

	UE_LOG(LogTemp, Log, TEXT("Launched Qt stats app: %s (endpoint=%s)"), 
		   *QtExePath, *EndpointName);
}
