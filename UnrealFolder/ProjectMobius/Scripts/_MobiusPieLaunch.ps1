<#
    Shared launch helper for the PIE-*.ps1 test scripts beside this file.

    Dot-sourced, not run directly. Each PIE-*.ps1 declares only its file set and calls
    Invoke-MobiusPieLaunch, so the engine/data resolution and reporting live in exactly one place
    and cannot drift between the three test configurations.

    EVERYTHING IS RESOLVED RELATIVE TO THE CALLING SCRIPT. No absolute path from the machine this
    was written on appears anywhere, so the tree can be copied to another machine or another drive
    and the scripts keep working. Layout assumed:

        <workspace>\ProjectMobius\                                       the repo
        <workspace>\ProjectMobius\TestData\                              shared test data (committed)
        <workspace>\ProjectMobius\UnrealFolder\ProjectMobius\            the .uproject
        <workspace>\ProjectMobius\UnrealFolder\ProjectMobius\Scripts\    these scripts
        <workspace>\Mobius_InternalData\                                 private test data (NOT committed)

    TWO DATA ROOTS, chosen by name rather than by search order. The committed PIE-*.ps1 scripts
    beside this file ask for 'TestData' and therefore run on any fresh clone. A maintainer with the
    larger private datasets keeps their own copies under Scripts\InternalTesting\ (gitignored) which
    ask for 'Mobius_InternalData'. Picking by name, not by "first one that exists", is deliberate:
    an order-based search would silently hand the committed scripts the private root on a machine
    that has both, and their relative paths would then resolve to nothing.
#>

Set-StrictMode -Off

# Fail loudly. Without this a mistake in here (the .Keys enumeration bug below was one) prints a red
# error and then carries on to launch anyway, which is how a test can silently stop validating.
$ErrorActionPreference = 'Stop'

# Extensions each argument accepts, mirroring UMobiusStartupPreloadSubsystem::IsExtensionSupportedForSlot.
$script:MobiusSlotExtensions = @{
    Geometry   = @('.fbx', '.obj', '.udatasmith', '.ifc', '.wkt', '.h5')
    Pedestrian = @('.json', '.h5')
    BRisk      = @('.smv')
}

function Get-MobiusProjectRoot {
    param([Parameter(Mandatory)][string] $ScriptDir)
    # WALK UP rather than assuming exactly one level. Scripts\ normally sits directly under the
    # .uproject folder, but the private copies live one level deeper in Scripts\InternalTesting\,
    # and a fixed Split-Path would quietly resolve to the wrong folder for those.
    $dir = $ScriptDir
    for ($i = 0; $i -lt 6 -and $dir; $i++) {
        $candidate = Join-Path $dir 'ProjectMobius.uproject'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        $dir = Split-Path -Parent $dir
    }
    return $null
}

function Get-MobiusDataRoot {
    param(
        [Parameter(Mandatory)][string] $ScriptDir,
        # 'TestData'            -> the repo's own committed samples; works on a fresh clone.
        # 'Mobius_InternalData' -> the maintainer's private datasets, never committed.
        [string] $RootName = 'TestData'
    )

    $uproject = Get-MobiusProjectRoot -ScriptDir $ScriptDir
    if (-not $uproject) { return $null }

    # <uproject dir> -> UnrealFolder -> ProjectMobius (repo root) -> workspace
    $uprojectDir = Split-Path -Parent $uproject
    $repoRoot    = Split-Path -Parent (Split-Path -Parent $uprojectDir)
    $workspace   = Split-Path -Parent $repoRoot

    if ($RootName -eq 'TestData') {
        # Committed alongside the source, so this is the one a contributor will have.
        $candidates = @( (Join-Path $repoRoot 'TestData') )
    }
    else {
        # Ordered by likelihood, not preference: the first hit wins.
        $candidates = @(
            (Join-Path $workspace 'Mobius_InternalData')                                   # current home
            (Join-Path $uprojectDir 'Mobius_InternalData')                                 # inside the project
            (Join-Path $workspace 'Packaged\Development\Windows\ProjectMobius\Mobius_InternalData')
        )
    }

    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c -PathType Container) { return $c }
    }
    return $null
}

