# Preload test configurations — scripts and console commands

Three fixed test configurations for the `-Mobius*` preload arguments, each as a runnable script and
as console commands you can paste into a session that is **already running**.

| # | Script | Configuration | Expected |
| --- | --- | --- | --- |
| 1 | `PIE-1-InvalidFiles.ps1` | all three arguments given the **wrong file type** | three rejections, nothing loads |
| 2 | `PIE-2-AllValid.ps1` | geometry + agents + B-RISK, all valid | everything loads |
| 3 | `PIE-3-GeometryAndAgents.ps1` | geometry + agents, **no** B-RISK | two load, B-RISK field stays empty |

## Running a script

Right-click → **Run with PowerShell**, or from a prompt in this folder:

```
.\PIE-2-AllValid.ps1
```

No arguments needed. The console is held open at the end so you can read the output.

Every path is resolved from the script's own folder, so this works on any machine with the same
tree layout — nothing is hard-coded to one drive. It finds:

- the project at `..\ProjectMobius.uproject`
- the engine from the project's `EngineAssociation` (registry, then `C:\Program Files\Epic Games\UE_<ver>`)
- the data at `<workspace>\Mobius_InternalData`

Override any of them with `-EnginePath` / `-DataRoot` if your layout differs.

### Switches

| Switch | Effect |
| --- | --- |
| *(none)* | `-game` — a standalone game world off the editor binaries. No editor to load, no Play button. Fastest fresh session, and `GIsEditor` is false so the legal-notice gate behaves as in a packaged build. |
| `-Editor` | Opens the editor instead. The files are consumed by the **first** Play In Editor session of that editor run. Use when you want breakpoints or the editor's tooling. |
| `-DryRun` | Prints the command line and console commands, launches nothing. |
| `-ExtraArgs` | Appended verbatim, e.g. `-ExtraArgs @('-ResX=2560','-ResY=1440')`. |

## Console commands (fastest loop)

Launch arguments are consumed **once per process**, so they cannot re-load into a session that is
already up. For repeat testing without restarting, use the console instead. Press `` ~ `` to open it.

```
Mobius.Load.Geometry <path>
Mobius.Load.Pedestrian <path>
Mobius.Load.BRisk <path>
Mobius.Load.Status
```

Paths do **not** need quoting here — the command rejoins its arguments, so a path containing spaces
works either way. `Mobius.Load.Status` reports, per file, what was requested, whether it was
accepted, and whether the loader for it was available.

### Getting the commands for *your* machine

Rather than editing paths by hand after moving machines, let the script print them:

```
.\PIE-2-AllValid.ps1 -DryRun
```

It emits the exact console commands with absolute paths correct for wherever the tree currently
lives, ready to copy. That is the reliable way to get these after taking the work home.

The literal commands below are correct for a workspace at `D:\NickWork\Mobius`. On another machine
replace that prefix with your workspace root — or just use `-DryRun` above.

### 1 — invalid file types (negative test)

Every path exists but is on the wrong argument: a `.json` as geometry, an `.fbx` as agent data, a
`.json` as B-RISK. Expect three *Startup File Load* error windows naming the accepted types, all
three fields still reading `Click Browse to choose file`, and three
`rejected (unsupported file type)` lines in the log with **no** `dispatched` line.

```
Mobius.Load.Geometry D:\NickWork\Mobius\Mobius_InternalData\TechSchoolTest\TechnicalSchool_1000.json
Mobius.Load.Pedestrian D:\NickWork\Mobius\Mobius_InternalData\TechSchoolTest\Technical-School-For-Lab-3D.fbx
Mobius.Load.BRisk D:\NickWork\Mobius\Mobius_InternalData\TechSchoolTest\TechnicalSchool_1000.json
Mobius.Load.Status
```

### 2 — all three valid

```
Mobius.Load.Geometry D:\NickWork\Mobius\Mobius_InternalData\12 RoomTest\Exported-model\ISO-Test-8-FireSmoke-3DView-{3D}.udatasmith
Mobius.Load.Pedestrian D:\NickWork\Mobius\Mobius_InternalData\12 RoomTest\ISO-Revit-Simulex-Tests\ISO-Test-8-FireSmoke-ok-no-fire.json
Mobius.Load.BRisk D:\NickWork\Mobius\Mobius_InternalData\12-room-test-v2\basemodel_default\basemodel_default.smv
Mobius.Load.Status
```

This one is worth keeping as the standing smoke test: the geometry path contains both a space
(`12 RoomTest`) and curly braces (`{3D}`), which are the two things that break naive argument
building.

### 3 — geometry + agents only

```
Mobius.Load.Geometry D:\NickWork\Mobius\Mobius_InternalData\TechSchoolTest\RevitTwinmotionExpt\Technical_School_R2027-3DView-{3D}.udatasmith
Mobius.Load.Pedestrian D:\NickWork\Mobius\Mobius_InternalData\TechSchoolTest\TechnicalSchool_1000.json
Mobius.Load.Status
```

B-RISK is deliberately absent. Its field staying on `Click Browse to choose file` is the pass
condition, not a fault.

## Re-issuing the same path

`-MobiusGeometry` / `-MobiusBRisk` no-op when the game instance already holds that exact path — the
log says `already loaded ... no reload was triggered` rather than reloading. Load a different file
first, or restart, if you need a genuine re-import. `-MobiusPedestrian` has no such guard and always
reloads.

## Accepted types

| Argument / command | Types |
| --- | --- |
| `Geometry` | `.fbx` `.obj` `.udatasmith` `.ifc` `.wkt` `.h5` |
| `Pedestrian` | `.json` `.h5` |
| `BRisk` | `.smv` (manifest only; companion files are read from the same folder) |

## If nothing happens

- Run `Mobius.Load.Status`.
- Check the log for `LogMobiusPreload` — every request, rejection and dispatch is named there.
  `<project>\Saved\Logs\` for editor and `-game` runs.
- Launch arguments only fire once per process; a second `-Editor` PIE session in the same editor run
  will not re-preload. Use the console commands.
