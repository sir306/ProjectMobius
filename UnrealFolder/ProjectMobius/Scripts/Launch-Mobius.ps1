<#
.SYNOPSIS
    Launch Project Mobius with geometry, pedestrian and/or B-Risk files preloaded.

.DESCRIPTION
    Reference launcher for the -Mobius* preload arguments. A third-party application does not need
    this script - it only needs to spawn the executable with the arguments shown by -DryRun - but this
    is the definition of "correct" for that command line, and the quoting rules below are the part
    that is easy to get wrong.

    All three files are INDEPENDENT. Pass one, two, or all three; the app loads whatever it was given
    and each file succeeds or fails on its own. Each one behaves exactly as if the matching Browse
    button had been clicked: the file field in the UI updates, and the full import / pre-processing
    chain runs.

    Files are validated HERE as well as in the app, so a typo fails immediately at the console instead
    of as a dialog thirty seconds into startup.

.PARAMETER Geometry
    Building / geometry mesh: .fbx .obj .udatasmith .ifc .wkt .h5

.PARAMETER Pedestrian
    Pedestrian trajectory data: .json .h5

.PARAMETER BRisk
    B-Risk scenario manifest: .smv  (the manifest only - its companion files are found beside it)

.PARAMETER Mode
    Packaged   - the shipped executable. Default. This is the mode a third-party app uses.
    Editor     - UnrealEditor.exe with the project open. Arguments are consumed by the FIRST Play In
                 Editor session of that editor process; later sessions re-trigger with the
                 Mobius.Load.* console commands. The legal notice never appears in the editor.
    EditorGame - UnrealEditor.exe -game. Runs as a standalone game off the editor binaries, so
                 GIsEditor is FALSE and the legal-notice gate is live. Use this to exercise the
                 first-launch consent path without waiting for a package.

.PARAMETER ExePath
    Override the packaged executable. Default: <repo>\..\..\..\Packaged\Development\Windows\ProjectMobius.exe

.PARAMETER EnginePath
    Override the engine root used by -Mode Editor / EditorGame. Default: C:\Program Files\Epic Games\UE_5.5

.PARAMETER PreloadTimeoutSeconds
    Seconds a queued file waits for its loader to come online AFTER the legal notice is accepted.
    Time spent reading the notice is never counted against it. Omit to use the app default (30).

.PARAMETER ExtraArgs
    Additional raw arguments appended verbatim (e.g. -windowed -ResX=2560 -ResY=1440).

.PARAMETER DryRun
    Print the exact command line and exit without launching. Copy this into your own launcher.

.PARAMETER ShowLog
    After launching, wait for the log to appear and print every preload line from it. Diagnostic aid;
    does not affect the app.

.PARAMETER Wait
    Block until the application exits.

.EXAMPLE
    # All three files, packaged build - the normal third-party integration call.
    # Replace <path> with the folder holding your exported data.
    .\Launch-Mobius.ps1 -Geometry '<path>\building.fbx' `
                        -Pedestrian '<path>\pedestrians.json' `
                        -BRisk '<path>\scenario.smv'

.EXAMPLE
    # Pedestrian data only. Geometry and B-Risk are simply not supplied.
    .\Launch-Mobius.ps1 -Pedestrian '<path>\pedestrians.json'

.EXAMPLE
    # Geometry + B-Risk, no crowd.
    .\Launch-Mobius.ps1 -Geometry '<path>\building.fbx' -BRisk '<path>\scenario.smv'

.EXAMPLE
    # Paths containing spaces - quote them, as below.
    .\Launch-Mobius.ps1 -Pedestrian '<path>\Sim Data\pedestrians one.json'

.EXAMPLE
    # Show the command line without launching, to lift into another application.
    .\Launch-Mobius.ps1 -Pedestrian '<path>\pedestrians.json' -DryRun

.EXAMPLE
    # Quick PIE debugging: consumed by the first Play of this editor process.
    .\Launch-Mobius.ps1 -Mode Editor -Pedestrian '<path>\pedestrians.json'

.EXAMPLE
    # Exercise the first-launch legal-notice gate without packaging.
    .\Launch-Mobius.ps1 -Mode EditorGame -BRisk '<path>\scenario.smv' -ShowLog

