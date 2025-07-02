// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PerformanceUtilSubsystem.generated.h"

enum EScalabilitySettings : uint8;
enum EScalabilityCategories : uint8;
/**
 * This subsystem is used to manage performance within the game. It can be used to notify the system when a users
 * hardware is not performing well, triggering automatic performance optimizations. Such as simple agent representations.
 */
UCLASS()
class MOBIUSCORE_API UPerformanceUtilSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Constructor for the Performance Util Subsystem
	 */
	explicit UPerformanceUtilSubsystem();

	/**
	 * Destructor for the Performance Util Subsystem
	 */
	virtual ~UPerformanceUtilSubsystem() override;

	/**
	 * Initialize the subsystem. This is called when the subsystem is created. We can initialize other subsystems
	 * and delegates here.
	 * 
	 * @param[FSubsystemCollectionBase] Collection Subsystem collection that this subsystem belongs to, useful for initializing other subsystems
	 * that this subsystem may depend on.
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * Cleans up and deinitializes the subsystem. This is called when the subsystem is being destroyed, allowing for
	 * release of allocated resources or unbinding of any delegates to ensure proper cleanup.
	 */
	virtual void Deinitialize() override;

	/**
	 * Get the stat ID for this subsystem. 
	 * 
	 * @return The stat ID for this subsystem
	 */
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPerformanceUtilSubsystem, STATGROUP_Tickables); }

	/**
	 * Tick function that is called every frame. This is where we can perform any necessary updates or checks.
	 * For this subsystem, it can be used to monitor performance metrics or trigger optimizations based on system performance.
	 * 
	 * @param DeltaTime World delta time, the time since the last tick.
	 */
	virtual void Tick(float DeltaTime) override;

	/**
	 * Applies a console variable setting based on a string input.
	 * The string should be in the format "CVarName=Value".
	 *
	 * @param CVarSetting The console variable setting to apply.
	 */
	void ApplyCVarSetting(const FString& CVarSetting);
	
	/**
	 * Takes a string array to apply console commands for performance tuning.
	 *
	 * @param CommandArray The array of console commands to apply.
	 */
	void ApplyConsoleCommands(const TArray<FString>& CommandArray);

	/**
	 * Apply predefined scalability settings to the game, based on the provided scalability level.
	 *
	 * @param[TEnumAsByte<EScalabilitySettings>] ScalabilityLevel The scalability level to apply.
	 * @param[TEnumAsByte<EScalabilityCategories>] ScalabilityCategory The category of scalability settings to apply.
	 */
	UFUNCTION(BlueprintCallable)
	void ApplyScalabilityLevel(const TEnumAsByte<EScalabilitySettings> ScalabilityLevel,
		const TEnumAsByte<EScalabilityCategories> ScalabilityCategory);

	/**
	 * 
	 * @param ScalabilityLevel The scalability level to apply to all categories.
	 */
	UFUNCTION(BlueprintCallable)
	void ApplyScalabilityLevelToAll(TEnumAsByte<EScalabilitySettings> ScalabilityLevel);

	/**
	 * Get the current scalability settings for the game for the given category.
	 *
	 * @param[TEnumAsByte<EScalabilityCategories>] ScalabilityCategory The category of scalability settings to retrieve.
	 * @return The current scalability settings.
	 */
	EScalabilitySettings GetScalabilityLevel(const TEnumAsByte<EScalabilityCategories> ScalabilityCategory) const;

	/**
	 * Get the current screen resolution.
	 * 
	 * @return The current screen resolution as an FIntPoint.
	 */
	FIntPoint GetCurrentScreenResolution() const;

	/**
	 * Get all available screen resolutions for the system.
	 * 
	 * @return A list of all available screen resolutions for the system.
	 */
	TArray<FIntPoint> GetSystemScreenResolutions() const;

	void UpdateScreenResolutions(FIntPoint NewResolution);
	
public:
	/** Ptr to the game user setting object -> we use this to apply scalability settings
	 * (as the console commands to change do not apply without it) */
	UPROPERTY()
	TObjectPtr<UGameUserSettings> GameUserSettings;
};
