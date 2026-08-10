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

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Theme/MobiusThemedUserWidget.h"  // A5: event-driven control theming base
#include "Interfaces/ProjectMobiusInterface.h"
#include "Slate/SlateBrushAsset.h"
#include "LoadDataParentWidget.generated.h"

class UScrollBox;
/**
 * 
 */
UCLASS()
// A5 (2026-07-28): base changed UUserWidget -> UMobiusThemedUserWidget so this widget themes its own
// standard controls on construct + every OnThemeChanged (see UUIThemeSubsystem::ThemeStandardControlsInTree)
// instead of waiting for the value walk to find them. Pure C++ base insertion - the WBP still parents to
// this class, so no .uasset changes.
class MOBIUSWIDGETS_API ULoadDataParentWidget : public UMobiusThemedUserWidget, public IProjectMobiusInterface
{
	GENERATED_BODY()

#pragma region METHODS
public:
#pragma region PUBLIC_METHODS
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor
	virtual void NativeConstruct() override;

	/** Native destructor - drops the game-instance file-change subscription. */
	virtual void NativeDestruct() override;

	/** Method to keep the design properties synchronized */
	virtual void SynchronizeProperties() override;

	/**
	 * S1 - pull the two file-row text colours from declared palette roles.
	 *
	 * The stakeholder row for this ("File-panel text boxes do not match the rest of the UI") recorded the
	 * cause as this class not deriving UMobiusThemedUserWidget. That is NOT the cause: A5 already gave it
	 * that base, so ThemeStandardControlsInTree does run over this tree. Measured instead - both text blocks
	 * on all three rows (agent / geometry / B-Risk) are wrong for the same underlying reason, that a bare
	 * UTextBlock is themed by the generic TextMap VALUE remap rather than by a declared role:
	 *
	 *  - DataFileTextBlock is authored 0.3231 grey, which lands on TextMap's `dim text` row inside the
	 *    subsystem's 0.012 epsilon. So the path renders #9a9a9a in light and #666666 in dark - the latter
	 *    on an InputBg fill of 0.0243, i.e. barely legible. Every other value readout in the app is retinted
	 *    to InputText ("Mobius.Text.Field", UIThemeSubsystem.cpp:2101), which is what makes this one the odd
	 *    one out in both themes.
	 *  - The row label is authored 0.147,0.162,0.191, which matches NO TextMap row, so it is frozen at that
	 *    one value in both themes and never moves on a toggle. A project-wide scan found exactly three text
	 *    blocks on that colour: these labels. Nothing else can be affected by correcting it.
	 *
	 * Fixed as owner-pulls rather than per-widget literals or a new TextMap row, matching how every other
	 * casualty of a missing generic pass was handled: a declared role beats an untargeted value remap. The
	 * base runs the standard-controls pass and then ApplyMobiusTheme, so these writes always land last.
	 * Both properties are inherited, so this one override covers all three file rows.
	 */
	virtual void ApplyMobiusTheme_Implementation() override;

	/**
	 * Re-read this widget's file path from the game instance and repaint the text block.
	 *
	 * This is what makes the displayed filename correct no matter WHO changed it. Before it existed
	 * the field was only ever written by this widget's own file-dialog callback, so a path set from
	 * anywhere else - a launch argument, a Mobius.Load.* console command, or any future caller of the
	 * game-instance setters - loaded the data while the field still read "Click Browse to choose
	 * file". The widget is now a pure observer of the game instance: the setter is the single writer,
	 * and this pulls from it.
	 *
	 * Bound to whichever OnXxxFileChanged delegate the subclass owns, so it also covers the ordering
	 * both ways round: a widget constructed AFTER the path was set picks it up from the
	 * GetMobiusGameInstanceData() pull in NativeConstruct instead.
	 */
	UFUNCTION()
	void RefreshFromGameInstance();

	/**
	* Method to call when the SelectFileButton is clicked
	*/
	UFUNCTION()
	virtual void OnSelectFileButtonClicked();

