// Central error/log routing without a hard dependency on UI modules.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MobiusUserFeedbackSubsystem.generated.h"

UENUM(BlueprintType)
enum class EMobiusErrorSeverity : uint8
{
	Info = 0,
	Warning = 1,
	Error = 2,
	Fatal = 3
};

UENUM(BlueprintType)
enum class EMobiusLogWindowCommand : uint8
{
	Open = 0,
	Close = 1,
	Enable = 2,
	Disable = 3
};

USTRUCT(BlueprintType)
struct FMobiusErrorMessage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Error")
	FText TitleBarText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Error")
	FText ErrorTitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Error")
	FText ErrorMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Error")
	FText ErrorLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Error")
	EMobiusErrorSeverity Severity = EMobiusErrorSeverity::Error;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mobius|Error")
	bool bShowPrompt = true;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMobiusErrorReported, const FMobiusErrorMessage&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMobiusLogLine, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMobiusLogWindowCommand, EMobiusLogWindowCommand);

/**
 * Routes errors and log lines from core modules to optional UI listeners.
 * Keeps prompts throttled and avoids hard dependencies on MobiusWidgets.
 */
UCLASS()
class MOBIUSCORE_API UMobiusUserFeedbackSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UMobiusUserFeedbackSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Convenience accessor for a world context object. */
	static UMobiusUserFeedbackSubsystem* Get(const UObject* WorldContextObject);

	/** Report an error/warning/info message (safe to call from any thread). */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Error", meta = (AdvancedDisplay = "ErrorLocation,Severity,bShowPrompt"))
	void ReportError(const FText& TitleBarText, const FText& ErrorTitle, const FText& ErrorMessage,
		const FText& ErrorLocation = FText::GetEmpty(),
		EMobiusErrorSeverity Severity = EMobiusErrorSeverity::Error,
		bool bShowPrompt = true);

	/** Report a fully formed error payload from any thread. */
	static void ReportErrorFromAnyThread(TWeakObjectPtr<UObject> WorldContextObject, const FMobiusErrorMessage& Message);

	/** Toggle whether error prompts should be surfaced in UI. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Error")
	void SetErrorPromptsEnabled(bool bEnabled);

	/** Returns true if UI error prompts are enabled. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Error")
	bool AreErrorPromptsEnabled() const;

	/** Request the log window to open. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logs")
	void RequestLogWindowOpen();

	/** Request the log window to close. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logs")
	void RequestLogWindowClose();

	/** Enable or disable the log window entirely. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logs")
	void SetLogWindowEnabled(bool bEnabled);

	/** True if the log window is enabled. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logs")
	bool IsLogWindowEnabled() const;

	/** True if the log window is currently requested open. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Logs")
	bool IsLogWindowOpen() const;

	/** Drain queued errors that occurred before UI listeners were ready. */
	TArray<FMobiusErrorMessage> DrainPendingErrors();

	/** Get cached log lines for initial UI population. */
	TArray<FString> GetCachedLogLines() const;

	/** Native delegates for UI listeners. */
	FOnMobiusErrorReported& OnErrorReported();
	FOnMobiusLogLine& OnLogLine();
	FOnMobiusLogWindowCommand& OnLogWindowCommand();

private:
	void ReportErrorInternal(const FMobiusErrorMessage& Message);
	void HandleLogLine(const FString& Line);
	void BroadcastLogWindowCommand(EMobiusLogWindowCommand Command);
	bool ShouldPrompt(const FMobiusErrorMessage& Message);
	uint32 HashMessage(const FMobiusErrorMessage& Message) const;
	void EnqueuePendingError(const FMobiusErrorMessage& Message);

	bool bErrorPromptsEnabled = true;
	float PromptCooldownSeconds = 2.0f;
	int32 MaxPendingErrors = 32;
	int32 MaxCachedLogLines = 1000;
	bool bLogWindowEnabled = true;
	bool bLogWindowOpen = false;

	TArray<FMobiusErrorMessage> PendingErrors;
	TArray<FString> CachedLogLines;
	TMap<uint32, double> RecentPromptTimes;

	FDelegateHandle LogLineHandle;
	FOnMobiusErrorReported ErrorReportedDelegate;
	FOnMobiusLogLine LogLineDelegate;
	FOnMobiusLogWindowCommand LogWindowCommandDelegate;
};
