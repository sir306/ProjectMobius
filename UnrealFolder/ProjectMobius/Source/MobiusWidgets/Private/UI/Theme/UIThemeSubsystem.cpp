// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "UI/Theme/UIThemeSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Slate/SlateBrushAsset.h"
#include "UserConfig/UserProjectSettings.h"
#include "Style/MobiusStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "UI/Components/ButtonWithText.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobiusTheme, Log, All);

namespace MobiusTheme
{
	struct FColorPair
	{
		FLinearColor Dark;
		FLinearColor Light;
	};

	// Linear-space role pairs, dark (7a) <-> light (4b). Values must match the literals applied to
	// the widgets exactly (within Epsilon) or the walker will skip them.
	static const FColorPair SurfaceMap[] =
	{
		{ FLinearColor(0.0395f, 0.0395f, 0.0395f),  FLinearColor(0.9131f, 0.9131f, 0.9131f) }, // panel body   #383838 -> #f5f5f5
		{ FLinearColor(0.0284f, 0.0284f, 0.0284f),  FLinearColor(0.7913f, 0.7913f, 0.7913f) }, // tab strip    #2f2f2f -> #e6e6e6
		{ FLinearColor(0.052861f, 0.052861f, 0.052861f), FLinearColor(0.8228f, 0.8228f, 0.8228f) }, // header bar #414141 -> #eaeaea
		{ FLinearColor(0.0595f, 0.0595f, 0.0595f),  FLinearColor(0.7681f, 0.7681f, 0.7681f) }, // divider/chip #454545 -> #e3e3e3
		// Field bg is #fcfcfc, NOT pure white: pure white is the neutral multiplier on almost
		// every brush (the UBorder double-tint convention), and a 1.0 light entry would remap all
		// of those to #2b2b2b on the light->dark pass, blacking out the chrome.
		{ FLinearColor(0.0243f, 0.0243f, 0.0243f),  FLinearColor(0.973f, 0.973f, 0.973f) },    // field bg     #2b2b2b -> #fcfcfc
		{ FLinearColor(0.091f, 0.091f, 0.091f),     FLinearColor(0.1945f, 0.1945f, 0.1945f) }, // field line   #555555 -> #7a7a7a
		{ FLinearColor(0.1023f, 0.1023f, 0.1023f),  FLinearColor(0.4179f, 0.4179f, 0.4179f) }, // chip line    #5a5a5a -> #adadad
		{ FLinearColor(0.068f, 0.068f, 0.068f),     FLinearColor(0.6307f, 0.6307f, 0.6307f) }, // slider track #4a4a4a -> #d0d0d0
		{ FLinearColor(0.045f, 0.045f, 0.045f),     FLinearColor(0.9131f, 0.9131f, 0.9131f) }, // active tab   #3c3c3c -> #f5f5f5
		{ FLinearColor(0.010f, 0.012f, 0.015f),     FLinearColor(0.973f, 0.973f, 0.973f) },    // legacy field / combo bg #1a1c20 -> #fcfcfc
		{ FLinearColor(0.007f, 0.007f, 0.009f),     FLinearColor(0.7913f, 0.7913f, 0.7913f) }, // bottom bar   -> #e6e6e6
		{ FLinearColor(0.172f, 0.172f, 0.172f),     FLinearColor(0.4179f, 0.4179f, 0.4179f) }, // icon border  -> #adadad
		{ FLinearColor(0.132f, 0.132f, 0.132f),     FLinearColor(0.4179f, 0.4179f, 0.4179f) }, // box outline  #666666 -> #adadad
		{ FLinearColor(0.100f, 0.330f, 0.661f),     FLinearColor(0.0f, 0.1356f, 0.5271f) },    // accent       #5a9bd5 -> #0067c0
		{ FLinearColor(0.135f, 0.405f, 0.750f),     FLinearColor(0.0f, 0.1800f, 0.6200f) },    // accent hover
		{ FLinearColor(0.070f, 0.240f, 0.500f),     FLinearColor(0.0f, 0.1000f, 0.4200f) },    // accent press
	};

