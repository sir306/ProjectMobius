// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Components/FlowCounterListRow.h"

#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/Theme/UIThemeSubsystem.h"

UUIThemeSubsystem* UFlowCounterListRow::ResolveThemeSubsystem() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				return Theme;
			}
		}
	}
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UGameInstance* GameInstance = World->GetGameInstance())
				{
					if (UUIThemeSubsystem* Theme = GameInstance->GetSubsystem<UUIThemeSubsystem>())
					{
						return Theme;
					}
				}
			}
		}
	}
	return nullptr;
}

void UFlowCounterListRow::NativeConstruct()
{
	// Seed the selection cache BEFORE Super: the themed base applies the theme from inside its
	// NativeConstruct, and that path reads bSelectedCached. Bind widgets are already resolved here.
	if (SelectedFlowCounter_ChkBox)
	{
		SelectedFlowCounter_ChkBox->OnCheckStateChanged.AddUniqueDynamic(this, &UFlowCounterListRow::HandleSelectChanged);
		bSelectedCached = SelectedFlowCounter_ChkBox->IsChecked();
	}

	// A6b-5: the OnThemeChanged bind that used to be here is the base's job now.
	Super::NativeConstruct();

	// Kept unconditional: the base only calls ApplyMobiusTheme when it FOUND a subsystem, and the row
	// still has to render its selected/idle state when there is none (idempotent if it already ran).
	ApplyRowVisual();
}

void UFlowCounterListRow::NativeDestruct()
{
	if (SelectedFlowCounter_ChkBox)
	{
		SelectedFlowCounter_ChkBox->OnCheckStateChanged.RemoveDynamic(this, &UFlowCounterListRow::HandleSelectChanged);
	}
	Super::NativeDestruct();
}

void UFlowCounterListRow::SetRowSelected(const bool bSelected)
{
	bSelectedCached = bSelected;
	ApplyRowVisual();
}

void UFlowCounterListRow::ApplyMobiusTheme_Implementation()
{
	ApplyRowVisual();
}

void UFlowCounterListRow::HandleSelectChanged(const bool bIsChecked)
{
	bSelectedCached = bIsChecked;
	ApplyRowVisual();
}

void UFlowCounterListRow::ApplyRowVisual()
{
	using namespace MobiusThemePalette;
	// The base's cached lookup first, then this class's own resolver. The fallback is kept deliberately: it
	// also sweeps GEngine's world contexts, which the base's GetGameInstance()-only path does not, and this
	// row is spawned into a list rather than constructed with the panel.
	const UUIThemeSubsystem* Theme = GetThemeSubsystem();
	if (!Theme)
	{
		Theme = ResolveThemeSubsystem();
	}
	const bool bLight = !Theme || Theme->GetTheme() == EMobiusUITheme::Light;

	// Selected = accent fill + white text; idle = transparent fill + primary (InputText) text.
	const FLinearColor Accent = Color(EMobiusPaletteRole::Accent, bLight);
	const FLinearColor Fill = bSelectedCached ? Accent : FLinearColor(0.f, 0.f, 0.f, 0.f);
	const FLinearColor TextCol = bSelectedCached ? FLinearColor::White : Color(EMobiusPaletteRole::InputText, bLight);

	if (Image_48)
	{
		Image_48->SetBrushTintColor(FSlateColor(Fill));
	}
	if (FC_NameTextBlock)
	{
		FC_NameTextBlock->SetColorAndOpacity(FSlateColor(TextCol));
	}
	if (SectionCountText)
	{
		SectionCountText->SetColorAndOpacity(FSlateColor(TextCol));
	}
}
