# SPDX-License-Identifier: MIT
#
# MSVC/CMake toolchain detection for Build-MobiusIfcBridge.ps1 when it is run BY HAND.
#
# Dot-source this file; it defines Resolve-MobiusGenerator, Resolve-MobiusToolset and
# Convert-MobiusToolsetToArgument.
#
# SCOPE: the superbuild (superbuild.py) always passes -Generator and -Toolset explicitly, so these
# functions only run when someone invokes Build-MobiusIfcBridge.ps1 directly. superbuild.py carries
# the same logic in Python (VS_MAJOR_TO_GENERATOR, resolve_toolset, msvc_toolset_argument) because
# it must work on macOS where PowerShell is not a given. THE TWO TABLES MUST BE UPDATED TOGETHER --
# if you add a Visual Studio version here, add it there too.
#
# WHY THIS EXISTS: every build doc and script in this tree used to hardcode
# -G "Visual Studio 17 2022". On a machine with a different Visual Studio installed that fails at
# configure time with
#
#     CMake Error: Generator "Visual Studio 17 2022" could not find any instance of Visual Studio.
#
# which says nothing about what IS installed. Detect it instead, and print what we picked.

# Deliberately no Set-StrictMode here: this file is dot-sourced, so a strict-mode setting would
# leak into whatever script sourced it and change the behaviour of code that never opted in.

# Visual Studio major version -> CMake generator name. Add a row when a new VS ships; that is the
# ONLY edit a future Visual Studio should need.
function Get-MobiusGeneratorMap {
    return @{
        15 = 'Visual Studio 15 2017'
        16 = 'Visual Studio 16 2019'
        17 = 'Visual Studio 17 2022'
        18 = 'Visual Studio 18 2026'
    }
}

function Get-MobiusVsWherePath {
    $Candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles          'Microsoft Visual Studio\Installer\vswhere.exe')
    )
    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path $Candidate)) { return $Candidate }
    }
    return $null
}

# Returns a PSCustomObject with InstallPath and Generator, or $null if no VS with the C++ toolset
# could be found. VS major version -> CMake generator name; extend the map when a new VS ships.
#
# Result is memoised: this is called by both Resolve-MobiusGenerator and Resolve-MobiusToolset, and
# shelling out to vswhere twice for the same answer is pure latency.
$script:MobiusVsCache = $null
$script:MobiusVsCacheValid = $false

function Resolve-MobiusVisualStudio {
    if ($script:MobiusVsCacheValid) { return $script:MobiusVsCache }
    $script:MobiusVsCacheValid = $true
    $script:MobiusVsCache = $null

    $VsWhere = Get-MobiusVsWherePath
    if (-not $VsWhere) { return $null }

    # -latest alone can pick an install without the C++ toolchain; require the VC tools component.
    #
    # Two traps here, both of which surface as "no Visual Studio found" on a machine that has one:
    #
    #   1. vswhere's -property takes exactly ONE name. Passing two (`-property a, b`) makes
    #      PowerShell hand it two arguments and vswhere exits 87 "Argument expected". Hence two
    #      calls, one property each.
    #   2. Do NOT pipe a native command into `Select-Object -First 1` and then test $LASTEXITCODE.
    #      Select-Object stops the upstream pipeline early, which can leave $LASTEXITCODE nonzero
    #      even though vswhere answered correctly -- an intermittent false negative. Capture the
    #      whole output into an array first, then index it.
    $BaseArgs = @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'value')

    $InstallPathLines = @(& $VsWhere @BaseArgs -property installationPath)
    $VersionLines     = @(& $VsWhere @BaseArgs -property installationVersion)
    if ($InstallPathLines.Count -eq 0 -or $VersionLines.Count -eq 0) { return $null }

    $InstallPath = "$($InstallPathLines[0])".Trim()
    $Version     = "$($VersionLines[0])".Trim()
    if (-not $InstallPath -or $Version -notmatch '^\d+\.') { return $null }

    $Major = [int]($Version -split '\.')[0]
    $GeneratorByMajor = Get-MobiusGeneratorMap
    if (-not $GeneratorByMajor.ContainsKey($Major)) { return $null }

    $script:MobiusVsCache = [PSCustomObject]@{
        InstallPath = $InstallPath
        Version     = $Version
        Generator   = $GeneratorByMajor[$Major]
    }
    return $script:MobiusVsCache
}

