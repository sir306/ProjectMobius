// ReversibleUniformGridPanel.h

#pragma once
#include "CoreMinimal.h"
#include "Components/UniformGridPanel.h"
#include "ReversibleUniformGridPanel.generated.h"

UCLASS()
class MOBIUSWIDGETS_API UReversibleUniformGridPanel : public UUniformGridPanel
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reversible Grid")
	bool bReverseOrder = false;

	UFUNCTION(BlueprintCallable, Category="Reversible Grid")
	void SetReverseOrder(bool bInReverse);

	// UWidget overrides
	virtual void SynchronizeProperties() override;
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Defer a one-shot reflow to next tick
	void RequestReflow();
	void ReflowAllRowsDenseAndMaybeReverse();
	void GetSlotsInRow(int32 Row, TArray<class UUniformGridSlot*>& Out) const;

	bool bReflowScheduled = false; // guard so we don't schedule multiple tickers
};