	static const FColorPair TextMap[] =
	{
		{ FLinearColor(1.0f, 1.0f, 1.0f),                    FLinearColor(0.0f, 0.0f, 0.0f) },          // playbar text  -> #000000
		{ FLinearColor(0.462077f, 0.462077f, 0.462077f),     FLinearColor(0.0578f, 0.0578f, 0.0578f) }, // header text   #b5b5b5 -> #444444
		{ FLinearColor(0.323f, 0.323f, 0.323f),              FLinearColor(0.1329f, 0.1329f, 0.1329f) }, // dim text      #9a9a9a -> #666666
		{ FLinearColor(0.323143f, 0.351533f, 0.391572f),     FLinearColor(0.1329f, 0.1329f, 0.1329f) }, // dim text (bluish variant)
		{ FLinearColor(0.625f, 0.625f, 0.625f),              FLinearColor(0.0160f, 0.0160f, 0.0160f) }, // primary text  #cfcfcf -> #222222
		{ FLinearColor(0.745f, 0.745f, 0.745f),              FLinearColor(0.0331f, 0.0331f, 0.0331f) }, // chip/button   #e0e0e0 -> #333333
		{ FLinearColor(0.6867f, 0.7084f, 0.7454f),           FLinearColor(0.0160f, 0.0160f, 0.0160f) }, // legacy body text
		{ FLinearColor(0.925f, 0.933f, 0.945f),              FLinearColor(0.0160f, 0.0160f, 0.0160f) }, // bright mono   #e6e8eb -> #222222
	};

	static bool NearlyEqual(const FLinearColor& A, const FLinearColor& B)
	{
		constexpr float Epsilon = 0.012f;
		return FMath::Abs(A.R - B.R) < Epsilon
			&& FMath::Abs(A.G - B.G) < Epsilon
			&& FMath::Abs(A.B - B.B) < Epsilon;
	}

	static bool Remap(FLinearColor& InOut, const bool bLight, const TArrayView<const FColorPair> Map,
	                  const bool bGuardNeutralWhite = true)
	{
		// Pure white is the neutral multiplier on brushes/brush-colors project-wide (the UBorder
		// double-tint convention) — never a surface role. Text maps opt out: white text IS a role.
		if (bGuardNeutralWhite && InOut.R > 0.99f && InOut.G > 0.99f && InOut.B > 0.99f)
		{
			return false;
		}
		for (const FColorPair& Pair : Map)
		{
			const FLinearColor& From = bLight ? Pair.Dark : Pair.Light;
			const FLinearColor& To = bLight ? Pair.Light : Pair.Dark;
			if (NearlyEqual(InOut, From))
			{
				const float Alpha = InOut.A;
				InOut = To;
				InOut.A = Alpha;
				return true;
			}
		}
		return false;
	}

	static bool RemapSlate(FSlateColor& InOut, const bool bLight, const TArrayView<const FColorPair> Map,
	                       const bool bGuardNeutralWhite = true)
	{
		if (!InOut.IsColorSpecified())
		{
			return false;
		}
		FLinearColor Color = InOut.GetSpecifiedColor();
		if (Remap(Color, bLight, Map, bGuardNeutralWhite))
		{
			InOut = FSlateColor(Color);
			return true;
		}
		return false;
	}

	/** Tints, outlines and the DarkTheme<->LightTheme chrome material swap for one brush. */
	static bool RemapBrush(FSlateBrush& Brush, const bool bLight)
	{
		bool bChanged = false;
		FSlateColor Tint = Brush.TintColor;
		if (RemapSlate(Tint, bLight, SurfaceMap))
		{
			Brush.TintColor = Tint;
			bChanged = true;
		}
		FSlateColor Outline = Brush.OutlineSettings.Color;
		if (RemapSlate(Outline, bLight, SurfaceMap))
		{
			Brush.OutlineSettings.Color = Outline;
			bChanged = true;
		}
		if (const UMaterialInterface* Material = Cast<UMaterialInterface>(Brush.GetResourceObject()))
		{
			const TCHAR* From = bLight ? TEXT("/Master/Instances/DarkTheme/") : TEXT("/Master/Instances/LightTheme/");
			const TCHAR* To = bLight ? TEXT("/Master/Instances/LightTheme/") : TEXT("/Master/Instances/DarkTheme/");
			FString Path = Material->GetPathName();
			if (Path.Contains(From))
			{
				Path.ReplaceInline(From, To);
				if (UMaterialInterface* Swapped = Cast<UMaterialInterface>(FSoftObjectPath(Path).TryLoad()))
				{
					Brush.SetResourceObject(Swapped);
					bChanged = true;
				}
			}
		}
		return bChanged;
	}