# Builds the message shown when auto-detection fails. Detection has exactly three failure modes and
# they need different fixes, so say WHICH one happened, list what is actually installed, and give
# the literal command that works around it. "No Visual Studio found" on a machine that has one is
# the single most time-wasting message this tooling could emit.
function Get-MobiusGeneratorDiagnosis {
    $Supported = (Get-MobiusGeneratorMap).GetEnumerator() | Sort-Object Name
    $SupportedText = ($Supported | ForEach-Object { "      $($_.Value)   (VS major $($_.Name))" }) -join "`n"

    $VsWhere = Get-MobiusVsWherePath
    if (-not $VsWhere) {
        return ("No Visual Studio could be detected: vswhere.exe is not installed.`n`n" +
                "  vswhere ships with the Visual Studio Installer, so this normally means Visual Studio`n" +
                "  itself is not installed. Install Visual Studio 2022 or 2026 with the`n" +
                "  'Desktop development with C++' workload.`n`n" +
                "  If Visual Studio IS installed and you just want to get moving, name the generator`n" +
                "  yourself:`n`n" +
                "      .\Setup-Superbuild.ps1 -Generator 'Visual Studio 17 2022'`n`n" +
                "  Supported generators:`n$SupportedText")
    }

    # Ask again without the C++ requirement, to tell "no VS at all" apart from "VS without C++"
    # apart from "a VS newer than this script knows about".
    $AllPaths    = @(& $VsWhere -products '*' -format value -property installationPath)
    $AllVersions = @(& $VsWhere -products '*' -format value -property installationVersion)

    if ($AllPaths.Count -eq 0) {
        return ("No Visual Studio installation was found at all (vswhere returned nothing).`n`n" +
                "  Install Visual Studio 2022 or 2026 with the 'Desktop development with C++' workload,`n" +
                "  then re-run. To override detection anyway:`n`n" +
                "      .\Setup-Superbuild.ps1 -Generator 'Visual Studio 17 2022'`n`n" +
                "  Supported generators:`n$SupportedText")
    }

    $Found = @()
    for ($i = 0; $i -lt $AllPaths.Count; $i++) {
        $VersionText = if ($i -lt $AllVersions.Count) { "$($AllVersions[$i])".Trim() } else { '?' }
        $Found += "      $VersionText  $("$($AllPaths[$i])".Trim())"
    }
    $FoundText = $Found -join "`n"

    $Map = Get-MobiusGeneratorMap
    $KnownMajors = @($AllVersions | ForEach-Object { if ("$_" -match '^(\d+)\.') { [int]$Matches[1] } })
    $UnknownMajors = @($KnownMajors | Where-Object { -not $Map.ContainsKey($_) } | Sort-Object -Unique)

    if ($UnknownMajors.Count -gt 0 -and @($KnownMajors | Where-Object { $Map.ContainsKey($_) }).Count -eq 0) {
        return ("The installed Visual Studio is newer than this script knows about.`n`n" +
                "  Installed (version, path):`n$FoundText`n`n" +
                "  Unrecognised major version(s): " + ($UnknownMajors -join ', ') + "`n`n" +
                "  Fix permanently: add a row to Get-MobiusGeneratorMap in`n" +
                "      cmake\Resolve-MsvcToolchain.ps1`n" +
                "  mapping that major version to its CMake generator name (run ``cmake --help`` to see`n" +
                "  the exact spelling your CMake supports).`n`n" +
                "  Work around it now by naming the generator yourself:`n`n" +
                "      .\Setup-Superbuild.ps1 -Generator '<name from cmake --help>'`n`n" +
                "  Supported generators:`n$SupportedText")
    }

    return ("Visual Studio is installed, but no installation has the C++ toolset.`n`n" +
            "  Installed (version, path):`n$FoundText`n`n" +
            "  None of these carries the component`n" +
            "      Microsoft.VisualStudio.Component.VC.Tools.x86.x64`n`n" +
            "  Fix: open the Visual Studio Installer -> Modify -> Workloads and tick`n" +
            "  'Desktop development with C++'. Individual components -> 'MSVC v143 14.38.33130' is`n" +
            "  also worth ticking; it is the toolset Unreal Engine 5.5 itself builds with.`n`n" +
            "  To try anyway with an explicit generator:`n`n" +
            "      .\Setup-Superbuild.ps1 -Generator 'Visual Studio 17 2022' -Toolset none`n`n" +
            "  Supported generators:`n$SupportedText")
}

