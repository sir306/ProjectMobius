// MobiusErrorTypes implementation - provides exported symbols for FMobiusErrorMessage

#include "MobiusErrorTypes.h"

FMobiusErrorMessage::FMobiusErrorMessage() = default;

FMobiusErrorMessage::~FMobiusErrorMessage() = default;

FMobiusErrorMessage::FMobiusErrorMessage(const FMobiusErrorMessage& Other) = default;

FMobiusErrorMessage& FMobiusErrorMessage::operator=(const FMobiusErrorMessage& Other) = default;

FMobiusErrorMessage::FMobiusErrorMessage(FMobiusErrorMessage&& Other) noexcept = default;

FMobiusErrorMessage& FMobiusErrorMessage::operator=(FMobiusErrorMessage&& Other) noexcept = default;
