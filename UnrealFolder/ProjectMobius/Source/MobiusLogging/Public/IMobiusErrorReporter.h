// Interface for error reporting that can be used without depending on MobiusCore.

#pragma once

#include "CoreMinimal.h"
#include "MobiusErrorTypes.h"

// Forward declare the getter function type
class IMobiusErrorReporter;
typedef IMobiusErrorReporter* (*FGetMobiusErrorReporterFunc)(const UObject*);

/**
 * Abstract interface for error reporting.
 * Implemented by UMobiusUserFeedbackSubsystem in MobiusCore.
 * Other modules can depend on MobiusLogging and use this interface
 * without creating circular dependencies.
 */
class MOBIUSLOGGING_API IMobiusErrorReporter
{
public:
	virtual ~IMobiusErrorReporter() = default;

	/**
	 * Report an error/warning/info message.
	 * Safe to call from any thread (implementation handles thread marshaling).
	 */
	virtual void ReportError(
		const FText& TitleBarText,
		const FText& ErrorTitle,
		const FText& ErrorMessage,
		const FText& ErrorLocation = FText::GetEmpty(),
		EMobiusErrorSeverity Severity = EMobiusErrorSeverity::Error,
		bool bShowPrompt = true) = 0;

	/**
	 * Get the error reporter instance for a world context.
	 * Returns nullptr if no reporter is available or if MobiusCore hasn't registered its getter.
	 */
	static IMobiusErrorReporter* Get(const UObject* WorldContextObject);

	/**
	 * Register the getter function. Called by MobiusCore during module startup.
	 * This allows the logging module to remain independent of MobiusCore.
	 */
	static void RegisterGetterFunc(FGetMobiusErrorReporterFunc InFunc);

private:
	static FGetMobiusErrorReporterFunc GetterFunc;
};
