<#
.SYNOPSIS
    TEST 1 of 3 - deliberately WRONG file types on all three arguments.

.DESCRIPTION
    A negative test. Every path below exists on disk but is handed to the wrong argument:

        -MobiusGeometry     <- a .json   (geometry takes .fbx .obj .udatasmith .ifc .wkt .h5)
        -MobiusPedestrian   <- an .fbx   (pedestrian takes .json .h5)
        -MobiusBRisk        <- a .json   (B-RISK takes .smv)

    Files come from the repository's own TestData folder, so this runs on a fresh clone with no
    setup. Pass -DataRoot to point it somewhere else.

    So this proves the REJECTION path, not the load path: the application should refuse all three,
    raise a "Startup File Load / Unsupported file type" window per rejection naming the supplied
    file and the accepted types, and load nothing. The three file fields must stay on
    "Click Browse to choose file".

    Local type validation is deliberately SKIPPED here - validating would refuse to launch and
    there would be nothing to test.

    Right-click > Run with PowerShell, or run it from a prompt. No arguments needed.

.PARAMETER Editor
    Open the editor instead of a standalone game world. The files are then consumed by the first
    Play In Editor session of that editor run.

.PARAMETER DryRun
    Print the command line and the console-command equivalents without launching.

.PARAMETER NoPause
    Do not wait for Enter before closing (for use from another script).

.EXAMPLE
    .\PIE-1-InvalidFiles.ps1

.EXAMPLE
    .\PIE-1-InvalidFiles.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [switch]   $Editor,
    [switch]   $DryRun,
    [switch]   $NoPause,
    [string]   $EnginePath,
    [string]   $DataRoot,
    [string[]] $ExtraArgs = @()
)

$here = $PSScriptRoot
if (-not $here) { $here = Split-Path -Parent $MyInvocation.MyCommand.Path }
. (Join-Path $here '_MobiusPieLaunch.ps1')

Invoke-MobiusPieLaunch `
    -ScriptDir     $here `
    -Title         'TEST 1 of 3 - INVALID FILE TYPES (expect three rejections, nothing loaded)' `
    -DataRootName  'TestData' `
    -RelGeometry   'iso-test-json-1.json' `
    -RelPedestrian 'ISO-Test-1-3DView.fbx' `
    -RelBRisk      'iso-test-json-1.json' `
    -SkipTypeValidation `
    -Expectation   ("Three error windows titled 'Startup File Load', one per argument, each naming`n" +
                    "the supplied file and the accepted extensions.`n" +
                    "All three File-tab fields stay on 'Click Browse to choose file'.`n" +
                    "Log shows three 'rejected (unsupported file type)' lines under LogMobiusPreload`n" +
                    "and NO 'dispatched' line.") `
    -Editor:$Editor -DryRun:$DryRun -NoPause:$NoPause `
    -EnginePath $EnginePath -DataRoot $DataRoot -ExtraArgs $ExtraArgs
