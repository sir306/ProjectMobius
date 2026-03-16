#!/usr/bin/env bash
# check-license-header.sh — Verify Source files contain the MIT license header.
#
# Checks that .h and .cpp files (excluding ThirdParty) contain the expected
# MIT license block. Files missing the header are reported as warnings.
#
# Exit code:
#   0 — all files have headers (or warnings emitted in warn-only mode)
#   1 — missing headers found (when STRICT=1)
#
# Usage:
#   bash .github/scripts/check-license-header.sh            # warn-only (default)
#   STRICT=1 bash .github/scripts/check-license-header.sh   # fail on missing headers

set -euo pipefail

STRICT="${STRICT:-0}"
MISSING=0
SOURCE_DIR="UnrealFolder/ProjectMobius/Source"

# The key phrase that must appear in the license header
LICENSE_MARKER="MIT License"

# Collect source files, excluding ThirdParty directories
mapfile -t FILES < <(find "$SOURCE_DIR" \( -name "*.h" -o -name "*.cpp" \) -not -path "*/ThirdParty/*" 2>/dev/null)

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "No source files found in $SOURCE_DIR"
  exit 0
fi

TOTAL=${#FILES[@]}

for file in "${FILES[@]}"; do
  # Check the first 30 lines for the license marker
  if ! head -30 "$file" | grep -q "$LICENSE_MARKER"; then
    MISSING=$((MISSING + 1))
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
      echo "::warning file=${file}::Missing MIT license header"
    else
      echo "WARNING: ${file}: Missing MIT license header"
    fi
  fi
done

PRESENT=$((TOTAL - MISSING))
echo ""
echo "License header check: ${PRESENT}/${TOTAL} files have MIT header (${MISSING} missing)"

if [[ $MISSING -gt 0 ]]; then
  echo ""
  echo "Expected header (first 30 lines of each file should contain):"
  echo '  /**'
  echo '   * MIT License'
  echo '   * Copyright (c) 2025 ProjectMobius contributors'
  echo '   * Nicholas R. Harding and Peter Thompson'
  echo '   * ...'
  echo '   */'
  if [[ "$STRICT" == "1" ]]; then
    echo ""
    echo "STRICT mode: failing due to missing headers"
    exit 1
  fi
fi

exit 0
