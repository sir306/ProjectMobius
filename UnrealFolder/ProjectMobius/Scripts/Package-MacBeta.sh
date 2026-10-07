#!/usr/bin/env bash
# ============================================================================
# Package-MacBeta.sh
# Builds, cooks and stages a macOS (Apple Silicon) Development build, then wraps
# it in a DMG and a zip for beta testers. The app is ad-hoc signed (bMacSignToRunLocally),
# not Developer ID signed or notarized: testers clear Gatekeeper once, as the
# bundled "READ ME FIRST" explains.
#
# Usage:  ./Scripts/Package-MacBeta.sh [output-dir]
#         UE_ROOT=/path/to/UE_5.5 ./Scripts/Package-MacBeta.sh
#
# Default output: Saved/Dist/MobiusViewer-macOS-beta-<date>/
#
# Run python3 superbuild.py once first; this script does not build Assimp,
# HDF5 or the IFC bridge.
# ============================================================================

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
UPROJECT="$PROJECT_DIR/ProjectMobius.uproject"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.5}"
RUNUAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
BUILD_DATE="$(date +%Y-%m-%d)"
OUT_DIR="${1:-$PROJECT_DIR/Saved/Dist/MobiusViewer-macOS-beta-$BUILD_DATE}"
APP_NAME="Mobius Viewer.app"
DMG_NAME="MobiusViewer-macOS-arm64-beta-$BUILD_DATE.dmg"
STAGED_APP="$PROJECT_DIR/Saved/StagedBuilds/Mac/ProjectMobius.app"

if [[ ! -x "$RUNUAT" ]]; then
    echo "ERROR: RunUAT not found at $RUNUAT (set UE_ROOT)." >&2
    exit 1
fi

# UE 5.5's -archive step copies Binaries/Mac/ProjectMobius.app, which Xcode fills during the BUILD
# step from whatever was staged LAST time - so an archived build ships the previous run's paks (or
# none at all on a first run). The freshly staged bundle is the one to ship, so -archive is not used,
# and the stale bundle is removed so nothing can pick it up by mistake.
rm -rf "$PROJECT_DIR/Binaries/Mac/ProjectMobius.app"

"$RUNUAT" BuildCookRun \
    -project="$UPROJECT" -noP4 -platform=Mac -clientconfig=Development \
    -build -cook -stage -pak -iostore -unattended -utf8output

echo "== Verifying the staged bundle's signature"
codesign --verify --deep --strict "$STAGED_APP"

echo "== Assembling $OUT_DIR"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/staging"
ditto "$STAGED_APP" "$OUT_DIR/staging/$APP_NAME"
xattr -cr "$OUT_DIR/staging/$APP_NAME"
codesign --verify --deep --strict "$OUT_DIR/staging/$APP_NAME"
sed "s/{{BUILD_DATE}}/$BUILD_DATE/" "$PROJECT_DIR/Scripts/MacBeta/READ ME FIRST - Installing the beta.txt" \
    > "$OUT_DIR/staging/READ ME FIRST - Installing the beta.txt"
ln -s /Applications "$OUT_DIR/staging/Applications"

echo "== Creating $DMG_NAME"
hdiutil create -volname "Mobius Viewer Beta" -srcfolder "$OUT_DIR/staging" -fs HFS+ -format ULFO -ov \
    "$OUT_DIR/$DMG_NAME"
hdiutil verify "$OUT_DIR/$DMG_NAME"

# The zip carries the same app and note in one top-level folder, without the DMG's Applications link.
# ditto (not zip) so the bundle's symlinks and permissions survive and the signature still verifies.
ZIP_DIR="Mobius Viewer Beta $BUILD_DATE"
ZIP_NAME="MobiusViewer-macOS-arm64-beta-$BUILD_DATE.zip"
echo "== Creating $ZIP_NAME"
rm -rf "$OUT_DIR/zipstaging"
mkdir -p "$OUT_DIR/zipstaging/$ZIP_DIR"
ditto "$OUT_DIR/staging/$APP_NAME" "$OUT_DIR/zipstaging/$ZIP_DIR/$APP_NAME"
cp "$OUT_DIR/staging/READ ME FIRST - Installing the beta.txt" "$OUT_DIR/zipstaging/$ZIP_DIR/"
ditto -c -k --keepParent "$OUT_DIR/zipstaging/$ZIP_DIR" "$OUT_DIR/$ZIP_NAME"
rm -rf "$OUT_DIR/zipstaging"

echo
for ARTIFACT in "$DMG_NAME" "$ZIP_NAME"; do
    echo "Done: $OUT_DIR/$ARTIFACT"
    du -h "$OUT_DIR/$ARTIFACT" | awk '{print "  Size:   " $1}'
    shasum -a 256 "$OUT_DIR/$ARTIFACT" | awk '{print "  SHA256: " $1}'
done
