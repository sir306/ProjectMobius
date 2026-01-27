#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FHdf5_DataImporterModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
