// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PerformanceUtilSubsystem.generated.h"

// Forward declarations
enum EGlobalScalabilitySettings : uint8;
enum EScalabilitySettings : uint8;
enum EScalabilityCategories : uint8;
enum EPedestrianScalabilitySettings : uint8;
enum ERenderPerformanceTier : uint8;

// Delegates
/** Delegate to broadcast new auto optimization triggered - we don't pass params as so many systems require different
 * information and would increase resource usage  */
DECLARE_MULTICAST_DELEGATE(FOnAutoScalabilityChanged);
/** When a user manually makes scalability changes this method is called, it is up to the listeners to determine if changes are made */
DECLARE_MULTICAST_DELEGATE(FOnManualScalabilityChanged);//TODO: investigate whether separate param delegates will be better for performance or not

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
	 * Calculates the current fps (frames per second) based on the current frame time,
	 * and updates the value stored in this subsystem.
	 * 
	 * @param[float] DeltaTime The time since the last frame in seconds.
	 */
	void CalculateCurrentFPS(float DeltaTime);

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

	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem")
	void UpdateGlobalScalabilitySetting(TEnumAsByte<EGlobalScalabilitySettings> NewSetting);

	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem")
	TEnumAsByte<EGlobalScalabilitySettings> GetCumulativeScalabilitySetting() const;

	EPedestrianScalabilitySettings GetCurrentPedestrianAvatarType() const;

	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem")
	void SetCurrentPedestrianAvatarType(EPedestrianScalabilitySettings NewAvatarModelType);

	// ----- Render performance tiers (tasks C1 / C2) -----------------------------------------------
	// Auto-detect / override system that toggles the heavy render-feature cvars (ray tracing, hardware
	// Lumen, anti-aliasing, distance-field AO) at runtime. This subsystem is the SINGLE runtime owner
	// of the cvars it manages: they are applied with ECVF_SetByCode, whose priority outranks
	// SetByScalability / SetByGameSetting, so the sg.* quality UI (ApplyScalabilityLevel* above) will
	// not move them once a tier has been applied. Distinct from the EGlobalScalabilitySettings buckets.

	/**
	 * Detect the appropriate render tier from hardware capability — hardware ray-tracing support,
	 * dedicated VRAM, and total system RAM. Thresholds are heuristic and tunable. (Task C1.)
	 *
	 * @return The detected tier (Low / Medium / High — never Auto).
	 */
	ERenderPerformanceTier DetectHardwareRenderTier() const;

	/**
	 * Apply a render tier's heavy-feature cvar set at runtime via ApplyConsoleCommands. High mirrors
	 * DefaultEngine.ini exactly, so applying it on capable hardware is a value-level no-op (only the
	 * cvar priority is bumped). Auto is resolved through DetectHardwareRenderTier(). (Task C1.)
	 *
	 * @param Tier The tier to apply.
	 */
	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem|Render Tier")
	void ApplyRenderPerformanceTier(TEnumAsByte<ERenderPerformanceTier> Tier);

	/**
	 * Set and persist the user's render tier override, then apply it immediately. Auto re-runs
	 * detection. Applies in any world (incl. editor/PIE) so a tier can be forced for testing. (Task C2.)
	 *
	 * @param NewOverride The override to persist and apply (Auto / Low / Medium / High).
	 */
	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem|Render Tier")
	void SetRenderPerformanceTierOverride(TEnumAsByte<ERenderPerformanceTier> NewOverride);

	/** Returns the currently-applied render tier. */
	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem|Render Tier")
	TEnumAsByte<ERenderPerformanceTier> GetCurrentRenderTier() const;

protected:
	/**
	 * Applies optimizations based on the current FPS.
	 */
	void ApplyOptimizationsBasedOnFPS();

	
private:
	/**
	 * Checks if the current FPS is below the low FPS threshold and
	 * starts the timer or clears it if the FPS is above the threshold.
	 * Using a timer we can be sure if this is a prolonged low FPS issue or a loading blip.
	 */
	void CheckCurrentFPS();

	/**
	 * Called from the low-FPS watchdog (ApplyOptimizationsBasedOnFPS). Intentionally a no-op for the
	 * render tier: C1 is startup hardware-detect + the C2 override only, NOT FPS-driven runtime
	 * degradation (which would break C1's "HIGH identical on capable HW" criterion — see the .cpp).
	 */
	void CheckHardwareUsageAndApplyOptimizations();

	/**
	 * Resolve the persisted override (Auto -> hardware detect) and apply the resulting render tier.
	 * Runs in Game / PIE worlds only, so the editor viewport render state is left untouched at startup.
	 * (Tasks C1 / C2.) Called once from Initialize().
	 */
	void InitializeRenderPerformanceTier();

public:
	/** Delegate to notify when auto scalability changes */
	FOnAutoScalabilityChanged OnAutoScalabilityChanged;

	/** Delegate to notify when scalability changes made by a user */
	FOnManualScalabilityChanged OnManualScalabilityChanged;

	
protected:
	/** Ptr to the game user setting object -> we use this to apply scalability settings
	 * (as the console commands to change do not apply without it) */
	UPROPERTY()
	TObjectPtr<UGameUserSettings> ProjectSettings;

	/** Stores the global scalability setting value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Performance Util Subsystem")
	TEnumAsByte<EGlobalScalabilitySettings> GlobalScalabilitySetting;

	/** Currently-applied render performance tier (tasks C1/C2). Defaults to High so first construction
	 *  and capable hardware match DefaultEngine.ini exactly until detection/override says otherwise. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Performance Util Subsystem|Render Tier")
	TEnumAsByte<ERenderPerformanceTier> CurrentRenderTier;

	/** Store the current pedestrian avatar type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Performance Util Subsystem")
	TEnumAsByte<EPedestrianScalabilitySettings> CurrentAvatarModelType;

	/** Instantaneous FPS, recalculated each tick from DeltaTime. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Performance Util Subsystem")
	float CurrentFPS = 120.0f;

	/** Exponential moving average of FPS (α=0.1). Smooths over ~10 frames. Use this for sustained-drop checks. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Performance Util Subsystem")
	float SmoothedFPS = 120.0f;

	/** Low FPS threshold, when we hit this threshold we need to start auto triggering optimization techniques */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Performance Util Subsystem")
	float LowFPSThreshold = 15.0f;

	/** Timer handle used to trigger auto scalability if prolonged low fps occurs */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Performance Util Subsystem")
	FTimerHandle AutoScalabilityTimerHandle;

public:
	/** Returns the current global scalability setting */
	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem")
	FORCEINLINE TEnumAsByte<EGlobalScalabilitySettings> GetGlobalScalabilitySetting() const { return GlobalScalabilitySetting; }

	/** Instantaneous FPS from last tick. */
	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem")
	FORCEINLINE float GetCurrentFPS() const { return CurrentFPS; }

	/** Exponential moving average FPS (~10-frame window). Prefer this for sustained-drop decisions. */
	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem")
	FORCEINLINE float GetSmoothedFPS() const { return SmoothedFPS; }

	/** Low FPS threshold used by auto-optimisation logic. */
	UFUNCTION(BlueprintCallable, Category = "Performance Util Subsystem")
	FORCEINLINE float GetLowFPSThreshold() const { return LowFPSThreshold; }
};
