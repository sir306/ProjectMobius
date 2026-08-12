# First-launch legal notice

## What ships

The packaged desktop application, **Project Möbius**, shows a native Unreal Slate modal before a user can interact with it. The compact red **X** exits the application; it never dismisses the notice and allows use. The **I agree - continue to Project Möbius** button is disabled until both acknowledgements are checked.

The notice links to:

- this package's `BuildDocs/THIRD-PARTY-LICENSES.md`;
- the MIT License page;
- Epic's Unreal Engine EULA; and
- Epic's Content License Agreement.

It also states that Twinmotion desktop-export content is excluded from this packaged interactive application. This is an acknowledgement screen, not a replacement for any agreement required directly by Epic or a third-party licensor.

## Persistence and release behaviour

Acceptance is stored as `AcceptedLegalNoticeVersion` in Unreal's per-user `GameUserSettings.ini`, through `UUserProjectSettings::SaveSettings()`. The setting is outside the packaged directory, so a packaged build copied to another computer or OS user presents the notice again. Once accepted, the notice is not shown for that user until the notice version changes.

The current version is `1` in `UserProjectSettings.cpp`. For a material change to the displayed terms:

1. Update the notice text and/or shipped documentation.
2. Have the revised legal text reviewed and approved by the project owner or legal adviser.
3. Raise `CurrentLegalNoticeVersion` in both `HasAcceptedCurrentLegalNotice()` and `AcceptCurrentLegalNotice()`.
4. Build and package for Windows and macOS. Existing users will then see the new notice once.

## Manual release verification

1. Use a clean OS account, or remove the project's `GameUserSettings.ini` in the local Unreal Saved/Config folder.
2. Start the packaged executable/application. Confirm that the legal window opens, the normal UI cannot receive input, and the continue button is initially disabled.
3. Confirm that each link opens its target and that the third-party notice is present beside the packaged application under `BuildDocs`.
4. Tick both acknowledgements and continue. Close and relaunch: the notice must not reappear.
5. Repeat from a different OS account or another machine: the notice must reappear.
6. Test the Mac package as well as the Windows package. The implementation uses Unreal Slate and `FPlatformProcess`, so it does not rely on Windows-only APIs or an external Python installation.

## Editor visual preview

After compiling a non-Shipping editor build, open the Unreal Output Log or in-game console and run:

```
Mobius.LegalNotice.Preview
```

This forces the exact native dialog to open even when the current user has already accepted it. It is not compiled into Shipping builds. In Preview, the red **X** and **I agree** close only the preview window: neither exits the Unreal Editor nor changes the saved acceptance state, so the command can be rerun freely.

## Implementation map

- `Source/MobiusWidgets/Private/Core/MobiusWidgets.cpp` opens the modal after the first packaged game world loads. Editor and unattended sessions are skipped.
- `Source/MobiusWidgets/Private/UI/LegalNoticeDialog.cpp` builds the native, application-modal UI directly from the authoritative Mobius theme palette.
- `Source/MobiusCore/Public/UserConfig/UserProjectSettings.h` and `Private/UserConfig/UserProjectSettings.cpp` hold the independent, versioned acceptance state.

The gate is a launch-flow control, not tamper resistance: a modified binary or manual user-config edit can bypass it. This is intentional because Project Möbius is open source; stronger enforcement would require additional anti-tampering, integrity-validation, and distribution controls. An unmodified packaged application cannot proceed through its normal UI until acceptance is recorded.