.NOTES
    ARGUMENTS (this is the whole contract - anything that can spawn a process can use it):

        ProjectMobius.exe -MobiusGeometry="<path>" -MobiusPedestrian="<path>" -MobiusBRisk="<path>"

    Every argument is optional and independent. Additionally:

        -MobiusPreloadTimeout=<seconds>      readiness timeout override

    An ALREADY-RUNNING instance is driven by console command instead, which also works through the
    engine's own -ExecCmds argument:

        Mobius.Load.Geometry   <path>
        Mobius.Load.Pedestrian <path>
        Mobius.Load.BRisk      <path>
        Mobius.Load.Status                   what was requested and what happened to it

        ProjectMobius.exe -ExecCmds="Mobius.Load.Pedestrian <path>\pedestrians.json"

    QUOTING. Wrap every path in double quotes so spaces survive, and never leave a trailing backslash
    immediately before the closing quote - on Windows `\"` escapes the quote and the argument runs on
    into the next one. This script strips trailing backslashes for you.

    DO NOT pass -unattended. It suppresses the legal notice, which means acceptance can never be
    recorded, and the app deliberately REFUSES to preload rather than treat that as consent.

    LEGAL NOTICE. On a packaged first launch the app shows a mandatory terms/licence notice. Preload
    waits for it: files load after "I agree", and if the notice is declined the queued files are
    discarded and the app exits without loading anything. Human reading time is not charged against
    the readiness timeout.
#>
[CmdletBinding()]
param(
    [string] $Geometry,
    [string] $Pedestrian,
    [string] $BRisk,

    [ValidateSet('Packaged', 'Editor', 'EditorGame')]
    [string] $Mode = 'Packaged',

    [string] $ExePath,
    [string] $EnginePath = 'C:\Program Files\Epic Games\UE_5.5',

    [double] $PreloadTimeoutSeconds = 0,

    [string[]] $ExtraArgs = @(),

    [switch] $DryRun,
    [switch] $ShowLog,
    [switch] $Wait
)

$ErrorActionPreference = 'Stop'

# ------------------------------------------------------------------------------------------------
# Paths
# ------------------------------------------------------------------------------------------------

# This script lives in <project>\Scripts\, so the .uproject folder is one level up.
$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir  = Split-Path -Parent $scriptDir
$uproject    = Join-Path $projectDir 'ProjectMobius.uproject'

# Default packaged location, relative to the repo, matching CL-BuildCookPackScripts output:
#   <workspace>\Packaged\Development\Windows\ProjectMobius.exe
# projectDir = <workspace>\ProjectMobius\UnrealFolder\ProjectMobius
$workspaceDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $projectDir))
$defaultExe   = Join-Path $workspaceDir 'Packaged\Development\Windows\ProjectMobius.exe'

# ------------------------------------------------------------------------------------------------
# File validation - same rules the application applies, applied earlier
# ------------------------------------------------------------------------------------------------

$supported = @{
    'Geometry'   = @('.fbx', '.obj', '.udatasmith', '.ifc', '.wkt', '.h5')
    'Pedestrian' = @('.json', '.h5')
    'BRisk'      = @('.smv')
}

function Resolve-PreloadFile {
    param(
        [string] $Path,
        [string] $Slot
    )

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }

    # Trailing backslashes would escape the closing quote in the generated command line.
    $trimmed = $Path.Trim().Trim('"').TrimEnd('\')

    if (-not (Test-Path -LiteralPath $trimmed -PathType Leaf)) {
        throw "$Slot file not found (or is a folder): $trimmed"
    }

    $full = (Resolve-Path -LiteralPath $trimmed).ProviderPath
    $ext  = [System.IO.Path]::GetExtension($full).ToLowerInvariant()

    if ($supported[$Slot] -notcontains $ext) {
        throw "$Slot file has unsupported type '$ext'. Supported: $($supported[$Slot] -join ', ')"
    }

    return $full
}

$geometryFull   = Resolve-PreloadFile -Path $Geometry   -Slot 'Geometry'
$pedestrianFull = Resolve-PreloadFile -Path $Pedestrian -Slot 'Pedestrian'
$briskFull      = Resolve-PreloadFile -Path $BRisk      -Slot 'BRisk'

if (-not $geometryFull -and -not $pedestrianFull -and -not $briskFull) {
    Write-Warning 'No -Geometry, -Pedestrian or -BRisk supplied. Mobius will start with nothing preloaded.'
}

# B-Risk companion sanity check. The .smv is a manifest; the zone time series lives beside it, and
# without it the import succeeds but every results-driven feature (smoke, tenability, dose) stays
# blank. Warn rather than fail - a geometry-only B-Risk folder is a legitimate authoring state.
if ($briskFull) {
    $briskDir = Split-Path -Parent $briskFull
    if (-not (Get-ChildItem -LiteralPath $briskDir -Filter '*_zone.csv' -File -ErrorAction SilentlyContinue)) {
        Write-Warning "No *_zone.csv beside '$briskFull'. Geometry will import but smoke/tenability results will be empty."
    }
}

# ------------------------------------------------------------------------------------------------
# Target executable + argument list
# ------------------------------------------------------------------------------------------------

$launchArgs = @()

