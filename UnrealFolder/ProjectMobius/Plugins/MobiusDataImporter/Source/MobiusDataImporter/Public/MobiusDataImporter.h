// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.

#pragma once

#include "Modules/ModuleManager.h"

class FMobiusDataImporterModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

};
