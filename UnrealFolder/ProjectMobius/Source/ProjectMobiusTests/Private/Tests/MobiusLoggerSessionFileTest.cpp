// Copyright (c) 2026 ProjectMobius contributors. Licensed under MIT.
//
// MobiusLoggerSessionFileTest.cpp
//
// UMobiusCustomLoggerSubsystem writes one file per session (process), MobiusCustomLog_<start time>.txt,
// instead of appending every session forever to one MobiusCustomLog.txt (115 MB on the dev machine,
// with side-by-side processes interleaved in it). Checks, against THIS process's logger:
//   - the file is per-session by name: MobiusCustomLog_yyyy.mm.dd-hh.mm.ss[_n].txt, never the old name;
//   - it starts with the "=== Mobius session started <time> (pid N) ===" line, whose time is the stamp
//     in the file name and whose pid is this process;
//   - it holds exactly one session - no other process has appended to it;
//   - no more session files are kept than the logger's retention cap.
//
// Runs anywhere the engine does (no world, no renderer), so it is in the default correctness suite.
// Run: MobiusPerf\RunTests.ps1 -Filter "ProjectMobius.Logger."
//
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Regex.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/MobiusCustomLoggerSubsystem.h"

namespace MobiusLoggerSessionFileTest
{
	/** MaxSessionLogFiles in MobiusCustomLoggerSubsystem.cpp (file-local). If that changes, so must this. */
	constexpr int32 MaxSessionLogFiles = 20;

	const TCHAR* const SessionHeaderMarker = TEXT("=== Mobius session started ");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMobiusLoggerSessionLogFilePerProcessTest,
	"ProjectMobius.Logger.SessionLogFilePerProcess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMobiusLoggerSessionLogFilePerProcessTest::RunTest(const FString& Parameters)
{
	using namespace MobiusLoggerSessionFileTest;

	UMobiusCustomLoggerSubsystem* Logger = GEngine ? GEngine->GetEngineSubsystem<UMobiusCustomLoggerSubsystem>() : nullptr;
	if (!Logger)
	{
		AddError(TEXT("UMobiusCustomLoggerSubsystem is missing"));
		return false;
	}

	const FString SessionLogPath = Logger->GetLogFilePath();
	const FString SessionLogName = FPaths::GetCleanFilename(SessionLogPath);
	AddInfo(FString::Printf(TEXT("this session's log: %s"), *SessionLogPath));

	TestNotEqual(TEXT("not the old shared file"), SessionLogName, FString(TEXT("MobiusCustomLog.txt")));

	const FRegexPattern NamePattern(TEXT("^MobiusCustomLog_(\\d{4})\\.(\\d{2})\\.(\\d{2})-(\\d{2})\\.(\\d{2})\\.(\\d{2})(_\\d+)?\\.txt$"));
	FRegexMatcher NameMatch(NamePattern, SessionLogName);
	if (!NameMatch.FindNext())
	{
		AddError(FString::Printf(TEXT("'%s' is not MobiusCustomLog_yyyy.mm.dd-hh.mm.ss[_n].txt"), *SessionLogName));
		return false;
	}
	// The header prints the same start time as "yyyy-mm-dd hh:mm:ss".
	const FString StampFromName = FString::Printf(TEXT("%s-%s-%s %s:%s:%s"),
		*NameMatch.GetCaptureGroup(1), *NameMatch.GetCaptureGroup(2), *NameMatch.GetCaptureGroup(3),
		*NameMatch.GetCaptureGroup(4), *NameMatch.GetCaptureGroup(5), *NameMatch.GetCaptureGroup(6));

	// Make sure everything queued so far (the header included) is on disk.
	Logger->FlushQueuedMessages();

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *SessionLogPath) || Lines.Num() == 0)
	{
		AddError(FString::Printf(TEXT("could not read any lines from %s"), *SessionLogPath));
		return false;
	}

	const FString ExpectedHeader = FString::Printf(TEXT("%s%s (pid %u) ==="),
		SessionHeaderMarker, *StampFromName, FPlatformProcess::GetCurrentProcessId());
	AddInfo(FString::Printf(TEXT("first line: %s"), *Lines[0]));
	TestTrue(FString::Printf(TEXT("first line is this session's start line, '%s'"), *ExpectedHeader),
		Lines[0].EndsWith(ExpectedHeader));

	int32 SessionHeaders = 0;
	for (const FString& Line : Lines)
	{
		if (Line.Contains(SessionHeaderMarker))
		{
			++SessionHeaders;
		}
	}
	TestEqual(TEXT("the file holds exactly one session"), SessionHeaders, 1);

	TArray<FString> SessionFiles;
	IFileManager::Get().FindFiles(SessionFiles, *FPaths::Combine(FPaths::GetPath(SessionLogPath), TEXT("MobiusCustomLog_*.txt")),
		/*Files=*/true, /*Directories=*/false);
	AddInfo(FString::Printf(TEXT("%d session log file(s) in %s"), SessionFiles.Num(), *FPaths::GetPath(SessionLogPath)));
	TestTrue(FString::Printf(TEXT("at most %d session files are kept"), MaxSessionLogFiles),
		SessionFiles.Num() <= MaxSessionLogFiles);

	return true;
}

#endif // !UE_BUILD_SHIPPING
