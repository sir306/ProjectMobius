// Source/MobiusIPC/Public/MobiusIpcClient.h
#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Templates/SharedPointer.h"

// Simple delegate for incoming frames (already de-framed)
DECLARE_DELEGATE_OneParam(FOnIpcMessage, const TArray<uint8>&);

class MOBIUSCORE_API FMobiusIpcClient : public FRunnable, public TSharedFromThis<FMobiusIpcClient>
{
public:
	FMobiusIpcClient(const FString& InEndpointName, FOnIpcMessage InOnMsg);
	virtual ~FMobiusIpcClient() override;

	// Call from owning code (subsystem) or destructor
	void Shutdown();

	// Start/Stop background thread
	bool Start();
	void Stop();
	virtual void Exit() override {}
	uint32 Run() override;

	// Nonblocking send of a single framed message
	bool Send(const TArray<uint8>& Payload);

private:
	bool Platform_Connect();
	void Platform_Disconnect();
	bool Platform_BlockingRead(uint8* Dst, int32 BytesToRead);
	bool Platform_BlockingWrite(const uint8* Src, int32 BytesToWrite);

private:
	FString EndpointName;
	FOnIpcMessage OnMessage;
	FThreadSafeBool bRun{false};
	FRunnableThread* Thread{nullptr};

	// Outgoing message queue
	TQueue<TArray<uint8>, EQueueMode::Mpsc> OutgoingQueue;

#if PLATFORM_WINDOWS
	void* PipeHandle{nullptr}; // HANDLE
#elif PLATFORM_MAC
	int SocketFd{-1};
#endif
};