switch ($Mode) {
    'Packaged' {
        $target = if ($ExePath) { $ExePath } else { $defaultExe }
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "Packaged executable not found: $target`nBuild one with CL-BuildCookPackScripts\Package-Development.ps1, or pass -ExePath."
        }
        $logPath = Join-Path (Split-Path -Parent $target) 'ProjectMobius\Saved\Logs\ProjectMobius.log'
    }

    'Editor' {
        $target = Join-Path $EnginePath 'Engine\Binaries\Win64\UnrealEditor.exe'
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "UnrealEditor.exe not found: $target`nPass -EnginePath."
        }
        $launchArgs += ('"{0}"' -f $uproject)
        $logPath = Join-Path $projectDir 'Saved\Logs\ProjectMobius.log'
    }

    'EditorGame' {
        $target = Join-Path $EnginePath 'Engine\Binaries\Win64\UnrealEditor.exe'
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "UnrealEditor.exe not found: $target`nPass -EnginePath."
        }
        # -game makes GIsEditor false, so the legal-notice gate is live - the point of this mode.
        $launchArgs += ('"{0}"' -f $uproject)
        $launchArgs += '-game'
        $logPath = Join-Path $projectDir 'Saved\Logs\ProjectMobius.log'
    }
}

if ($geometryFull)   { $launchArgs += ('-MobiusGeometry="{0}"'   -f $geometryFull) }
if ($pedestrianFull) { $launchArgs += ('-MobiusPedestrian="{0}"' -f $pedestrianFull) }
if ($briskFull)      { $launchArgs += ('-MobiusBRisk="{0}"'      -f $briskFull) }

if ($PreloadTimeoutSeconds -gt 0) {
    $launchArgs += ('-MobiusPreloadTimeout={0}' -f $PreloadTimeoutSeconds)
}

if ($ExtraArgs.Count -gt 0) { $launchArgs += $ExtraArgs }

$commandLine = '"{0}" {1}' -f $target, ($launchArgs -join ' ')

Write-Host ''
Write-Host 'Mobius launch command:' -ForegroundColor Cyan
Write-Host "  $commandLine"
Write-Host ''
Write-Host 'Preloading:' -ForegroundColor Cyan
# Windows PowerShell 5.1 has no inline if-expression, so these go through $( ).
Write-Host ('  geometry   : {0}' -f $(if ($geometryFull)   { $geometryFull }   else { '<none>' }))
Write-Host ('  pedestrian : {0}' -f $(if ($pedestrianFull) { $pedestrianFull } else { '<none>' }))
Write-Host ('  b-risk     : {0}' -f $(if ($briskFull)      { $briskFull }      else { '<none>' }))
Write-Host ''

if ($DryRun) {
    Write-Host 'DryRun: nothing launched.' -ForegroundColor Yellow
    return
}

# ------------------------------------------------------------------------------------------------
# Launch
# ------------------------------------------------------------------------------------------------

# Roll the log aside so -ShowLog cannot report a previous run's result as this one's.
if ($ShowLog -and (Test-Path -LiteralPath $logPath -PathType Leaf)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    Move-Item -LiteralPath $logPath -Destination "$logPath.$stamp.bak" -Force -ErrorAction SilentlyContinue
}

$startParams = @{
    FilePath     = $target
    ArgumentList = $launchArgs
    PassThru     = $true
}

$process = Start-Process @startParams
Write-Host ('Launched PID {0}.' -f $process.Id) -ForegroundColor Green

if ($ShowLog) {
    Write-Host 'Waiting for preload log lines (Ctrl+C to stop watching; the app keeps running)...' -ForegroundColor Cyan

    $deadline = (Get-Date).AddSeconds(120)
    $seen     = 0

    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $logPath -PathType Leaf) {
            # Read shared: the engine holds the log open for writing.
            $stream = $null
            try {
                $stream = New-Object System.IO.FileStream($logPath, [System.IO.FileMode]::Open,
                    [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
                $reader = New-Object System.IO.StreamReader($stream)
                $lines  = ($reader.ReadToEnd() -split "`r?`n") | Where-Object { $_ -match 'LogMobiusPreload' }
                $reader.Dispose()
            }
            catch { $lines = @() }
            finally { if ($stream) { $stream.Dispose() } }

            if ($lines.Count -gt $seen) {
                $lines[$seen..($lines.Count - 1)] | ForEach-Object { Write-Host "  $_" }
                $seen = $lines.Count
            }

            if ($lines -match 'dispatched|rejected|timed out|discarding') {
                # Give the remaining slots a moment to report, then stop.
                Start-Sleep -Milliseconds 1500
                break
            }
        }

        if ($process.HasExited) {
            Write-Warning ('Application exited (code {0}) before preload completed. The legal notice may have been declined.' -f $process.ExitCode)
            break
        }

        Start-Sleep -Milliseconds 500
    }

    if ($seen -eq 0) {
        Write-Warning "No LogMobiusPreload lines found in $logPath. Run 'Mobius.Load.Status' in the app console for the full picture."
    }
}

if ($Wait) {
    $process.WaitForExit()
    Write-Host ('Exited with code {0}.' -f $process.ExitCode)
    exit $process.ExitCode
}
