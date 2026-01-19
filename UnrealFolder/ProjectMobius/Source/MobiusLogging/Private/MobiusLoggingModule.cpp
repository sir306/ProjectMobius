// MobiusLogging module implementation.

#include "MobiusLoggingModule.h"
#include "Modules/ModuleManager.h"

void FMobiusLoggingModule::StartupModule()
{
	// Module startup - nothing special needed for this lightweight module
}

void FMobiusLoggingModule::ShutdownModule()
{
	// Module shutdown
}

IMPLEMENT_MODULE(FMobiusLoggingModule, MobiusLogging)
