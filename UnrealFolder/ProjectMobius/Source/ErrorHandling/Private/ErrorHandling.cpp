#include "ErrorHandling.h"

DEFINE_LOG_CATEGORY(ErrorHandling);

#define LOCTEXT_NAMESPACE "FErrorHandling"

void FErrorHandling::StartupModule()
{
	UE_LOG(ErrorHandling, Warning, TEXT("ErrorHandling module has been loaded"));
}

void FErrorHandling::ShutdownModule()
{
	UE_LOG(ErrorHandling, Warning, TEXT("ErrorHandling module has been unloaded"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FErrorHandling, ErrorHandling)