	/** Bottom-bar icon materials expose glyph/background/border params — theme via a dynamic instance. */
	static bool ThemeIconBrush(FSlateBrush& Brush, UObject* MidOuter, const bool bLight)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(Brush.GetResourceObject());
		if (!Material)
		{
			return false;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Material);
		const FString SourcePath = Mid ? (Mid->Parent ? Mid->Parent->GetPathName() : FString()) : Material->GetPathName();
		if (!SourcePath.Contains(TEXT("MobiusBottomBarIconMats")))
		{
			return false;
		}
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(Material, MidOuter);
			Brush.SetResourceObject(Mid);
		}
		// Reset all overrides first — only the three COLOUR params are themed. Other params
		// ("Inset Inner Button Texture", "TextureSize", ...) are geometry and must stay at the
		// parent instance's values.
		Mid->ClearParameterValues();
		Mid->SetVectorParameterValue(TEXT("Texture Colour"), bLight ? FLinearColor(0.016f, 0.016f, 0.016f) : FLinearColor::White);
		Mid->SetVectorParameterValue(TEXT("BackgroundColour"), bLight ? FLinearColor(0.9131f, 0.9131f, 0.9131f) : FLinearColor(0.007f, 0.007f, 0.009f));
		Mid->SetVectorParameterValue(TEXT("BorderColour"), bLight ? FLinearColor(0.4179f, 0.4179f, 0.4179f) : FLinearColor(0.172f, 0.172f, 0.172f));
		return true;
	}

	/**
	 * Panel/popup/bar backgrounds all instance M_WidgetBackground (BackgroundMaterials folder):
	 * cog popup (MI_PlayBarBackground), flow-counter/floor-stats header bars, loading + egress
	 * panels. Light: retint via a dynamic instance, preserving each param's authored ALPHA (some
	 * variants are deliberately transparent). Dark: clearing the overrides restores the parent
	 * instance's authored values exactly.
	 */
	static bool ThemeBackgroundBrush(FSlateBrush& Brush, UObject* MidOuter, const bool bLight)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(Brush.GetResourceObject());
		if (!Material)
		{
			return false;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Material);
		const FString SourcePath = Mid ? (Mid->Parent ? Mid->Parent->GetPathName() : FString()) : Material->GetPathName();
		if (!SourcePath.Contains(TEXT("/WidgetMaterials/BackgroundMaterials/")))
		{
			return false;
		}
		if (!Mid)
		{
			Mid = UMaterialInstanceDynamic::Create(Material, MidOuter);
			Brush.SetResourceObject(Mid);
		}
		if (bLight)
		{
			// Override ONLY the two colour params — clearing everything here would also drop the
			// parent's scalar overrides (opacity, corner radius) and popups turn translucent.
			FLinearColor Background = FLinearColor::Black;
			FLinearColor BorderTint = FLinearColor::White;
			Mid->Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Background Color Tint")), Background);
			Mid->Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Border Color Tint")), BorderTint);
			Mid->SetVectorParameterValue(TEXT("Background Color Tint"), FLinearColor(0.9131f, 0.9131f, 0.9131f, Background.A));
			Mid->SetVectorParameterValue(TEXT("Border Color Tint"), FLinearColor(0.4179f, 0.4179f, 0.4179f, BorderTint.A));
		}
		else
		{
			// Dropping the overrides restores the parent instance's authored values exactly.
			Mid->ClearParameterValues();
		}
		return true;
	}
}

void UUIThemeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// LIGHT is the product default; the saved choice in UUserProjectSettings (the project's
	// GameUserSettings class) overrides it. Widgets are not constructed yet —
	// UThemeToggleWidget::NativeConstruct triggers the deferred ReapplyTheme() that actually
	// paints a non-dark theme onto the UI.
	const UUserProjectSettings* Settings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
	CurrentTheme = (!Settings || Settings->GetUseLightUITheme()) ? EMobiusUITheme::Light : EMobiusUITheme::Dark;

	// Retint the SHARED styles before any widget constructs: several Slate widgets (ButtonWithText
	// labels) copy their style at construction, so the first paint must already be themed.
	ApplySharedStyles(CurrentTheme == EMobiusUITheme::Light);
}

void UUIThemeSubsystem::SetTheme(const EMobiusUITheme NewTheme)
{
	CurrentTheme = NewTheme;
	ApplyTheme(CurrentTheme == EMobiusUITheme::Light);

	// Persist through the project's GameUserSettings so the choice survives sessions alongside
	// the other user preferences (logger flags, render tier, UI scale).
	if (UUserProjectSettings* Settings = Cast<UUserProjectSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr))
	{
		Settings->SetUseLightUITheme(CurrentTheme == EMobiusUITheme::Light);
	}
}

void UUIThemeSubsystem::ToggleTheme()
{
	SetTheme(CurrentTheme == EMobiusUITheme::Dark ? EMobiusUITheme::Light : EMobiusUITheme::Dark);
}

void UUIThemeSubsystem::ReapplyTheme()
{
	ApplyTheme(CurrentTheme == EMobiusUITheme::Light);
}

void UUIThemeSubsystem::ApplyTheme(const bool bLight)
{
	ApplySharedStyles(bLight);
	ApplyToLiveWidgets(bLight);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(false);
	}
}