function Get-MobiusEngineRoot {
    param(
        [string] $Explicit,
        [string] $UProject
    )

    if ($Explicit) { return $Explicit }

    # Read the engine version the project is bound to rather than hard-coding 5.5, so this keeps
    # working after an engine upgrade.
    $assoc = $null
    if ($UProject -and (Test-Path -LiteralPath $UProject -PathType Leaf)) {
        try { $assoc = (Get-Content -LiteralPath $UProject -Raw | ConvertFrom-Json).EngineAssociation } catch { }
    }

    $candidates = @()
    if ($assoc) {
        # Non-default install locations are recorded here by the Epic launcher. (On this machine the
        # key holds only a stale 4.0 entry, so the standard path below is what actually resolves -
        # it is checked anyway because it is correct when present and costs nothing.)
        $key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$assoc"
        if (Test-Path $key) {
            $dir = (Get-ItemProperty -Path $key -ErrorAction SilentlyContinue).InstalledDirectory
            if ($dir) { $candidates += $dir }
        }
        $candidates += "C:\Program Files\Epic Games\UE_$assoc"
        $candidates += "D:\Program Files\Epic Games\UE_$assoc"
    }
    # Last resort: any launcher-installed engine on the usual drives.
    foreach ($root in @('C:\Program Files\Epic Games', 'D:\Program Files\Epic Games')) {
        if (Test-Path -LiteralPath $root) {
            $candidates += (Get-ChildItem -LiteralPath $root -Directory -Filter 'UE_*' -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending | ForEach-Object { $_.FullName })
        }
    }

    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath (Join-Path $c 'Engine\Binaries\Win64\UnrealEditor.exe') -PathType Leaf)) {
            return $c
        }
    }
    return $null
}

<#
.SYNOPSIS
    Launch Mobius with a given set of preload files. Called by the PIE-*.ps1 scripts.

.PARAMETER SkipTypeValidation
    Send the paths through even when the extension does not match the argument. Only the
    invalid-files test uses this: its whole purpose is to prove the application REJECTS
    mismatched types, so validating locally would defeat the test by refusing to launch.
