# Mobius Logger Runtime Control Implementation Summary

## What Was Implemented

Added 4 new **blueprint-callable** methods to `UUserProjectSettings` that enable runtime control of the Mobius logger and log window with automatic subsystem notification.

## Files Modified

### 1. UserProjectSettings.h
**Location:** `Source/MobiusCore/Public/UserConfig/UserProjectSettings.h`

Added new RUNTIME_CONTROL region with 4 blueprint-callable methods:

```cpp
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
```

### 2. UserProjectSettings.cpp
**Location:** `Source/MobiusCore/Private/UserConfig/UserProjectSettings.cpp`

#### Added Include Headers:
```cpp
#include "Subsystems/MobiusCustomLoggerSubsystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
```

#### Implemented Methods:

**EnableMobiusLogger(bool bEnable)**
- Updates the `bEnableMobiusLoggerAtStartup` setting
- Calls `MobiusCustomLoggerSubsystem::SetLoggingEnabled()` immediately
- Logs a message when enabled via the logger
- Automatically closes the log window when logger is disabled
- *Use case:* Toggle logger on/off at runtime from blueprints

**ShowMobiusLogWindow(bool bShow)**
- Updates the `bDisplayMobiusLogWindowAtStartup` setting
- Calls `MobiusUserFeedbackSubsystem::RequestLogWindowOpen/Close()` immediately
- Prevents showing window if logger is disabled (logs warning)
- *Use case:* Show/hide log window at runtime from blueprints

**IsMobiusLoggerEnabled() const**
- Queries current runtime state from `MobiusCustomLoggerSubsystem::IsLoggingEnabled()`
- Returns false if subsystem not available
- *Use case:* Check if logging is currently active before acting

**IsMobiusLogWindowVisible() const**
- Queries current runtime state from `MobiusUserFeedbackSubsystem::IsLogWindowOpen()`
- Returns false if subsystem not available
- *Use case:* Check if log window is currently visible before acting

#### Enhanced Error Handling:

Updated `SaveConfig()` and `LoadConfig()` methods to use proper error reporting:
- Now use `UMobiusUserFeedbackSubsystem::ReportError()` instead of silent failures
- Provides actionable error messages to users
- Falls back to UE_LOG if subsystems unavailable
- Gracefully handles missing GEngine

## Design Details

### Separation of Concerns

**Startup Settings** (existing methods - unchanged):
- `GetEnableMobiusLoggerAtStartup()` / `SetEnableMobiusLoggerAtStartup()`
- `GetDisplayMobiusLogWindowAtStartup()` / `SetDisplayMobiusLogWindowAtStartup()`
- These only change variables, used by ProjectMobiusGameInstance at launch

**Runtime Control** (new methods):
- `EnableMobiusLogger()` / `ShowMobiusLogWindow()`
- These update variables AND notify subsystems immediately
- Provide real-time control without restarting

**State Queries** (new methods):
- `IsMobiusLoggerEnabled()` / `IsMobiusLogWindowVisible()`
- These query actual subsystem state, not just variables
- Allows blueprints to respond to actual runtime state

### Dependency Management

**Logger → Window Dependency:**
- Window cannot be shown if logger is disabled
- Disabling logger automatically closes window
- Prevents inconsistent states

**Subsystem Notification:**
- All methods use safe subsystem access via GEngine
- No hard dependencies on UI modules
- Graceful degradation if subsystems unavailable

### Thread Safety

- All subsystem methods are already thread-safe (dispatch to game thread)
- UserProjectSettings methods follow standard UObject conventions
- Safe to call from blueprints (game thread only)

## Blueprint Usage Examples

### Toggle Logger On/Off
```
Get Game User Settings (cast to UserProjectSettings)
  → Enable Mobius Logger (true/false)
```

### Show/Hide Log Window
```
Get Game User Settings (cast to UserProjectSettings)
  → Show Mobius Log Window (true/false)
```

### Check Logger Status
```
Get Game User Settings (cast to UserProjectSettings)
  → Is Mobius Logger Enabled
  → (Branch: if true, continue; if false, show message)
```

### Save Settings to Disk
```
Get Game User Settings (cast to UserProjectSettings)
  → Save Settings
```

## Integration Points

### With MobiusCustomLoggerSubsystem
- Calls `SetLoggingEnabled(bool)` to control logging
- Calls `IsLoggingEnabled() const` to check state
- Calls `EnqueueLogMessage()` to log control events
- Methods are blueprint-callable and thread-safe

### With MobiusUserFeedbackSubsystem
- Calls `RequestLogWindowOpen()` to open window
- Calls `RequestLogWindowClose()` to close window
- Calls `IsLogWindowOpen() const` to check state
- Calls `ReportError()` for error reporting
- Window control is automatic - no manual wiring needed

### With ProjectMobiusGameInstance
- Existing `Init()` method unchanged
- Startup settings read normally at launch
- Runtime control methods provide optional behavior changes

## Testing Recommendations

### Basic Functionality
- [ ] Enable logger at runtime → `MobiusCustomLog.txt` receives messages
- [ ] Disable logger → New messages don't appear in log file
- [ ] Re-enable logger → Logging resumes
- [ ] Show log window → UI popup appears
- [ ] Hide log window → UI popup closes
- [ ] Try to show window with logger disabled → Warning logged, window stays closed

### State Queries
- [ ] `IsMobiusLoggerEnabled()` returns true when enabled
- [ ] `IsMobiusLoggerEnabled()` returns false when disabled
- [ ] `IsMobiusLogWindowVisible()` returns true when open
- [ ] `IsMobiusLogWindowVisible()` returns false when closed

### Settings Persistence
- [ ] After enabling logger, call `SaveSettings()`
- [ ] Restart game → Logger still enabled
- [ ] After opening window, call `SaveSettings()`
- [ ] Restart game → Window still open at startup

### Error Handling
- [ ] Corrupt config file → Error popup via MobiusUserFeedbackSubsystem
- [ ] Missing settings → Error popup with helpful message
- [ ] GEngine unavailable → Falls back to UE_LOG

## Related Files (Unchanged)

- `ProjectMobiusGameInstance.cpp` - Continues to read startup settings normally
- `MobiusCustomLoggerSubsystem.h/cpp` - Provides underlying logging API
- `MobiusUserFeedbackSubsystem.h/cpp` - Provides underlying error/window API
- All UI systems - Automatically respond to subsystem notifications

## Notes

- Runtime control methods do NOT auto-save to disk
- User must explicitly call `SaveSettings()` to persist changes
- This allows safe toggling during gameplay
- All subsystem interactions are type-safe with null checks
- No circular dependencies introduced
- Fully compatible with existing startup settings flow

## Blueprint Visibility

Methods appear in Blueprint Editor under:
- **Search:** "Mobius Logger", "Mobius Log Window"
- **Category:** UserSettings|Logger
- **Availability:** Anywhere you can access GameUserSettings

