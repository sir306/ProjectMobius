# MobiusWidgets Module Layout

This module groups UI, Slate, and helper logic by purpose to keep includes and ownership clear.

## Directory Map

```
Source/MobiusWidgets/
  Public/
    Core/                 Module interfaces + subsystem
    UI/                   UMG widgets (top-level)
      Components/         Reusable UMG widgets
      Components/Scalability/
      InWorld/            World-space UI + components
      LoadSave/           Load/save UI
    Slate/                Slate widgets
      Components/         Reusable Slate widgets
    ErrorHandling/        Error window widget + Slate window
    Util/                 Utility helpers
    Data/                 Shared structs/enums used by widgets
  Private/
    (mirrors Public/)
```

## Notes

- Keep public headers in `Public/` and implementation in `Private/`.
- UMG widgets live under `UI/`; Slate widgets live under `Slate/`.
- Shared data types used by widgets go in `Data/`.