#>
function Invoke-MobiusPieLaunch {
    param(
        [Parameter(Mandatory)][string] $ScriptDir,
        [Parameter(Mandatory)][string] $Title,
        [string]   $RelGeometry,
        [string]   $RelPedestrian,
        [string]   $RelBRisk,
        [string]   $Expectation,
        [switch]   $SkipTypeValidation,
        # Which data root the relative paths above are written against. See Get-MobiusDataRoot.
        [ValidateSet('TestData', 'Mobius_InternalData')]
        [string]   $DataRootName = 'TestData',
        # Caller-facing switches, forwarded verbatim from each PIE-*.ps1.
        [switch]   $Editor,
        [switch]   $DryRun,
        [switch]   $NoPause,
        [string]   $EnginePath,
        [string]   $DataRoot,
        [string[]] $ExtraArgs = @()
    )

    $uproject = Get-MobiusProjectRoot -ScriptDir $ScriptDir
    if (-not $uproject) {
        Write-Host ''
        Write-Host 'Could not find ProjectMobius.uproject above this script.' -ForegroundColor Red
        Write-Host "Keep these scripts in <project>\Scripts\ (or a folder under it).  Script folder: $ScriptDir"
        Complete-MobiusPieLaunch -NoPause:$NoPause -ExitCode 1
    }

    if (-not $DataRoot) { $DataRoot = Get-MobiusDataRoot -ScriptDir $ScriptDir -RootName $DataRootName }
    if (-not $DataRoot) {
        Write-Host ''
        Write-Host "Could not find the '$DataRootName' data root." -ForegroundColor Red
        if ($DataRootName -eq 'TestData') {
            Write-Host 'Expected it at the repository root, i.e. <repo>\TestData. Pass -DataRoot to override.'
        }
        else {
            Write-Host 'Expected it beside the repo, i.e. <workspace>\Mobius_InternalData, or pass -DataRoot.'
            Write-Host 'That folder is private and is not part of the repository; the committed'
            Write-Host 'PIE-*.ps1 scripts in the parent folder use <repo>\TestData instead.'
        }
        Complete-MobiusPieLaunch -NoPause:$NoPause -ExitCode 1
    }

    $engine = Get-MobiusEngineRoot -Explicit $EnginePath -UProject $uproject
    if (-not $engine) {
        Write-Host ''
        Write-Host 'Could not find an Unreal Engine install with Engine\Binaries\Win64\UnrealEditor.exe.' -ForegroundColor Red
        Write-Host 'Pass -EnginePath "<engine root>".'
        Complete-MobiusPieLaunch -NoPause:$NoPause -ExitCode 1
    }
    $exe = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe'

    # ---- resolve + check the file set -------------------------------------------------------
    $slots = [ordered]@{}
    if ($RelGeometry)   { $slots['Geometry']   = Join-Path $DataRoot $RelGeometry }
    if ($RelPedestrian) { $slots['Pedestrian'] = Join-Path $DataRoot $RelPedestrian }
    if ($RelBRisk)      { $slots['BRisk']      = Join-Path $DataRoot $RelBRisk }

    # Snapshot the keys with @(): the loop writes back into $slots, and assigning to a key
    # invalidates a live .Keys enumerator ("Collection was modified"). Without the snapshot the loop
    # threw on its second iteration and every slot after the first went UNVALIDATED - which looked
    # fine, because the paths were already absolute and printed correctly either way.
    $problems = @()
    foreach ($slot in @($slots.Keys)) {
        $path = $slots[$slot]
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $problems += "$slot : NOT FOUND - $path"
            continue
        }
        if (-not $SkipTypeValidation) {
            $ext = [System.IO.Path]::GetExtension($path).ToLowerInvariant()
            if ($script:MobiusSlotExtensions[$slot] -notcontains $ext) {
                $problems += ("$slot : '$ext' is not accepted (expects {0}) - $path" -f ($script:MobiusSlotExtensions[$slot] -join ' '))
            }
        }
        # LiteralPath throughout: these filenames contain spaces and curly braces.
        $slots[$slot] = (Resolve-Path -LiteralPath $path).ProviderPath
    }

    if ($problems.Count -gt 0) {
        Write-Host ''
        Write-Host 'Problem with the test file set:' -ForegroundColor Red
        $problems | ForEach-Object { Write-Host "  $_" }
        Write-Host ''
        Write-Host "Data root used: $DataRoot" -ForegroundColor Yellow
        Complete-MobiusPieLaunch -NoPause:$NoPause -ExitCode 1
    }

    # ---- build the argument list ------------------------------------------------------------
    $launchArgs = @( ('"{0}"' -f $uproject) )
    if (-not $Editor) {
        # -game runs a real game world off the editor binaries: no editor UI to load and no Play
        # button to press, so it is much faster than opening the editor, and GIsEditor is FALSE so
        # the first-launch legal-notice gate is live exactly as in a packaged build.
        $launchArgs += '-game'
        $launchArgs += @('-windowed', '-ResX=1600', '-ResY=900')
    }
    foreach ($slot in $slots.Keys) {
        $launchArgs += ('-Mobius{0}="{1}"' -f $slot, $slots[$slot])
    }
    if ($ExtraArgs.Count -gt 0) { $launchArgs += $ExtraArgs }

    # ---- report -----------------------------------------------------------------------------
    Write-Host ''
    Write-Host $Title -ForegroundColor Cyan
    Write-Host ('mode      : {0}' -f $(if ($Editor) { 'EDITOR - args are consumed by the FIRST Play In Editor of this editor run' } else { '-game (standalone game world, no editor, no Play button)' }))
    Write-Host ("engine    : $engine")
    Write-Host ("data root : $DataRoot")
    Write-Host ''
    foreach ($slot in $slots.Keys) { Write-Host ("  {0,-10} {1}" -f $slot, $slots[$slot]) }
    Write-Host ''
    Write-Host 'Command line:' -ForegroundColor Cyan
    Write-Host ('  "{0}" {1}' -f $exe, ($launchArgs -join ' '))
    Write-Host ''
    Write-Host 'Equivalent console commands (paste into an ALREADY-RUNNING session instead):' -ForegroundColor Cyan
    foreach ($slot in $slots.Keys) { Write-Host ('  Mobius.Load.{0} {1}' -f $slot, $slots[$slot]) }
    Write-Host '  Mobius.Load.Status'
    if ($Expectation) {
        Write-Host ''
        Write-Host 'Expected result:' -ForegroundColor Cyan
        $Expectation -split "`n" | ForEach-Object { Write-Host "  $_" }
    }
    Write-Host ''

    if ($DryRun) {
        Write-Host 'DryRun: nothing launched.' -ForegroundColor Yellow
        Complete-MobiusPieLaunch -NoPause:$NoPause -ExitCode 0
    }

    $process = Start-Process -FilePath $exe -ArgumentList $launchArgs -PassThru
    Write-Host ('Launched PID {0}.' -f $process.Id) -ForegroundColor Green
    if ($Editor) {
        Write-Host 'Press Play in the editor once it finishes loading - the files load on that first PIE session.' -ForegroundColor Yellow
    }
    Write-Host ''
    Complete-MobiusPieLaunch -NoPause:$NoPause -ExitCode 0
}

<#
    Right-click > "Run with PowerShell" closes the console the instant the script ends, taking the
    printed command line and any error with it. Hold the window unless told not to, and tolerate a
    non-interactive host where Read-Host would throw.
#>
function Complete-MobiusPieLaunch {
    param([switch] $NoPause, [int] $ExitCode = 0)

    if (-not $NoPause) {
        try { Read-Host 'Press Enter to close' | Out-Null } catch { }
    }
    exit $ExitCode
}
