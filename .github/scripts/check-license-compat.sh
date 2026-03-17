#!/usr/bin/env bash
# check-license-compat.sh — Detect incompatible licenses in the repository.
#
# Scans LICENSE/COPYING files and source headers for copyleft or otherwise
# MIT-incompatible licenses. The project is MIT-licensed; any dependency
# carrying GPL, AGPL, LGPL, CC-BY-SA, CC-BY-NC, or SSPL is flagged.
#
# Exit code:
#   0 — no incompatible licenses found
#   1 — incompatible license detected (always enforced)
#
# Usage:
#   bash .github/scripts/check-license-compat.sh

set -euo pipefail

REPO_ROOT="${REPO_ROOT:-.}"
VIOLATIONS=0
WARNINGS=0

# ── Incompatible license patterns (case-insensitive grep -E) ──────────────
# Each pattern is paired with a human-readable label.
declare -a BLOCKED_PATTERNS=(
  "GNU General Public License|GPL-[23]|GPLv[23]"
  "GNU Affero|AGPL"
  "GNU Lesser General Public License|LGPL"
  "Server Side Public License|SSPL"
  "Creative Commons.*ShareAlike|CC-BY-SA|CC BY-SA"
  "Creative Commons.*NonCommercial|CC-BY-NC|CC BY-NC"
  "Creative Commons.*NoDerivs|CC-BY-ND|CC BY-ND"
)

declare -a BLOCKED_LABELS=(
  "GPL (copyleft)"
  "AGPL (network copyleft)"
  "LGPL (weak copyleft)"
  "SSPL (copyleft)"
  "CC-BY-SA (share-alike)"
  "CC-BY-NC (non-commercial)"
  "CC-BY-ND (no derivatives)"
)

# ── Directories to skip (vendored UE content, build artifacts) ────────────
EXCLUDE_DIRS=(
  ".git"
  "_superbuild"
  "Intermediate"
  "Saved"
  "DerivedDataCache"
  "Binaries"
  "node_modules"
  "build"
)

build_find_excludes() {
  local excludes=""
  for dir in "${EXCLUDE_DIRS[@]}"; do
    excludes+=" -not -path '*/${dir}/*'"
  done
  echo "$excludes"
}

# ── Helper: emit annotation or plain text ─────────────────────────────────
report() {
  local level="$1" file="$2" msg="$3"
  if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    echo "::${level} file=${file}::${msg}"
  else
    echo "${level^^}: ${file}: ${msg}"
  fi
}

# ── 1. Scan LICENSE / COPYING / NOTICE files ──────────────────────────────
echo "=== Scanning license files ==="

LICENSE_FILES=()
while IFS= read -r -d '' f; do
  LICENSE_FILES+=("$f")
done < <(eval "find '$REPO_ROOT' -type f \
  \\( -iname 'LICENSE' -o -iname 'LICENSE.*' -o -iname 'LICENCE' -o -iname 'LICENCE.*' \
     -o -iname 'COPYING' -o -iname 'COPYING.*' \\) \
  $(build_find_excludes) -print0 2>/dev/null")

echo "Found ${#LICENSE_FILES[@]} license file(s)"

for lf in "${LICENSE_FILES[@]}"; do
  for i in "${!BLOCKED_PATTERNS[@]}"; do
    if grep -qiE "${BLOCKED_PATTERNS[$i]}" "$lf" 2>/dev/null; then
      # Exception: LGPL/GPL mentioned only in "not under GPL" disclaimer context
      # Check if it's a genuine grant vs. a negation/disclaimer
      match_line=$(grep -iE "${BLOCKED_PATTERNS[$i]}" "$lf" | head -1)
      if echo "$match_line" | grep -qiE "not.*under|dual.licen|or later.*at your option|exception|linking exception"; then
        report "warning" "$lf" "Mentions ${BLOCKED_LABELS[$i]} but appears to be a disclaimer/exception — review manually"
        WARNINGS=$((WARNINGS + 1))
      else
        report "error" "$lf" "Incompatible license detected: ${BLOCKED_LABELS[$i]}"
        VIOLATIONS=$((VIOLATIONS + 1))
      fi
    fi
  done
done

# ── 2. Scan source file headers for embedded license grants ──────────────
echo ""
echo "=== Scanning source file headers (first 40 lines) ==="

