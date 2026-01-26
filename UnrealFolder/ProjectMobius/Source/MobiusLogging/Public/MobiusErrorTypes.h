// Shared error types for the Mobius logging/feedback system.
// This module exists to break circular dependencies between MobiusCore and other modules.

#pragma once

#include "CoreMinimal.h"
#include "MobiusErrorTypes.generated.h"

/**
 * Severity level for error messages.
 */
UENUM(BlueprintType)
enum class EMobiusErrorSeverity : uint8
{
	Info = 0,
	Warning = 1,
	Error = 2,
	Fatal = 3
};

/**
 * Commands for controlling the log window UI.
 */
UENUM(BlueprintType)
enum class EMobiusLogWindowCommand : uint8
{
	Open = 0,
	Close = 1,
	Enable = 2,
	Disable = 3
};

/**
 * Structured error message for the feedback system.
 */
USTRUCT(BlueprintType)
struct MOBIUSLOGGING_API FMobiusErrorMessage
{
	GENERATED_BODY()

	FMobiusErrorMessage();
	~FMobiusErrorMessage();
	FMobiusErrorMessage(const FMobiusErrorMessage& Other);
	FMobiusErrorMessage& operator=(const FMobiusErrorMessage& Other);
	FMobiusErrorMessage(FMobiusErrorMessage&& Other) noexcept;
	FMobiusErrorMessage& operator=(FMobiusErrorMessage&& Other) noexcept;

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

// Delegates for error/log broadcasting
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMobiusErrorReported, const FMobiusErrorMessage&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMobiusLogLine, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMobiusLogWindowCommand, EMobiusLogWindowCommand);