# $Requested: '' or 'auto' -> detect. Anything else is returned verbatim (an explicit override).
function Resolve-MobiusGenerator {
    param([string]$Requested = '')

    if ($Requested -and $Requested -ne 'auto') { return $Requested }

    $Vs = Resolve-MobiusVisualStudio
    if (-not $Vs) { throw (Get-MobiusGeneratorDiagnosis) }
    Write-Host "Detected $($Vs.Generator) at $($Vs.InstallPath)" -ForegroundColor DarkGray
    return $Vs.Generator
}

# Toolset selection.
#
#   '' or 'none' -> no -T argument at all (use whatever the generator defaults to). 'none' exists
#                   because `powershell -File script.ps1 -Toolset ""` SILENTLY DROPS the empty
#                   argument and the script then fails with "Missing an argument for parameter
#                   'Toolset'" -- so a caller that wants "generator default" needs a word, not "".
#   'auto'       -> $Preferred if installed, else the newest installed, else '' with a warning
#   <version>    -> that exact version, hard-failing if it is not installed
#
# $Preferred defaults to the toolset UE 5.5 itself builds with. Matching it is not strictly
# required (the shim's boundary is a C ABI) but it removes an axis of divergence in the CRT the
# DLL links against, for free.
function Resolve-MobiusToolset {
    param(
        [string]$Requested = 'auto',
        [string]$Preferred = '14.38.33130'
    )

    if ($Requested -eq '' -or $Requested -eq 'none') { return '' }

    $Vs = Resolve-MobiusVisualStudio
    $Installed = @()
    if ($Vs) {
        $ToolsRoot = Join-Path $Vs.InstallPath 'VC\Tools\MSVC'
        if (Test-Path $ToolsRoot) {
            $Installed = @(Get-ChildItem -Directory $ToolsRoot | Select-Object -ExpandProperty Name | Sort-Object)
        }
    }

    if ($Requested -ne 'auto') {
        if ($Installed.Count -gt 0 -and $Installed -notcontains $Requested) {
            throw ("Requested MSVC toolset $Requested is not installed. Installed: " +
                   ($Installed -join ', ') + ". Pass -Toolset with one of those, -Toolset auto to " +
                   "pick automatically, or -Toolset '' to use the generator default.")
        }
        return $Requested
    }

    if ($Installed -contains $Preferred) { return $Preferred }

    if ($Installed.Count -gt 0) {
        $Newest = $Installed[-1]
        Write-Warning ("MSVC toolset $Preferred (the one UE 5.5 builds with) is not installed; using " +
                       "$Newest instead. Installed: " + ($Installed -join ', ') + ". Install $Preferred " +
                       "via the Visual Studio Installer -> Individual components if you want an exact ABI match.")
        return $Newest
    }

    Write-Warning "Could not enumerate installed MSVC toolsets; letting the generator pick its default."
    return ''
}

# Turns an MSVC version (14.38.33130) into the full CMake -T argument (v143,version=14.38.33130).
#
# The version alone is NOT enough. `-T version=14.38.33130` under the "Visual Studio 18 2026"
# generator fails with
#
#     given toolset and version specification v145,version=14.38.33130
#     contains an invalid version specification
#
# because CMake pairs the requested version with the generator's DEFAULT platform toolset (v145),
# and 14.38 belongs to v143. Name the platform toolset explicitly and it resolves correctly. Note
# there is no v144 -- VS2022 spans 14.30-14.4x on v143, and VS2026 starts v145 at 14.5x.
function Convert-MobiusToolsetToArgument {
    param([string]$Toolset)

    if (-not $Toolset) { return '' }

    $Parts = $Toolset -split '\.'
    if ($Parts.Count -lt 2) { return "version=$Toolset" }

    $Major = [int]$Parts[0]
    $Minor = [int]$Parts[1]
    if ($Major -ne 14) { return "version=$Toolset" }

    if     ($Minor -lt 10) { $PlatformToolset = 'v140' }
    elseif ($Minor -lt 20) { $PlatformToolset = 'v141' }
    elseif ($Minor -lt 30) { $PlatformToolset = 'v142' }
    elseif ($Minor -lt 50) { $PlatformToolset = 'v143' }
    else                   { $PlatformToolset = 'v145' }

    return "$PlatformToolset,version=$Toolset"
}