SOURCE_FILES=()
while IFS= read -r -d '' f; do
  SOURCE_FILES+=("$f")
done < <(eval "find '$REPO_ROOT' -type f \
  \\( -name '*.h' -o -name '*.cpp' -o -name '*.c' -o -name '*.hpp' \
     -o -name '*.py' -o -name '*.cmake' -o -name 'CMakeLists.txt' \\) \
  -not -path '*/ThirdParty/*' \
  -not -path '*/assimp/*' \
  -not -path '*/hdf5*/*' \
  -not -path '*/OpenCV*/*' \
  $(build_find_excludes) -print0 2>/dev/null")

echo "Scanning ${#SOURCE_FILES[@]} source file(s)"

for sf in "${SOURCE_FILES[@]}"; do
  header=$(head -40 "$sf" 2>/dev/null || true)
  for i in "${!BLOCKED_PATTERNS[@]}"; do
    if echo "$header" | grep -qiE "${BLOCKED_PATTERNS[$i]}" 2>/dev/null; then
      report "error" "$sf" "Source file header contains ${BLOCKED_LABELS[$i]} license grant"
      VIOLATIONS=$((VIOLATIONS + 1))
    fi
  done
done

# ── 3. Check for new LICENSE files in ThirdParty directories ──────────────
echo ""
echo "=== Scanning ThirdParty license files ==="

TP_LICENSE_FILES=()
while IFS= read -r -d '' f; do
  TP_LICENSE_FILES+=("$f")
done < <(eval "find '$REPO_ROOT' -type f -path '*/ThirdParty/*' \
  \\( -iname 'LICENSE' -o -iname 'LICENSE.*' -o -iname 'LICENCE' -o -iname 'LICENCE.*' \
     -o -iname 'COPYING' -o -iname 'COPYING.*' \\) \
  $(build_find_excludes) -print0 2>/dev/null")

echo "Found ${#TP_LICENSE_FILES[@]} third-party license file(s)"

for lf in "${TP_LICENSE_FILES[@]}"; do
  for i in "${!BLOCKED_PATTERNS[@]}"; do
    if grep -qiE "${BLOCKED_PATTERNS[$i]}" "$lf" 2>/dev/null; then
      match_line=$(grep -iE "${BLOCKED_PATTERNS[$i]}" "$lf" | head -1)
      if echo "$match_line" | grep -qiE "not.*under|dual.licen|or later.*at your option|exception|linking exception"; then
        report "warning" "$lf" "Third-party mentions ${BLOCKED_LABELS[$i]} but may be a disclaimer — review manually"
        WARNINGS=$((WARNINGS + 1))
      else
        report "error" "$lf" "Third-party dependency has incompatible license: ${BLOCKED_LABELS[$i]}"
        VIOLATIONS=$((VIOLATIONS + 1))
      fi
    fi
  done
done

# ── Summary ───────────────────────────────────────────────────────────────
echo ""
echo "========================================"
echo "License compatibility check complete"
echo "  Violations: ${VIOLATIONS}"
echo "  Warnings:   ${WARNINGS}"
echo "========================================"

if [[ $VIOLATIONS -gt 0 ]]; then
  echo ""
  echo "FAILED: ${VIOLATIONS} incompatible license(s) found."
  echo ""
  echo "This project is MIT-licensed. The following license families are incompatible:"
  echo "  - GPL / GPLv2 / GPLv3  (copyleft)"
  echo "  - AGPL                 (network copyleft)"
  echo "  - LGPL                 (weak copyleft — may be OK for dynamic linking, review needed)"
  echo "  - SSPL                 (copyleft)"
  echo "  - CC-BY-SA             (share-alike)"
  echo "  - CC-BY-NC             (non-commercial)"
  echo "  - CC-BY-ND             (no derivatives)"
  echo ""
  echo "Compatible licenses include: MIT, BSD-2-Clause, BSD-3-Clause, ISC,"
  echo "Apache-2.0, Zlib, WTFPL, Unlicense, CC0, CC-BY-4.0, HDF5 (BSD-style)."
  exit 1
fi

if [[ $WARNINGS -gt 0 ]]; then
  echo ""
  echo "PASSED with warnings — please review flagged files manually."
fi

exit 0
