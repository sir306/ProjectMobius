// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 Nicholas R. Harding
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
#include "Interfaces/ProjectMobiusInterface.h"
#include "Slate/SlateBrushAsset.h"
#include "LoadDataParentWidget.generated.h"

class UScrollBox;
/**
 * 
 */
UCLASS()
class MOBIUSWIDGETS_API ULoadDataParentWidget : public UUserWidget, public IProjectMobiusInterface
{
	GENERATED_BODY()

#pragma region METHODS
public:
#pragma region PUBLIC_METHODS
	// Native Pre Construct
	virtual void NativePreConstruct() override;

	// Native Constructor 
	virtual void NativeConstruct() override;

	// Tick Method for in C++ for the widget
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Method to keep the design properties synchronized */
	virtual void SynchronizeProperties() override;

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
	 * @param[const FString&] AgentFilePath The path to the file selected
	 * @param[const FString&] MeshFilePath The path to the mesh file selected
	 * @param[bool] bAgentSuccess Whether the agent file was successfully selected
	 * @param[bool] bMeshSuccess Whether the mesh file was successfully selected
	 * 
	 */
	
	virtual void DialogClosed(const FString& AgentFilePath, const FString& MeshFilePath, bool bAgentSuccess, bool bMeshSuccess);

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
	
#pragma endregion PROPERTIES_AND_CLASS_COMPONENTS

#pragma region GETTERS_SETTERS
public:
	// Getter for the CurrentDataFile TextBlock text
	//FORCEINLINE FText* GetCurrentDataFile() const { return CurrentDataFile->GetText(); }

	// Setter for the CurrentDataFile TextBlock text
	//FORCEINLINE void SetCurrentDataFileText(FText Text) { CurrentDataFile->SetText(Text); }

#pragma endregion GETTERS_SETTERS
};
