// IMobiusErrorReporter implementation.

#include "IMobiusErrorReporter.h"

// Static member initialization
FGetMobiusErrorReporterFunc IMobiusErrorReporter::GetterFunc = nullptr;

IMobiusErrorReporter* IMobiusErrorReporter::Get(const UObject* WorldContextObject)
{
	if (GetterFunc)
	{
		return GetterFunc(WorldContextObject);
	}
	return nullptr;
}

void IMobiusErrorReporter::RegisterGetterFunc(FGetMobiusErrorReporterFunc InFunc)
{
	GetterFunc = InFunc;
}
