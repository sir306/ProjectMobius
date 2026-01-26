// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "UserProjectSettings.generated.h"

/**
 * 
 */
UCLASS(config=ProjectUserSettings, ProjectUserConfig, Blueprintable)
class MOBIUSCORE_API UUserProjectSettings : public UGameUserSettings
{
	GENERATED_BODY()
public:
	UUserProjectSettings(const FObjectInitializer& ObjectInitializer);
	
	/** */
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	void SaveConfig();
	
	/** */
	void LoadConfig();
	
	/** */
	void ResetConfig();
	
	
#pragma region PUBLIC_VARIABLES
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UserSettings")
	UGameUserSettings* ProjectUserSettings;
	
#pragma endregion PUBLIC_VARIABLES
	
private:
#pragma region PRIVATE_VARIABLES
	UPROPERTY(Config)
	bool bEnableMobiusLoggerAtStartup = true;
	
	UPROPERTY(Config)
	bool bDisplayMobiusLogWindowAtStartup = false;
#pragma endregion PRIVATE_VARIABLES
	
public:
#pragma region GETTERS_AND_SETTERS
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	bool GetEnableMobiusLoggerAtStartup() const { return bEnableMobiusLoggerAtStartup; }
	
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	void SetEnableMobiusLoggerAtStartup(bool bEnable) { bEnableMobiusLoggerAtStartup = bEnable; }
	
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	bool GetDisplayMobiusLogWindowAtStartup() const { return bDisplayMobiusLogWindowAtStartup; }
	
	UFUNCTION(BlueprintCallable, Category="UserSettings")
	void SetDisplayMobiusLogWindowAtStartup(bool bEnable) { bDisplayMobiusLogWindowAtStartup = bEnable; }
#pragma endregion GETTERS_AND_SETTERS

public:
#pragma region RUNTIME_CONTROL
	/** Enable or disable the Mobius logger at runtime. Updates setting and notifies subsystem immediately. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	void EnableMobiusLogger(bool bEnable);

	/** Show or hide the Mobius log window at runtime. Updates setting and notifies subsystem immediately. */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	void ShowMobiusLogWindow(bool bShow);

	/** Get current runtime state of logger (may differ from startup setting). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	bool IsMobiusLoggerEnabled() const;

	/** Get current runtime state of log window (may differ from startup setting). */
	UFUNCTION(BlueprintCallable, Category="UserSettings|Logger")
	bool IsMobiusLogWindowVisible() const;
#pragma endregion RUNTIME_CONTROL

};
