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
	Super::NativeConstruct();

	if (SelectedFlowCounter_ChkBox)
	{
		SelectedFlowCounter_ChkBox->OnCheckStateChanged.AddUniqueDynamic(this, &UFlowCounterListRow::HandleSelectChanged);
		bSelectedCached = SelectedFlowCounter_ChkBox->IsChecked();
	}

	if (UUIThemeSubsystem* Theme = ResolveThemeSubsystem())
	{
		ThemeSubsystem = Theme;
		Theme->OnThemeChanged.AddUniqueDynamic(this, &UFlowCounterListRow::HandleThemeChanged);
		bThemeBound = true;
	}

	ApplyRowVisual();
}

void UFlowCounterListRow::NativeDestruct()
{
	if (bThemeBound && ThemeSubsystem.IsValid())
	{
		ThemeSubsystem->OnThemeChanged.RemoveDynamic(this, &UFlowCounterListRow::HandleThemeChanged);
	}
	bThemeBound = false;
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

void UFlowCounterListRow::HandleThemeChanged()
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
	const UUIThemeSubsystem* Theme = ThemeSubsystem.IsValid() ? ThemeSubsystem.Get() : ResolveThemeSubsystem();
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