void UUIThemeSubsystem::ApplyToLiveWidgets(const bool bLight)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	// TopLevelOnly=false returns every live UUserWidget, embedded ones included, so each widget
	// only needs its OWN tree walked (embedded user widgets are skipped as tree nodes below).
	TArray<UUserWidget*> AllUserWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, AllUserWidgets, UUserWidget::StaticClass(), false);
	int32 WidgetsVisited = 0;
	for (UUserWidget* UserWidget : AllUserWidgets)
	{
		if (!UserWidget || !UserWidget->WidgetTree)
		{
			continue;
		}
		UserWidget->WidgetTree->ForEachWidget([this, bLight, &WidgetsVisited](UWidget* Widget)
		{
			if (Widget && !Widget->IsA<UUserWidget>())
			{
				ApplyToWidget(Widget, bLight);
				++WidgetsVisited;
			}
		});
	}
	UE_LOG(LogMobiusTheme, Display, TEXT("ApplyToLiveWidgets(%s): %d user widgets, %d leaf widgets visited"),
		bLight ? TEXT("light") : TEXT("dark"), AllUserWidgets.Num(), WidgetsVisited);
}

void UUIThemeSubsystem::ApplyToWidget(UWidget* Widget, const bool bLight)
{
	using namespace MobiusTheme;

	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		FLinearColor BrushColor = Border->GetBrushColor();
		if (Remap(BrushColor, bLight, SurfaceMap))
		{
			Border->SetBrushColor(BrushColor);
		}
		FSlateBrush Brush = Border->Background;
		bool bChanged = RemapBrush(Brush, bLight);
		bChanged |= ThemeBackgroundBrush(Brush, Border, bLight);
		if (bChanged)
		{
			Border->SetBrush(Brush);
		}
	}
	else if (UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		FSlateColor Color = Text->GetColorAndOpacity();
		if (RemapSlate(Color, bLight, TextMap, /*bGuardNeutralWhite*/ false))
		{
			Text->SetColorAndOpacity(Color);
		}
	}
	else if (USlider* Slider = Cast<USlider>(Widget))
	{
		FLinearColor Bar = Slider->GetSliderBarColor();
		if (Remap(Bar, bLight, SurfaceMap))
		{
			Slider->SetSliderBarColor(Bar);
		}
		FLinearColor Handle = Slider->GetSliderHandleColor();
		if (Remap(Handle, bLight, SurfaceMap))
		{
			Slider->SetSliderHandleColor(Handle);
		}
	}
	else if (UCheckBox* CheckBox = Cast<UCheckBox>(Widget))
	{
		FCheckBoxStyle Style = CheckBox->GetWidgetStyle();
		bool bChanged = false;
		FSlateBrush* Brushes[] =
		{
			&Style.UncheckedImage, &Style.UncheckedHoveredImage, &Style.UncheckedPressedImage,
			&Style.CheckedImage, &Style.CheckedHoveredImage, &Style.CheckedPressedImage,
			&Style.UndeterminedImage, &Style.UndeterminedHoveredImage, &Style.UndeterminedPressedImage,
		};
		for (FSlateBrush* Brush : Brushes)
		{
			bChanged |= RemapBrush(*Brush, bLight);
		}
		if (bChanged)
		{
			CheckBox->SetWidgetStyle(Style);
		}
	}
	else if (UButton* Button = Cast<UButton>(Widget))
	{
		FButtonStyle Style = Button->GetStyle();
		bool bChanged = false;
		FSlateBrush* Brushes[] = { &Style.Normal, &Style.Hovered, &Style.Pressed, &Style.Disabled };
		for (FSlateBrush* Brush : Brushes)
		{
			bChanged |= RemapBrush(*Brush, bLight);
			bChanged |= ThemeIconBrush(*Brush, Button, bLight);
			bChanged |= ThemeBackgroundBrush(*Brush, Button, bLight);
		}
		bChanged |= RemapSlate(Style.NormalForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.HoveredForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.PressedForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		if (bChanged)
		{
			Button->SetStyle(Style);
		}
		// Some buttons (floor-stat bars et al) get their colour from UButton::BackgroundColor,
		// which MULTIPLIES the (white) style brushes — remap it too or they stay dark.
		FLinearColor ButtonBackground = Button->GetBackgroundColor();
		if (Remap(ButtonBackground, bLight, SurfaceMap))
		{
			Button->SetBackgroundColor(ButtonBackground);
		}
		// ButtonWithText labels bake their style at construction (STextBlock copies it) — re-push
		// so they pick up the retinted "Mobius.Text.Label" / SWS text styles.
		if (UButtonWithText* ButtonWithText = Cast<UButtonWithText>(Widget))
		{
			ButtonWithText->RefreshTextStyle();
		}
	}
	else if (UComboBoxString* ComboBox = Cast<UComboBoxString>(Widget))
	{
		FComboBoxStyle Style = ComboBox->GetWidgetStyle();
		bool bChanged = false;
		FSlateBrush* Brushes[] =
		{
			&Style.ComboButtonStyle.ButtonStyle.Normal, &Style.ComboButtonStyle.ButtonStyle.Hovered,
			&Style.ComboButtonStyle.ButtonStyle.Pressed, &Style.ComboButtonStyle.ButtonStyle.Disabled,
			&Style.ComboButtonStyle.MenuBorderBrush, &Style.ComboButtonStyle.DownArrowImage,
		};
		for (FSlateBrush* Brush : Brushes)
		{
			bChanged |= RemapBrush(*Brush, bLight);
			// The dark combo uses a pure-white 1px outline (which the neutral-white guard
			// protects) — swap it explicitly for a readable border on the light chrome.
			FSlateColor Outline = Brush->OutlineSettings.Color;
			if (Outline.IsColorSpecified())
			{
				const FLinearColor OutlineColor = Outline.GetSpecifiedColor();
				if (bLight && OutlineColor.R > 0.99f && OutlineColor.G > 0.99f && OutlineColor.B > 0.99f)
				{
					Brush->OutlineSettings.Color = FSlateColor(FLinearColor(0.1945f, 0.1945f, 0.1945f)); // #7a7a7a
					bChanged = true;
				}
				else if (!bLight && FMath::IsNearlyEqual(OutlineColor.R, 0.1945f, 0.012f)
					&& FMath::IsNearlyEqual(OutlineColor.G, 0.1945f, 0.012f)
					&& FMath::IsNearlyEqual(OutlineColor.B, 0.1945f, 0.012f)
					&& Brush != &Style.ComboButtonStyle.MenuBorderBrush)
				{
					Brush->OutlineSettings.Color = FSlateColor(FLinearColor::White);
					bChanged = true;
				}
			}
		}
		bChanged |= RemapSlate(Style.ComboButtonStyle.ButtonStyle.NormalForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.ComboButtonStyle.ButtonStyle.HoveredForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		bChanged |= RemapSlate(Style.ComboButtonStyle.ButtonStyle.PressedForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
		if (bChanged)
		{
			ComboBox->SetWidgetStyle(Style);
		}
		// Dropdown MENU rows + item text (FTableRowStyle) — explicit per theme; the authored values
		// are dark-only.
		{
			FTableRowStyle Items = ComboBox->GetItemStyle();
			const FLinearColor RowBg = bLight ? FLinearColor(0.964f, 0.964f, 0.964f) : FLinearColor(0.010f, 0.012f, 0.015f);
			const FLinearColor RowHover = bLight ? FLinearColor(0.7913f, 0.7913f, 0.7913f) : FLinearColor(0.068f, 0.068f, 0.068f);
			const FLinearColor RowSelected = bLight ? FLinearColor(0.0f, 0.1356f, 0.5271f) : FLinearColor(0.100f, 0.330f, 0.661f);
			auto SetRowBrush = [](FSlateBrush& InBrush, const FLinearColor& InColor)
			{
				InBrush.TintColor = FSlateColor(InColor);
				InBrush.DrawAs = ESlateBrushDrawType::Image;
				InBrush.SetResourceObject(nullptr);
			};
			SetRowBrush(Items.EvenRowBackgroundBrush, RowBg);
			SetRowBrush(Items.OddRowBackgroundBrush, RowBg);
			SetRowBrush(Items.EvenRowBackgroundHoveredBrush, RowHover);
			SetRowBrush(Items.OddRowBackgroundHoveredBrush, RowHover);
			SetRowBrush(Items.ActiveBrush, RowSelected);
			SetRowBrush(Items.ActiveHoveredBrush, RowSelected);
			SetRowBrush(Items.InactiveBrush, RowBg);
			SetRowBrush(Items.InactiveHoveredBrush, RowHover);
			Items.TextColor = FSlateColor(bLight ? FLinearColor(0.016f, 0.016f, 0.016f) : FLinearColor(0.625f, 0.625f, 0.625f));
			Items.SelectedTextColor = FSlateColor(FLinearColor::White);
			ComboBox->SetItemStyle(Items);

			FComboBoxStyle MenuStyle = ComboBox->GetWidgetStyle();
			MenuStyle.ComboButtonStyle.MenuBorderBrush.TintColor = FSlateColor(RowBg);
			ComboBox->SetWidgetStyle(MenuStyle);
		}
		// Themed item/content generator — the default one bakes construction-time colours.
		ComboBox->OnGenerateWidgetEvent.BindDynamic(this, &UUIThemeSubsystem::HandleGenerateThemedComboEntry);
	}
	else if (UImage* Image = Cast<UImage>(Widget))
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FSlateBrush Brush = Image->Brush;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		bool bChanged = RemapBrush(Brush, bLight);
		bChanged |= ThemeIconBrush(Brush, Image, bLight);
		bChanged |= ThemeBackgroundBrush(Brush, Image, bLight);
		if (bChanged)
		{
			Image->SetBrush(Brush);
		}
	}
}

void UUIThemeSubsystem::ApplySharedStyles(const bool bLight)
{
	using namespace MobiusTheme;

	// Ribbon tab style (inactive/hover/press states) — mutate the loaded asset's style struct in
	// place; live SButtons hold a pointer to it and repaint on the InvalidateAllWidgets that follows.
	if (const USlateWidgetStyleAsset* TabStyleAsset = LoadObject<USlateWidgetStyleAsset>(nullptr,
		TEXT("/Game/01_Dev/Widgets/WidgetMaterials/SlateStyleSheets/UI_Styles/SWS_SettingButtonStyle.SWS_SettingButtonStyle")))
	{
		if (FButtonStyle* TabStyle = const_cast<FButtonStyle*>(TabStyleAsset->GetStyle<FButtonStyle>()))
		{
			TabStyle->Normal.TintColor = bLight ? FLinearColor(0.7913f, 0.7913f, 0.7913f) : FLinearColor(0.0284f, 0.0284f, 0.0284f);
			TabStyle->Hovered.TintColor = bLight ? FLinearColor(0.8500f, 0.8500f, 0.8500f) : FLinearColor(0.0370f, 0.0370f, 0.0370f);
			TabStyle->Pressed.TintColor = bLight ? FLinearColor(0.9131f, 0.9131f, 0.9131f) : FLinearColor(0.0452f, 0.0452f, 0.0452f);
			// 7a/4b tab text: inactive dim, active/hover bright(er).
			TabStyle->NormalForeground = bLight ? FLinearColor(0.1329f, 0.1329f, 0.1329f) : FLinearColor(0.323f, 0.323f, 0.323f);   // #666666 / #9a9a9a
			TabStyle->HoveredForeground = bLight ? FLinearColor(0.0160f, 0.0160f, 0.0160f) : FLinearColor(0.8228f, 0.8228f, 0.8228f); // #222222 / #eaeaea
			TabStyle->PressedForeground = TabStyle->HoveredForeground;
		}
	}

	// Sweep every Slate style/brush ASSET under the widget folder. Several widgets (playbar
	// play/pause, gear button, hide/show bar) re-copy their style from these shared assets at
	// runtime, so theming the live widget copy alone is undone on the next state change —
	// the source assets must carry the theme.
	int32 StyleAssetsThemed = 0;
	const FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> StyleAssets;
	AssetRegistry.Get().GetAssetsByPath(TEXT("/Game/01_Dev/Widgets"), StyleAssets, true);
	for (const FAssetData& AssetData : StyleAssets)
	{
		if (AssetData.AssetClassPath == USlateWidgetStyleAsset::StaticClass()->GetClassPathName())
		{
			// The tab style is handled explicitly above with exact palette values.
			if (AssetData.AssetName == TEXT("SWS_SettingButtonStyle"))
			{
				continue;
			}
			USlateWidgetStyleAsset* StyleAsset = Cast<USlateWidgetStyleAsset>(AssetData.GetAsset());
			if (!StyleAsset)
			{
				continue;
			}
			bool bChanged = false;
			if (FButtonStyle* ButtonStyle = const_cast<FButtonStyle*>(StyleAsset->GetStyle<FButtonStyle>()))
			{
				FSlateBrush* Brushes[] = { &ButtonStyle->Normal, &ButtonStyle->Hovered, &ButtonStyle->Pressed, &ButtonStyle->Disabled };
				for (FSlateBrush* Brush : Brushes)
				{
					bChanged |= RemapBrush(*Brush, bLight);
					bChanged |= ThemeIconBrush(*Brush, StyleAsset, bLight);
					bChanged |= ThemeBackgroundBrush(*Brush, StyleAsset, bLight);
				}
				bChanged |= RemapSlate(ButtonStyle->NormalForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				bChanged |= RemapSlate(ButtonStyle->HoveredForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				bChanged |= RemapSlate(ButtonStyle->PressedForeground, bLight, TextMap, /*bGuardNeutralWhite*/ false);
				// Scalability/panel buttons: their dark fill shares the panel-body value so the
				// generic remap can't give them contrast — set the full style explicitly per theme.
				if (AssetData.AssetName == TEXT("SWS_PanelButtonStyle"))
				{
					ButtonStyle->Normal.TintColor = FSlateColor(bLight ? FLinearColor(0.964f, 0.964f, 0.964f) : FLinearColor(0.052861f, 0.052861f, 0.052861f));
					ButtonStyle->Hovered.TintColor = FSlateColor(bLight ? FLinearColor(0.8228f, 0.8228f, 0.8228f) : FLinearColor(0.068f, 0.068f, 0.068f));
					ButtonStyle->Pressed.TintColor = FSlateColor(bLight ? FLinearColor(0.7913f, 0.7913f, 0.7913f) : FLinearColor(0.0452f, 0.0452f, 0.0452f));
					const FLinearColor OutlineColor = bLight
						? FLinearColor(0.4179f, 0.4179f, 0.4179f)   // #adadad
						: FLinearColor(0.1023f, 0.1023f, 0.1023f);  // #5a5a5a
					for (FSlateBrush* Brush : Brushes)
					{
						Brush->OutlineSettings.Color = FSlateColor(OutlineColor);
						Brush->OutlineSettings.Width = 1.0f;
					}
					bChanged = true;
				}
				// The "current tier" chip: replace the baked black material with a flat rounded box
				// carrying an accent ring — readable in both themes with the shared dark/light labels.
				else if (AssetData.AssetName == TEXT("SWS_ScaleabilityButtonCurrentSet"))
				{
					const FLinearColor ChipFill = bLight ? FLinearColor(0.964f, 0.964f, 0.964f) : FLinearColor(0.0452f, 0.0452f, 0.0452f);
					const FLinearColor Accent = bLight ? FLinearColor(0.0f, 0.1356f, 0.5271f) : FLinearColor(0.100f, 0.330f, 0.661f);
					for (FSlateBrush* Brush : Brushes)
					{
						Brush->SetResourceObject(nullptr);
						Brush->DrawAs = ESlateBrushDrawType::RoundedBox;
						Brush->TintColor = FSlateColor(ChipFill);
						Brush->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
						Brush->OutlineSettings.CornerRadii = FVector4(2.0f, 2.0f, 2.0f, 2.0f);
						Brush->OutlineSettings.Color = FSlateColor(Accent);
						Brush->OutlineSettings.Width = 2.0f;
					}
					bChanged = true;
				}
			}
			// Button LABELS come from separate text-style assets (SWS_*TextStyle) — without this
			// the light theme leaves white labels on light buttons.
			else if (FTextBlockStyle* TextStyle = const_cast<FTextBlockStyle*>(StyleAsset->GetStyle<FTextBlockStyle>()))
			{
				bChanged |= RemapSlate(TextStyle->ColorAndOpacity, bLight, TextMap, /*bGuardNeutralWhite*/ false);
			}
			StyleAssetsThemed += bChanged ? 1 : 0;
		}
		else if (AssetData.AssetClassPath == USlateBrushAsset::StaticClass()->GetClassPathName())
		{
			if (USlateBrushAsset* BrushAsset = Cast<USlateBrushAsset>(AssetData.GetAsset()))
			{
				bool bChanged = RemapBrush(BrushAsset->Brush, bLight);
				bChanged |= ThemeIconBrush(BrushAsset->Brush, BrushAsset, bLight);
				bChanged |= ThemeBackgroundBrush(BrushAsset->Brush, BrushAsset, bLight);
				StyleAssetsThemed += bChanged ? 1 : 0;
			}
		}
	}
	UE_LOG(LogMobiusTheme, Display, TEXT("ApplySharedStyles(%s): %d style assets themed"),
		bLight ? TEXT("light") : TEXT("dark"), StyleAssetsThemed);

	// "Mobius.Button" (Browse et al) — 4b light buttons are white-ish with #adadad outline, #222 label.
	FButtonStyle& MobiusButton = const_cast<FButtonStyle&>(FMobiusStyle::Get().GetWidgetStyle<FButtonStyle>("Mobius.Button"));
	const FLinearColor Fill = bLight ? FLinearColor(0.9647f, 0.9647f, 0.9647f) : FLinearColor::FromSRGBColor(FColor(0x4A, 0x4A, 0x4A));
	const FLinearColor Hover = bLight ? FLinearColor(0.8714f, 0.8714f, 0.8714f) : FLinearColor::FromSRGBColor(FColor(0x56, 0x56, 0x56));
	const FLinearColor Press = bLight ? FLinearColor(0.7867f, 0.7867f, 0.7867f) : FLinearColor::FromSRGBColor(FColor(0x3A, 0x3A, 0x3A));
	const FLinearColor Line = bLight ? FLinearColor(0.4179f, 0.4179f, 0.4179f) : FLinearColor::FromSRGBColor(FColor(0x5A, 0x5A, 0x5A));
	const FLinearColor Label = bLight ? FLinearColor(0.0160f, 0.0160f, 0.0160f) : FLinearColor::FromSRGBColor(FColor(0xE0, 0xE0, 0xE0));
	MobiusButton.Normal.TintColor = Fill;
	MobiusButton.Hovered.TintColor = Hover;
	MobiusButton.Pressed.TintColor = Press;
	MobiusButton.Disabled.TintColor = Fill;
	MobiusButton.Normal.OutlineSettings.Color = Line;
	MobiusButton.Hovered.OutlineSettings.Color = Line;
	MobiusButton.Pressed.OutlineSettings.Color = Line;
	MobiusButton.Disabled.OutlineSettings.Color = Line;
	MobiusButton.NormalForeground = Label;
	MobiusButton.HoveredForeground = bLight ? FLinearColor::Black : FLinearColor::White;
	MobiusButton.PressedForeground = Label;

	// ButtonWithText labels (ribbon tabs, Browse) read "Mobius.Text.Label". The stock UseForeground
	// colour resolves to plain white here (the buttons' per-state foreground never reaches these
	// STextBlocks), which is unreadable on the light chrome — so pin an EXPLICIT colour per theme.
	// Explicit both ways (no capture/restore) so a stuck value self-heals on the next switch.
	FTextBlockStyle& LabelText = const_cast<FTextBlockStyle&>(FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Label"));
	LabelText.ColorAndOpacity = bLight
		? FSlateColor(FLinearColor(0.0331f, 0.0331f, 0.0331f))  // #333333
		: FSlateColor(FLinearColor(0.745f, 0.745f, 0.745f));    // #e0e0e0
}

UWidget* UUIThemeSubsystem::HandleGenerateThemedComboEntry(const FString Item)
{
	UTextBlock* Text = NewObject<UTextBlock>(this);
	Text->SetText(FText::FromString(Item));
	Text->SetFont(FMobiusStyle::Get().GetWidgetStyle<FTextBlockStyle>("Mobius.Text.Field").Font);
	Text->SetColorAndOpacity(FSlateColor(CurrentTheme == EMobiusUITheme::Light
		? FLinearColor(0.016f, 0.016f, 0.016f)
		: FLinearColor(0.625f, 0.625f, 0.625f)));
	return Text;
}

// Dev diagnostic: dump colour-relevant state of every live ButtonWithText.
static FAutoConsoleCommandWithWorldAndArgs GMobiusDumpButtonsCmd(
	TEXT("Mobius.DumpButtons"),
	TEXT("Log colour state of live ButtonWithText widgets."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TArray<UUserWidget*> All;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, All, UUserWidget::StaticClass(), false);
		for (UUserWidget* UW : All)
		{
			if (!UW || !UW->WidgetTree)
			{
				continue;
			}
			UW->WidgetTree->ForEachWidget([](UWidget* W)
			{
				if (UButtonWithText* B = Cast<UButtonWithText>(W))
				{
					const FLinearColor CO = B->GetColorAndOpacity();
					const FLinearColor BG = B->GetBackgroundColor();
					const FSlateColor NF = B->GetStyle().NormalForeground;
					UE_LOG(LogMobiusTheme, Display, TEXT("BTN %s: ColorAndOpacity=(%.3f,%.3f,%.3f,%.2f) BackgroundColor=(%.3f,%.3f,%.3f) NormalTint=%s NormalFg=(%.3f,%.3f,%.3f)"),
						*B->GetName(), CO.R, CO.G, CO.B, CO.A, BG.R, BG.G, BG.B,
						*B->GetStyle().Normal.TintColor.GetSpecifiedColor().ToString(),
						NF.IsColorSpecified() ? NF.GetSpecifiedColor().R : -1.0f,
						NF.IsColorSpecified() ? NF.GetSpecifiedColor().G : -1.0f,
						NF.IsColorSpecified() ? NF.GetSpecifiedColor().B : -1.0f);
				}
			});
		}
	}));

// Headless/dev verification hook: Mobius.SetUITheme 0|1 from the console.
static FAutoConsoleCommandWithWorldAndArgs GMobiusSetUIThemeCmd(
	TEXT("Mobius.SetUITheme"),
	TEXT("Set the Mobius UI theme at runtime: 0 = dark, 1 = light."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			return;
		}
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UUIThemeSubsystem* ThemeSubsystem = GameInstance->GetSubsystem<UUIThemeSubsystem>())
			{
				ThemeSubsystem->SetTheme(Args[0] == TEXT("1") ? EMobiusUITheme::Light : EMobiusUITheme::Dark);
			}
		}
	}));
