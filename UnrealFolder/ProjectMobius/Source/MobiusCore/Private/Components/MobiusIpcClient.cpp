#include "Components/MobiusIpcClient.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_MAC
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

// Helper: blocking read of exactly N bytes on this connection
static bool ReadExact(TFunctionRef<bool(uint8*, int32)> ReadFn, uint8* Dst, int32 N)
{
    int32 Offset = 0;
    while (Offset < N)
    {
        if (!ReadFn(Dst + Offset, N - Offset))
        {
            return false; // connection closed or error
        }
        // Our Platform_BlockingRead is "all or fail", so if we get here once,
        // we've read everything. If you ever change it to partial reads,
        // update this to accumulate properly.
        Offset = N;
    }
    return true;
}

FMobiusIpcClient::FMobiusIpcClient(const FString& InEndpointName, FOnIpcMessage InOnMsg)
    : EndpointName(InEndpointName)
    , OnMessage(InOnMsg)
{
}

FMobiusIpcClient::~FMobiusIpcClient()
{
    Shutdown();
}

void FMobiusIpcClient::Shutdown()
{
    if (!Thread)
        return;

    // Tell thread to exit and unblock I/O
    bRun = false;
    Platform_Disconnect();

    // Join
    Thread->WaitForCompletion();
    delete Thread;
    Thread = nullptr;
}

bool FMobiusIpcClient::Start()
{
    if (Thread)
    {
        return true;
    }

    bRun = true;
    Thread = FRunnableThread::Create(this, TEXT("MobiusIPC_Thread"), 0, TPri_AboveNormal);
    return Thread != nullptr;
}

void FMobiusIpcClient::Stop()
{
    // Just signal and unblock; DO NOT join or delete here.
    bRun = false;
    Platform_Disconnect();
}

// ---------------------- Core async send ----------------------

bool FMobiusIpcClient::Send(const TArray<uint8>& Payload)
{
    if (!bRun || Payload.Num() == 0)
    {
        return false;
    }

    // Build framed message: [u32_le length][payload...]
    TArray<uint8> Frame;
    const uint32 Len = (uint32)Payload.Num();
    Frame.Reserve(sizeof(uint32) + Payload.Num());
    Frame.Append(reinterpret_cast<const uint8*>(&Len), sizeof(uint32));
    Frame.Append(Payload.GetData(), Payload.Num());

    // Enqueue for the IPC thread to send; non-blocking for callers.
    OutgoingQueue.Enqueue(MoveTemp(Frame));
    return true;
}

// ---------------------- Thread loop ----------------------

