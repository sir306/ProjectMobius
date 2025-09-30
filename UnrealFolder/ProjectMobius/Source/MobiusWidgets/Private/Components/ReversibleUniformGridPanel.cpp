// ReversibleUniformGridPanel.cpp


#include "Components/ReversibleUniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Containers/Ticker.h" // FTSTicker

void UReversibleUniformGridPanel::SetReverseOrder(bool bInReverse)
{
    if (bReverseOrder != bInReverse)
    {
        bReverseOrder = bInReverse;
        RequestReflow();
    }
}

void UReversibleUniformGridPanel::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    // After properties sync, normalize order on next frame
    RequestReflow();
}

void UReversibleUniformGridPanel::OnSlotAdded(UPanelSlot* InSlot)
{
    Super::OnSlotAdded(InSlot);
    // Blueprint often assigns Row/Column right after add → defer
    RequestReflow();
}

void UReversibleUniformGridPanel::OnSlotRemoved(UPanelSlot* InSlot)
{
    Super::OnSlotRemoved(InSlot);
    RequestReflow();
}

#if WITH_EDITOR
void UReversibleUniformGridPanel::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    static const FName NAME_bReverseOrder(TEXT("bReverseOrder"));
    if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == NAME_bReverseOrder)
    {
        RequestReflow();
    }
}
#endif

void UReversibleUniformGridPanel::RequestReflow()
{
    if (bReflowScheduled)
        return;

    bReflowScheduled = true;

    // Schedule a one-shot on the game thread for the next tick (0s delay)
    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateWeakLambda(this, [this](float)
        {
            bReflowScheduled = false;
            ReflowAllRowsDenseAndMaybeReverse();
            InvalidateLayoutAndVolatility();
            return false; // one-shot
        }),
        0.0f);
}

void UReversibleUniformGridPanel::GetSlotsInRow(int32 Row, TArray<UUniformGridSlot*>& Out) const
{
    Out.Reset();

    // IMPORTANT: iterate Slots[] in its natural order (chronological “add” order)
    for (UPanelSlot* PS : Slots)
    {
        if (UUniformGridSlot* GS = Cast<UUniformGridSlot>(PS))
        {
            if (GS->GetRow() == Row)
            {
                Out.Add(GS);
            }
        }
    }
}

void UReversibleUniformGridPanel::ReflowAllRowsDenseAndMaybeReverse()
{
    // 1) Collect distinct rows.
    TSet<int32> Rows;
    for (UPanelSlot* PS : Slots)
    {
        if (const UUniformGridSlot* GS = Cast<UUniformGridSlot>(PS))
        {
            Rows.Add(GS->GetRow());
        }
    }

    // 2) For each row, rebuild columns deterministically from panel add-order.
    for (int32 Row : Rows)
    {
        TArray<UUniformGridSlot*> RowSlots;
        GetSlotsInRow(Row, RowSlots);   // IMPORTANT: preserves Slots[] order (no sorting)

        const int32 N = RowSlots.Num();
        if (N == 0)
        {
            continue;
        }

        // ✅ Edge case: if only one widget remains in the row, force it back to Column 0.
        if (N == 1)
        {
            if (RowSlots[0]->GetColumn() != 0)
            {
                RowSlots[0]->SetColumn(0);
            }
            continue; // nothing else to do
        }

        // Dense-pack by panel order: 0..N-1
        for (int32 i = 0; i < N; ++i)
        {
            RowSlots[i]->SetColumn(i);
        }

        // Optional mirror: i -> (N-1 - i)
        if (bReverseOrder)
        {
            for (int32 i = 0; i < N; ++i)
            {
                RowSlots[i]->SetColumn(N - 1 - i);
            }
        }
    }
}
