// Copyright (c) 2025 ProjectMobius contributors. Licensed under MIT.
//
// ThemeMaterialCardBrushTest.cpp
//
// Guards the two brush conventions UBaseLoadingWidget::ThemeMaterialCard has to keep apart. It is called
// on every UBorder in a UBaseLoadingWidget's tree, matched by TYPE, so it sees whatever brush a designer
// authored - and the right write depends entirely on which kind of brush that is:
//
//   material-backed brush -> BrushColor must be pinned WHITE, and the colours go into the MID params.
//                            SBorder paints Background.TintColor x BrushColor x material output, so a
//                            non-white BrushColor multiplies the MID output a second time (D169).
//   flat-colour brush     -> there is no MID to write, so the Fill goes on BrushColor and the brush TINT
//                            is pinned white instead - the same single-multiplier convention as
//                            UMobiusThemedBorder::RefreshThemedBorder.
//
// The failure this test exists for is silent and one-directional: whitening BrushColor before testing for
// a MID (the behaviour up to 2026-07-30) leaves a flat-colour Border with a white fill and NOTHING written
// back, which on screen is a blank card rather than an obviously broken one. No asset reaches that path
// today - the only two WBPs deriving from UBaseLoadingWidget (WBP_LoadingBar, WBP_LoadingImage) are both
// material-backed - so nothing in the project would catch a regression here. Hence a synthetic Border.
//
// ThemeMaterialCard is static and touches only the UBorder's own properties, so no widget tree, no
// UUserWidget instance and no theme subsystem are needed; the colours are passed in explicitly.
//
// Run from the Session Frontend (search "ProjectMobius.UI") or:
//   MobiusPerf\RunTests.ps1
//
// CoreMinimal FIRST, before the shipping guard: UE_BUILD_SHIPPING comes from Core's Misc/Build.h, not
// from the compiler command line, so a bare `#if !UE_BUILD_SHIPPING` ahead of it trips C4668 (warnings are
// errors) the moment this file compiles NON-UNITY - which adaptive unity does for every file in the working
// set. The sibling tests get away with the guard-first order only while they sit in a unity blob.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "Components/Border.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Styling/SlateBrush.h"
#include "UI/Components/BaseLoadingWidget.h"
#include "UObject/Package.h"

namespace
{
	/** Matches the param names ThemeMaterialCard writes (M_WidgetBackground's two colour params). */
	const FName GFillParam(TEXT("Background Color Tint"));
	const FName GOutlineParam(TEXT("Border Color Tint"));

	/** The MI the two real loading cards use, so the material case runs against the shipping asset. */
	const TCHAR* GCardMaterialPath =
		TEXT("/Game/01_Dev/Widgets/WidgetMaterials/BackgroundMaterials/"
			"MI_LoadingInnerBackground.MI_LoadingInnerBackground");

	const FLinearColor GFill(0.85499f, 0.87137f, 0.88792f);  // WellBg, light
	const FLinearColor GOutline(0.76815f, 0.76815f, 0.76815f); // PanelDivider, light

	/** A flat brush carrying the two values the function has to overwrite, plus an outline to colour. */
	FSlateBrush MakeFlatBrush(const float OutlineWidth)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(FLinearColor(0.04f, 0.04f, 0.04f)); // authored dark tint
		Brush.OutlineSettings.Width = OutlineWidth;
		Brush.OutlineSettings.Color = FSlateColor(FLinearColor(1.0f, 0.0f, 1.0f)); // poison magenta
		return Brush;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FThemeMaterialCardBrushTest,
	"ProjectMobius.UI.ThemeMaterialCardBrushConventions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FThemeMaterialCardBrushTest::RunTest(const FString& Parameters)
{
	// ---- flat-colour brush: Fill lands on BrushColor, tint pinned white, outline recoloured ----
	{
		UBorder* Flat = NewObject<UBorder>(GetTransientPackage());
		Flat->SetBrush(MakeFlatBrush(2.0f));
		Flat->SetBrushColor(FLinearColor::Black); // poison: must not survive

		UBaseLoadingWidget::ThemeMaterialCard(Flat, GFill, GOutline);

		TestTrue(TEXT("flat brush has no MID (precondition)"), Flat->GetDynamicMaterial() == nullptr);
		TestTrue(TEXT("flat: BrushColor takes Fill"), Flat->GetBrushColor().Equals(GFill, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("flat: brush TintColor pinned white"),
			Flat->Background.TintColor.GetSpecifiedColor().Equals(FLinearColor::White, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("flat: outline takes Outline"),
			Flat->Background.OutlineSettings.Color.GetSpecifiedColor().Equals(GOutline, KINDA_SMALL_NUMBER));

		// Idempotent: a second pass must not drift (the theme subsystem re-runs this on every toggle).
		UBaseLoadingWidget::ThemeMaterialCard(Flat, GFill, GOutline);
		TestTrue(TEXT("flat: BrushColor stable on second pass"),
			Flat->GetBrushColor().Equals(GFill, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("flat: outline stable on second pass"),
			Flat->Background.OutlineSettings.Color.GetSpecifiedColor().Equals(GOutline, KINDA_SMALL_NUMBER));
	}

	// ---- flat brush that draws no outline: the authored outline colour stays asset-owned ----
	{
		UBorder* NoOutline = NewObject<UBorder>(GetTransientPackage());
		NoOutline->SetBrush(MakeFlatBrush(0.0f));

		UBaseLoadingWidget::ThemeMaterialCard(NoOutline, GFill, GOutline);

		TestTrue(TEXT("width-0: BrushColor still takes Fill"),
			NoOutline->GetBrushColor().Equals(GFill, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("width-0: authored outline colour untouched"),
			NoOutline->Background.OutlineSettings.Color.GetSpecifiedColor()
				.Equals(FLinearColor(1.0f, 0.0f, 1.0f), KINDA_SMALL_NUMBER));
	}

	// ---- material-backed brush: BrushColor pinned white, colours written through the MID ----
	{
		UMaterialInterface* CardMaterial = LoadObject<UMaterialInterface>(nullptr, GCardMaterialPath);
		if (TestNotNull(TEXT("MI_LoadingInnerBackground loads"), CardMaterial))
		{
			UBorder* Card = NewObject<UBorder>(GetTransientPackage());
			Card->SetBrushFromMaterial(CardMaterial);
			Card->SetBrushColor(FLinearColor::Black); // poison: the double-multiply case

			UBaseLoadingWidget::ThemeMaterialCard(Card, GFill, GOutline);

			TestTrue(TEXT("material: BrushColor pinned white"),
				Card->GetBrushColor().Equals(FLinearColor::White, KINDA_SMALL_NUMBER));

			UMaterialInstanceDynamic* CardMID = Card->GetDynamicMaterial();
			if (TestNotNull(TEXT("material: MID created"), CardMID))
			{
				TestTrue(TEXT("material: MID fill param takes Fill"),
					CardMID->K2_GetVectorParameterValue(GFillParam).Equals(GFill, KINDA_SMALL_NUMBER));
				TestTrue(TEXT("material: MID outline param takes Outline"),
					CardMID->K2_GetVectorParameterValue(GOutlineParam).Equals(GOutline, KINDA_SMALL_NUMBER));
			}
		}
	}

	// ---- null Border is a no-op, not a crash (ForEachWidget hands over whatever is in the tree) ----
	UBaseLoadingWidget::ThemeMaterialCard(nullptr, GFill, GOutline);

	return true;
}

#endif // !UE_BUILD_SHIPPING