	/** Setup TextBlocks */
	UFUNCTION()
	virtual void SetupTextBlocks();

	/** Update TextBlocks with new texts */
	UFUNCTION()
	void UpdateFileTextBlockTexts() const;
	
	/** Update FileProperties with new file paths and names */
	UFUNCTION()
	void UpdateWidgetFileProperties(FString CompleteDataPath);

	/**
	* Get Mobius Game Instance data -- 
	* defaults to getting the movement data, 
	* made overridable so we can get the data we want for childs 
	*/
	UFUNCTION()
	virtual void GetMobiusGameInstanceData();

	/**
	* Update Mobius Game Instance data -- 
	* defaults to updating the movement data, 
	* made overridable so we can update the data we want from childs
	*/
	UFUNCTION()
	virtual void UpdateMobiusGameInstanceData();

	/**
	 * When the data file text changes, the scroll box should update by scrolling to the end
	 */
	UFUNCTION()
	void UpdateScrollBarPosition() const;

	/**
	 * Handle the dialog closed event
	 * @param[FString] AgentFilePath The path to the file selected
	 * @param[FString] MeshFilePath The path to the mesh file selected
	 * @param[bool] bAgentSuccess Whether the agent file was successfully selected
	 * @param[bool] bMeshSuccess Whether the mesh file was successfully selected
	 * 
	 */
	
	virtual void DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess, bool bMeshSuccess);

protected:
	/**
	 * Subscribe RefreshFromGameInstance to the game-instance delegate that carries THIS widget's
	 * file. The base does nothing: ULoadDataParentWidget has no file of its own, and each subclass
	 * owns a different delegate (mesh / pedestrian / B-Risk).
	 */
	virtual void BindGameInstanceFileDelegate() {}

	/** Mirror of BindGameInstanceFileDelegate, called from NativeDestruct. */
	virtual void UnbindGameInstanceFileDelegate() {}

public:

#pragma endregion PUBLIC_METHODS

#pragma endregion METHODS

#pragma region PROPERTIES_AND_CLASS_COMPONENTS
public:

protected:
	/** The string holding the filename and path to data */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Data|File", meta = (AllowPrivateAccess = "true"))
	FString DataFile;

private:
	/** Text block to show current selected Data file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	class UTextBlock* DataFileTextBlock;

	/** Button for executing find file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<class UButtonWithText> SelectFileButton;

	/** Scrollbox - stores the text block of the text file */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidget))
	TObjectPtr<UScrollBox> DataFileScrollBox;

	/**
	 * S1: the row's own label ("Pedestrian Vectors" / "Geometry" / "Smoke (B-RISK)").
	 *
	 * The name is the designer default and is deliberately NOT changed here. A BindWidget matches on the
	 * widget's name, so renaming it would mean editing all three .uassets - churn in the public repo, with
	 * the risk of orphaning any Blueprint graph reference - to buy nothing this pull does not already get.
	 * Optional rather than required so a row authored without the label still constructs instead of failing
	 * the bind at compile.
	 *
	 * BlueprintReadWrite + AllowPrivateAccess to match the sibling BindWidget properties, and because the
	 * rows' Blueprint graphs already read this widget by name - promoting it to a C++ property without
	 * exposing it makes those existing `Get TextBlock_127` nodes fail to compile.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true", BindWidgetOptional))
	TObjectPtr<class UTextBlock> TextBlock_127;
	
#pragma endregion PROPERTIES_AND_CLASS_COMPONENTS

#pragma region GETTERS_SETTERS
public:
	// Getter for the CurrentDataFile TextBlock text
	//FORCEINLINE FText* GetCurrentDataFile() const { return CurrentDataFile->GetText(); }

	// Setter for the CurrentDataFile TextBlock text
	//FORCEINLINE void SetCurrentDataFileText(FText Text) { CurrentDataFile->SetText(Text); }

#pragma endregion GETTERS_SETTERS
};
