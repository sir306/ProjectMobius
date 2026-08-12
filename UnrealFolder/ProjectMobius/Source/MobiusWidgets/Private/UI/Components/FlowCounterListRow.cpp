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

FLinearColor UFlowCounterListRow::GetRowHighlightColour()
{
	using namespace MobiusThemePalette;

	const UUIThemeSubsystem* Theme = GetThemeSubsystem();
	if (!Theme)
	{
		Theme = ResolveThemeSubsystem();
	}
	const bool bLight = !Theme || Theme->GetTheme() == EMobiusUITheme::Light;

	// Read the CHECKBOX, never the cached flag: a programmatic SetIsChecked does not broadcast, so the cache
	// can be stale by exactly one selection change — which is the stale-highlight bug this replaces.
	const bool bSelected = SelectedFlowCounter_ChkBox
		? SelectedFlowCounter_ChkBox->IsChecked()
		: bSelectedCached;

	if (bSelected)
	{
		return Color(EMobiusPaletteRole::ListSelectedBg, bLight);
	}
	// Hover is the same role the dropdown rows use, so a list row and a combo row feel identical.
	if (IsHovered())
	{
		return Color(EMobiusPaletteRole::HoverBg, bLight);
	}
	return FLinearColor(0.f, 0.f, 0.f, 0.f);
}

void UFlowCounterListRow::RefreshSelectionVisual()
{
	// ApplyRowVisual now reads the checkbox itself, so this is simply "repaint from live state".
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

	// Take selection from the CHECKBOX, not the cached flag, whenever the checkbox exists.
	//
	// 2026-08-10: a previously-selected row kept its highlight after selection moved elsewhere. UMG's
	// UCheckBox::SetIsChecked does NOT broadcast OnCheckStateChanged, so when the container unticks this row
	// programmatically HandleSelectChanged never fires and bSelectedCached stays true — the row goes on
	// painting itself selected while its own tick box reads unchecked. Reading the live state makes the two
	// impossible to disagree; the cache stays only as the pre-construct fallback, since bind widgets are not
	// resolved yet when the selection is seeded before Super::NativeConstruct.
	if (SelectedFlowCounter_ChkBox)
	{
		bSelectedCached = SelectedFlowCounter_ChkBox->IsChecked();
	}

	// Text is InputText in BOTH states. The FILL is not computed here at all — it is bound, see
	// GetRowHighlightColour.
	//
	// 2026-08-10 (owner): the fill was Accent blue + hardcoded white text, which stayed blue in BOTH themes
	// and left this list disagreeing with the dropdowns once S2 gave those a grey selection. Selection is now
	// one role project-wide. The white text went with the blue — it existed only because white was the
	// legible choice ON blue; on ListSelectedBg it would be wrong in light theme (white on #c8c8c8 is about
	// 1.3:1), the same trap that forced SelectedTextColor off white in StyleComboBoxForBuild.
	const FLinearColor TextCol = Color(EMobiusPaletteRole::InputText, bLight);

	if (Image_48)
	{
		// The highlight itself is BOUND (see GetRowHighlightColour), so all this has to do is make sure the
		// brush is a neutral multiplier — the WBP authors it flat BLACK, and black x anything stays black.
		// Binding ColorAndOpacity and leaving the authored tint in place would render every row invisible.
		Image_48->SetBrushTintColor(FSlateColor(FLinearColor::White));
		if (!Image_48->ColorAndOpacityDelegate.IsBound())
		{
			Image_48->ColorAndOpacityDelegate.BindDynamic(this, &UFlowCounterListRow::GetRowHighlightColour);

			// SynchronizeProperties is what actually pushes a UMG property binding down to Slate: UImage sets
			// its Slate attribute from PROPERTY_BINDING there, and nowhere else. Binding after that has run
			// leaves Slate holding the AUTHORED static value — which, now that the brush tint is forced to
			// white, renders the row opaque WHITE instead of transparent.
			//
			// It only showed on newly ADDED rows because existing ones happened to get re-synced later by a
			// theme toggle. Do NOT "fix" this with SetColorAndOpacity(): that assigns a static value and
			// replaces the bound attribute, silently undoing the binding until the next sync.
			Image_48->SynchronizeProperties();
		}
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