uint32 FMobiusIpcClient::Run()
{
    // Worker thread: connect, then loop sending queued messages and reading frames.
    // Each frame is length-prefixed (u32 little-endian) so we know how many bytes to read.
    while (bRun)
    {
        if (!Platform_Connect())
        {
            FPlatformProcess::Sleep(0.25f);
            continue;
        }

        bool bConnected = true;

        while (bRun && bConnected)
        {
            // 1) Flush outgoing messages
            {
                TArray<uint8> Frame;
                while (OutgoingQueue.Dequeue(Frame))
                {
                    if (!Platform_BlockingWrite(Frame.GetData(), Frame.Num()))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("IPC: Write failed, disconnecting"));
                        bConnected = false;
                        break;
                    }
                }
            }
            if (!bConnected)
            {
                break;
            }

            // 2) Try to read a frame if available (non-busy, low overhead)

#if PLATFORM_WINDOWS
            // Peek to avoid hard blocking when there's no data
            DWORD BytesAvailable = 0;
            if (!PeekNamedPipe(PipeHandle, nullptr, 0, nullptr, &BytesAvailable, nullptr))
            {
                UE_LOG(LogTemp, Warning, TEXT("IPC: PeekNamedPipe failed, disconnecting"));
                bConnected = false;
                break;
            }

            if (BytesAvailable >= sizeof(uint32))
            {
                uint32 LenLE = 0;
                if (!Platform_BlockingRead(reinterpret_cast<uint8*>(&LenLE), sizeof(uint32)))
                {
                    bConnected = false;
                    break;
                }

                const uint32 PayloadLen = LenLE;
                if (PayloadLen == 0 || PayloadLen > 128u * 1024u * 1024u)
                {
                    UE_LOG(LogTemp, Warning, TEXT("IPC: Invalid payload length %u, disconnecting"), PayloadLen);
                    bConnected = false;
                    break;
                }

                TArray<uint8> Buf;
                Buf.SetNumUninitialized(PayloadLen);

                if (!ReadExact(
                        [this](uint8* D, int32 N) { return Platform_BlockingRead(D, N); },
                        Buf.GetData(),
                        (int32)PayloadLen))
                {
                    bConnected = false;
                    break;
                }

                // Delegate listeners (e.g. IpcSubsystem::OnIpcMessage) touch UObject
                // state — marshal to GT and re-validate the client is still alive.
                if (OnMessage.IsBound())
                {
                    TWeakPtr<FMobiusIpcClient> WeakSelf = AsShared();
                    AsyncTask(ENamedThreads::GameThread,
                        [WeakSelf, Payload = MoveTemp(Buf)]()
                        {
                            if (TSharedPtr<FMobiusIpcClient> Self = WeakSelf.Pin())
                            {
                                Self->OnMessage.ExecuteIfBound(Payload);
                            }
                        });
                }
            }
#elif PLATFORM_MAC
            // Simple readable check; can be refined with poll/select if needed.
            fd_set ReadSet;
            FD_ZERO(&ReadSet);
            FD_SET(SocketFd, &ReadSet);

            timeval Tv;
            Tv.tv_sec = 0;
            Tv.tv_usec = 1000; // 1ms

            const int Sel = select(SocketFd + 1, &ReadSet, nullptr, nullptr, &Tv);
            if (Sel > 0 && FD_ISSET(SocketFd, &ReadSet))
            {
                uint32 LenLE = 0;
                ssize_t R = recv(SocketFd, &LenLE, sizeof(uint32), MSG_WAITALL);
                if (R != sizeof(uint32))
                {
                    bConnected = false;
                    break;
                }

                const uint32 PayloadLen = LenLE;
                if (PayloadLen == 0 || PayloadLen > 128u * 1024u * 1024u)
                {
                    bConnected = false;
                    break;
                }

                TArray<uint8> Buf;
                Buf.SetNumUninitialized(PayloadLen);
                if (!ReadExact(
                        [this](uint8* D, int32 N) { return Platform_BlockingRead(D, N); },
                        Buf.GetData(),
                        (int32)PayloadLen))
                {
                    bConnected = false;
                    break;
                }

                // Delegate listeners (e.g. IpcSubsystem::OnIpcMessage) touch UObject
                // state — marshal to GT and re-validate the client is still alive.
                if (OnMessage.IsBound())
                {
                    TWeakPtr<FMobiusIpcClient> WeakSelf = AsShared();
                    AsyncTask(ENamedThreads::GameThread,
                        [WeakSelf, Payload = MoveTemp(Buf)]()
                        {
                            if (TSharedPtr<FMobiusIpcClient> Self = WeakSelf.Pin())
                            {
                                Self->OnMessage.ExecuteIfBound(Payload);
                            }
                        });
                }
            }
            else
            {
                // no data, just fall through
            }
#endif

            // Tiny sleep to avoid spinning hot if no traffic
            FPlatformProcess::Sleep(0.001f);
        }

        Platform_Disconnect();
        FPlatformProcess::Sleep(0.1f);
    }

    return 0;
}

// ---------------------- Platform helpers ----------------------

