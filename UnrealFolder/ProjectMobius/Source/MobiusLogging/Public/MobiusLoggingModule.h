// MobiusLogging module interface.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FMobiusLoggingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
