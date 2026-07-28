// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobiusWidgetEditorTools.generated.h"

class UWidget;

/**
 * A6b (2026-07-28): editor-only scripting helpers for Widget Blueprint surgery.
 *
 * WHY THIS EXISTS: the themed-Border migration has to change the CLASS of ~79 widgets already authored
 * inside WidgetTrees. The engine can do it (right-click -> Replace With, i.e.
 * FWidgetBlueprintEditorUtils::ReplaceWidgets), but that lives in UMGEditor's PRIVATE headers and there is
 * no Python or MCP route to it — verified 2026-07-28: unreal.WidgetBlueprintEditorLibrary and
 * unreal.WidgetBlueprintLibrary do not exist in 5.5, and manage_blueprint's set_widget_parent_class
 * changes the Blueprint's OWN parent, not a child widget's class. Rather than 79 manual designer edits,
 * this exposes the swap as a BlueprintCallable so the migration is one reviewable, repeatable script.
 *
 * Built on PUBLIC runtime UMG API (UWidgetTree, UPanelWidget::ReplaceChild) rather than UMGEditor
 * internals, so it does not depend on private engine headers that move between versions.
 */
UCLASS()
class UMobiusWidgetEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Replace one widget inside a Widget Blueprint's tree with a new instance of NewWidgetClass, keeping
	 * the widget's NAME, its parent SLOT (and therefore all layout/slot data), its child content, and every
	 * property whose name and type still match.
	 *
	 * Does NOT compile or save — call CompileAndSaveWidgetBlueprint after a batch, so a failed run leaves
	 * nothing on disk.
	 *
	 * @param WidgetBlueprintPath  e.g. "/Game/01_Dev/Widgets/Components/WBP_HelpPanel"
	 * @param WidgetName           the widget's name in the hierarchy, e.g. "HChip_move_0"
	 * @param NewWidgetClass       must be a subclass of the widget's current class, or the property copy
	 *                             is not meaningful — enforced, see OutError.
	 * @param OutError             human-readable reason on failure; empty on success.
	 * @return true if the widget was replaced.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Editor|Widgets")
	static bool ReplaceWidgetClass(const FString& WidgetBlueprintPath, const FString& WidgetName,
	                               TSubclassOf<UWidget> NewWidgetClass, FString& OutError);

	/** Mark structurally modified, compile, and save. Separate from the swap so a batch is all-or-nothing. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Editor|Widgets")
	static bool CompileAndSaveWidgetBlueprint(const FString& WidgetBlueprintPath, FString& OutError);

	/**
	 * Set a property by name on a widget inside a Widget Blueprint's tree, using the property's own text
	 * import — so enums can be passed as their name ("PanelHeaderBg") and bools as "true"/"false".
	 * Used to stamp FillRole / bThemeOutline / OutlineRole after a swap.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Editor|Widgets")
	static bool SetWidgetPropertyText(const FString& WidgetBlueprintPath, const FString& WidgetName,
	                                  const FString& PropertyName, const FString& ValueAsText, FString& OutError);

	/** Read a property back as text — used to verify a migration without opening the editor UI. */
	UFUNCTION(BlueprintCallable, Category = "Mobius|Editor|Widgets")
	static bool GetWidgetPropertyText(const FString& WidgetBlueprintPath, const FString& WidgetName,
	                                  const FString& PropertyName, FString& OutValue, FString& OutError);
};