bool FMobiusIpcClient::Platform_Connect()
{
#if PLATFORM_WINDOWS
    const FString PipePath = FString::Printf(TEXT(R"(\\.\pipe\%s)"), *EndpointName);
    const FTCHARToUTF8 Ansi(*PipePath);

    //UE_LOG(LogTemp, Log, TEXT("IPC: Attempting to connect to pipe: %s"), *PipePath);

    while (bRun)
    {
        HANDLE H = CreateFileA(
            Ansi.Get(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (H != INVALID_HANDLE_VALUE)
        {
            PipeHandle = H;
            //UE_LOG(LogTemp, Log, TEXT("IPC: Connected to pipe"));
            return true;
        }

        const DWORD Err = GetLastError();
        if (Err == ERROR_PIPE_BUSY)
        {
            WaitNamedPipeA(Ansi.Get(), 200);
            continue;
        }

        //UE_LOG(LogTemp, Error, TEXT("IPC: Failed to connect to pipe, error=%lu"), Err);
        break;
    }
    return false;

#elif PLATFORM_MAC
    auto AttemptConnect = [this](const FString& Path) -> bool
    {
        // Mac/Linux use Unix Domain Sockets. Qt's QLocalServer defaults to /tmp/<name>
        // on macOS; older builds may suffix ".sock", so we try both.
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
			FMobiusErrorMessage Payload;
			Payload.TitleBarText = FText::FromString("IPC Error");
			Payload.ErrorTitle = FText::FromString("Socket creation failed");
			Payload.ErrorMessage = FText::FromString("IPC failed to create a socket.");
			Payload.ErrorLocation = FText::FromString("MobiusIpcClient");
			UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(TWeakObjectPtr<UObject>(), Payload);
            UE_LOG(LogTemp, Error, TEXT("IPC: Failed to create socket"));
            return false;
        }

        sockaddr_un addr;
        FMemory::Memzero(&addr, sizeof(addr));
        addr.sun_family = AF_UNIX;

        FTCHARToUTF8 Utf8Path(*Path);
        const size_t MaxLen = sizeof(addr.sun_path) - 1;
        FCStringAnsi::Strncpy(addr.sun_path, Utf8Path.Get(), MaxLen);
        addr.sun_path[MaxLen] = '\0';

        UE_LOG(LogTemp, Log, TEXT("IPC: Attempting to connect to socket: %s"), *Path);

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
        {
            SocketFd = fd;
            UE_LOG(LogTemp, Log, TEXT("IPC: Connected to socket"));
            return true;
        }

        UE_LOG(LogTemp, Warning, TEXT("IPC: Failed to connect to socket %s, errno=%d"), *Path, errno);
        ::close(fd);
        return false;
    };

    // Qt's QLocalServer on macOS creates sockets in /tmp/<name>. Try the canonical
    // path first, then fall back to ".sock" for older builds or manual servers.
    const FString CanonicalPath = FString::Printf(TEXT("/tmp/%s"), *EndpointName);
    const FString SockPath = CanonicalPath + TEXT(".sock");

    if (AttemptConnect(CanonicalPath))
    {
        return true;
    }

    if (AttemptConnect(SockPath))
    {
        return true;
    }

	FMobiusErrorMessage Payload;
	Payload.TitleBarText = FText::FromString("IPC Error");
	Payload.ErrorTitle = FText::FromString("Unable to connect to socket");
	Payload.ErrorMessage = FText::FromString("IPC could not connect after trying both endpoint paths.");
	Payload.ErrorLocation = FText::FromString("MobiusIpcClient");
	UMobiusUserFeedbackSubsystem::ReportErrorFromAnyThread(TWeakObjectPtr<UObject>(), Payload);
    UE_LOG(LogTemp, Error, TEXT("IPC: Unable to connect to socket after trying %s and %s"), *CanonicalPath, *SockPath);
    return false;
#endif
}

void FMobiusIpcClient::Platform_Disconnect()
{
#if PLATFORM_WINDOWS
    if (PipeHandle)
    {
        CloseHandle(PipeHandle);
        PipeHandle = nullptr;
    }
#elif PLATFORM_MAC
    if (SocketFd >= 0)
    {
        ::close(SocketFd);
        SocketFd = -1;
    }
#endif
}

bool FMobiusIpcClient::Platform_BlockingRead(uint8* Dst, int32 N)
{
#if PLATFORM_WINDOWS
    if (!PipeHandle) return false;
    DWORD Read = 0;
    return ReadFile(PipeHandle, Dst, (DWORD)N, &Read, nullptr) && Read == (DWORD)N;
#elif PLATFORM_MAC
    uint8* P = Dst;
    int32 Left = N;
    while (Left > 0)
    {
        const ssize_t R = ::recv(SocketFd, P, Left, MSG_WAITALL);
        if (R <= 0) return false;
        P    += R;
        Left -= (int32)R;
    }
    return true;
#endif
}

bool FMobiusIpcClient::Platform_BlockingWrite(const uint8* Src, int32 N)
{
#if PLATFORM_WINDOWS
    if (!PipeHandle) return false;
    DWORD Written = 0;
    return WriteFile(PipeHandle, Src, (DWORD)N, &Written, nullptr) && Written == (DWORD)N;
#elif PLATFORM_MAC
    const uint8* P = Src;
    int32 Left = N;
    while (Left > 0)
    {
        const ssize_t W = ::send(SocketFd, P, Left, 0);
        if (W <= 0) return false;
        P    += W;
        Left -= (int32)W;
    }
    return true;
#endif
}
