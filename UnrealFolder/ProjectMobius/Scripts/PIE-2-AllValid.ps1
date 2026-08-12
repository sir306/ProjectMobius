<#
.SYNOPSIS
    TEST 2 of 3 - all three arguments valid (geometry + agents + B-RISK).

.DESCRIPTION
    The full positive case. All three files load and every field populates.

        -MobiusGeometry     ISO-Test-1-3DView.fbx
        -MobiusPedestrian   iso-test-json-1.json
        -MobiusBRisk        iso-test-json-1-brisk\iso-test-json-1-brisk.smv

    Files come from the repository's own TestData folder, so this runs on a fresh clone with no
    setup. Pass -DataRoot to point it somewhere else.

    WHAT THIS SAMPLE DOES AND DOES NOT COVER. iso-test-json-1-brisk is the committed single-room
    ISO scenario (see its README.txt): a .smv plus the matching _zone.csv, which is enough to
    exercise the B-RISK load path and the smoke/health calculation. It does NOT ship B-RISK's
    input1.xml / output1.xml, so the imported-tenability path is not exercised here, and being one
    room it says nothing about multi-room vent flow.

    Two things this test used to cover and no longer does, because no committed file has the
    shape: a geometry folder containing a SPACE, and a filename containing CURLY BRACES
    ("{3D}", as Revit/Datasmith exports them). Both are classic reasons a caller's argument
    building breaks. Those remain covered by the maintainer's private variant under
    Scripts\InternalTesting\, which is not part of this repository.

    Right-click > Run with PowerShell, or run it from a prompt. No arguments needed.

.PARAMETER Editor
    Open the editor instead of a standalone game world. The files are then consumed by the first
    Play In Editor session of that editor run.

.PARAMETER DryRun
    Print the command line and the console-command equivalents without launching.

.PARAMETER NoPause
    Do not wait for Enter before closing (for use from another script).

.EXAMPLE
    .\PIE-2-AllValid.ps1

.EXAMPLE
    .\PIE-2-AllValid.ps1 -DryRun
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
    -Title         'TEST 2 of 3 - ALL THREE VALID (expect everything to load)' `
    -DataRootName  'TestData' `
    -RelGeometry   'ISO-Test-1-3DView.fbx' `
    -RelPedestrian 'iso-test-json-1.json' `
    -RelBRisk      'iso-test-json-1-brisk\iso-test-json-1-brisk.smv' `
    -Expectation   ("All three File-tab fields show their filenames.`n" +
                    "Building geometry renders; agents spawn; the B-RISK hazard room appears.`n" +
                    "Log shows three 'dispatched' lines under LogMobiusPreload and no rejections.") `
    -Editor:$Editor -DryRun:$DryRun -NoPause:$NoPause `
    -EnginePath $EnginePath -DataRoot $DataRoot -ExtraArgs $ExtraArgs
