#!/usr/bin/env bash
# ============================================================================
# GenerateDatasmithMaterials.sh
# Generates RuntimeDatasmithOverrides materials from engine DatasmithRuntime
# sources by running the GenerateDatasmithMaterials commandlet.
#
# macOS / Linux equivalent of GenerateDatasmithMaterials.bat
# ============================================================================

set -euo pipefail

echo "============================================================"
echo " Generate Datasmith Override Materials"
echo "============================================================"

# ── Locate UE 5.5 install ─────────────────────────────────────────────────────

UE_EDITOR=""

# Try environment variable first
if [[ -n "${UE_5_5:-}" ]]; then
    if [[ "$(uname)" == "Darwin" ]]; then
        CANDIDATE="${UE_5_5}/Engine/Binaries/Mac/UnrealEditor-Cmd"
    else
        CANDIDATE="${UE_5_5}/Engine/Binaries/Linux/UnrealEditor-Cmd"
    fi
    if [[ -x "$CANDIDATE" ]]; then
        UE_EDITOR="$CANDIDATE"
        echo "Found UE via UE_5_5 environment variable."
    fi
fi

# Try common install paths
if [[ -z "$UE_EDITOR" ]]; then
    SEARCH_PATHS=()
    if [[ "$(uname)" == "Darwin" ]]; then
        SEARCH_PATHS=(
            "/Users/Shared/Epic Games/UE_5.5/Engine/Binaries/Mac/UnrealEditor-Cmd"
            "/Applications/Epic Games/UE_5.5/Engine/Binaries/Mac/UnrealEditor-Cmd"
        )
    else
        SEARCH_PATHS=(
            "/opt/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd"
            "/home/${USER:-}/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd"
        )
    fi

    for path in "${SEARCH_PATHS[@]}"; do
        if [[ -x "$path" ]]; then
            UE_EDITOR="$path"
            echo "Found UE at $(dirname "$(dirname "$(dirname "$(dirname "$path")")")")"
            break
        fi
    done
fi

if [[ -z "$UE_EDITOR" ]]; then
    echo "ERROR: Could not find Unreal Engine 5.5 installation."
    echo ""
    echo "Set the UE_5_5 environment variable to your UE 5.5 root directory, e.g.:"
    echo "  export UE_5_5=\"/Users/Shared/Epic Games/UE_5.5\""
    echo ""
    exit 1
fi

# ── Locate the .uproject ──────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
UPROJECT="$PROJECT_DIR/ProjectMobius.uproject"

if [[ ! -f "$UPROJECT" ]]; then
    echo "ERROR: Cannot find ProjectMobius.uproject at:"
    echo "  $UPROJECT"
    exit 1
fi

echo ""
echo "Editor:  $UE_EDITOR"
echo "Project: $UPROJECT"
echo ""

# ── Run the commandlet ─────────────────────────────────────────────────────────
# Note: -nullrhi is omitted — material compilation may need a rendering backend
# on macOS/Linux.

echo "Running GenerateDatasmithMaterials commandlet..."
echo ""

"$UE_EDITOR" "$UPROJECT" -run=GenerateDatasmithMaterials -unattended -nop4 -nosplash
EXIT_CODE=$?

echo ""
if [[ $EXIT_CODE -eq 0 ]]; then
    echo "============================================================"
    echo " SUCCESS: All Datasmith override materials generated."
    echo "============================================================"
else
    echo "============================================================"
    echo " FAILED: Commandlet returned error code $EXIT_CODE."
    echo " Check the log output above for details."
    echo "============================================================"
fi

exit $EXIT_CODE
