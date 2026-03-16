#!/usr/bin/env bash
# check-ue-naming.sh — Verify Unreal Engine naming conventions in header files.
#
# Checks UE-macro-decorated types only (UCLASS, USTRUCT, UINTERFACE, UPROPERTY)
# to avoid false positives on plain C++ classes.
#
# Exit code:
#   0 — all checks passed (or warnings emitted in warn-only mode)
#   1 — violations found (when STRICT=1)
#
# Usage:
#   bash .github/scripts/check-ue-naming.sh            # warn-only (default)
#   STRICT=1 bash .github/scripts/check-ue-naming.sh   # fail on violations

set -euo pipefail

STRICT="${STRICT:-0}"
VIOLATIONS=0
SOURCE_DIR="UnrealFolder/ProjectMobius/Source"

# Collect header files, excluding ThirdParty directories
mapfile -t HEADERS < <(find "$SOURCE_DIR" -name "*.h" -not -path "*/ThirdParty/*" 2>/dev/null)

if [[ ${#HEADERS[@]} -eq 0 ]]; then
  echo "No header files found in $SOURCE_DIR"
  exit 0
fi

warn() {
  local file="$1" line="$2" msg="$3"
  VIOLATIONS=$((VIOLATIONS + 1))
  if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    echo "::warning file=${file},line=${line}::${msg}"
  else
    echo "WARNING: ${file}:${line}: ${msg}"
  fi
}

for header in "${HEADERS[@]}"; do
  line_num=0
  in_uclass=0
  in_ustruct=0
  in_uinterface=0
  uproperty_line=0

  while IFS= read -r line || [[ -n "$line" ]]; do
    line_num=$((line_num + 1))

    # Detect UE macro decorators
    if [[ "$line" =~ ^[[:space:]]*UCLASS\( ]]; then
      in_uclass=1
      continue
    fi

    if [[ "$line" =~ ^[[:space:]]*USTRUCT\( ]]; then
      in_ustruct=1
      continue
    fi

    if [[ "$line" =~ ^[[:space:]]*UINTERFACE\( ]]; then
      in_uinterface=1
      continue
    fi

    if [[ "$line" =~ ^[[:space:]]*UPROPERTY\( ]]; then
      uproperty_line=$line_num
      continue
    fi

    # Check UCLASS-decorated class names
    if [[ $in_uclass -eq 1 ]]; then
      # Match: class MODULENAME_API ClassName : public AActor
      if [[ "$line" =~ class[[:space:]]+([A-Z_]+_API[[:space:]]+)?([A-Za-z0-9_]+)[[:space:]]*:[[:space:]]*public[[:space:]]+AActor ]]; then
        class_name="${BASH_REMATCH[2]}"
        if [[ ! "$class_name" =~ ^A[A-Z] ]]; then
          warn "$header" "$line_num" "Actor class '$class_name' should have 'A' prefix (e.g., A${class_name})"
        fi
        in_uclass=0
        continue
      fi
      # Match: class MODULENAME_API ClassName : public UObject/USomething
      if [[ "$line" =~ class[[:space:]]+([A-Z_]+_API[[:space:]]+)?([A-Za-z0-9_]+)[[:space:]]*:[[:space:]]*public[[:space:]]+U[A-Za-z] ]]; then
        class_name="${BASH_REMATCH[2]}"
        if [[ ! "$class_name" =~ ^U[A-Z] ]]; then
          warn "$header" "$line_num" "UObject class '$class_name' should have 'U' prefix (e.g., U${class_name})"
        fi
        in_uclass=0
        continue
      fi
      # Match: class MODULENAME_API ClassName : public SCompoundWidget/SPanel/etc.
      if [[ "$line" =~ class[[:space:]]+([A-Z_]+_API[[:space:]]+)?([A-Za-z0-9_]+)[[:space:]]*:[[:space:]]*public[[:space:]]+S[A-Za-z] ]]; then
        class_name="${BASH_REMATCH[2]}"
        if [[ ! "$class_name" =~ ^S[A-Z] ]]; then
          warn "$header" "$line_num" "Slate widget class '$class_name' should have 'S' prefix (e.g., S${class_name})"
        fi
        in_uclass=0
        continue
      fi
      # Generic class line (no parent match) — reset
      if [[ "$line" =~ ^[[:space:]]*class[[:space:]] ]]; then
        in_uclass=0
        continue
      fi
    fi

    # Check USTRUCT-decorated struct names
    if [[ $in_ustruct -eq 1 ]]; then
      if [[ "$line" =~ ^[[:space:]]*struct[[:space:]]+([A-Z_]+_API[[:space:]]+)?([A-Za-z0-9_]+) ]]; then
        struct_name="${BASH_REMATCH[2]}"
        # Skip GENERATED_BODY line
        if [[ "$struct_name" == "GENERATED_BODY" ]]; then
          continue
        fi
        if [[ ! "$struct_name" =~ ^F[A-Z] ]]; then
          warn "$header" "$line_num" "USTRUCT '$struct_name' should have 'F' prefix (e.g., F${struct_name})"
        fi
        in_ustruct=0
        continue
      fi
    fi

    # Check UINTERFACE-decorated interface names
    if [[ $in_uinterface -eq 1 ]]; then
      if [[ "$line" =~ ^[[:space:]]*class[[:space:]]+([A-Z_]+_API[[:space:]]+)?([A-Za-z0-9_]+) ]]; then
        iface_name="${BASH_REMATCH[2]}"
        if [[ ! "$iface_name" =~ ^U[A-Z] ]]; then
          warn "$header" "$line_num" "UINTERFACE stub class '$iface_name' should have 'U' prefix (e.g., U${iface_name})"
        fi
        in_uinterface=0
        continue
      fi
    fi

    # Check UPROPERTY bool naming (bSomething)
    if [[ $uproperty_line -gt 0 ]]; then
      # Match bool member declarations (skip operator overloads)
      if [[ "$line" =~ ^[[:space:]]*bool[[:space:]]+([A-Za-z0-9_]+) ]]; then
        bool_name="${BASH_REMATCH[1]}"
        # Skip operator overloads like "bool operator==()"
        if [[ "$bool_name" == "operator" ]]; then
          uproperty_line=0
          continue
        fi
        if [[ ! "$bool_name" =~ ^b[A-Z] ]]; then
          warn "$header" "$line_num" "UPROPERTY bool '$bool_name' should have 'b' prefix (e.g., b${bool_name^})"
        fi
        uproperty_line=0
        continue
      fi
      # If we hit a non-bool declaration after UPROPERTY, reset
      if [[ "$line" =~ ^[[:space:]]*(int|float|double|FString|FName|FText|FVector|FRotator|FTransform|TArray|TMap|TSet|TSubclassOf|TSoftObjectPtr|class|struct|enum|U[A-Z]|A[A-Z]|F[A-Z]|E[A-Z]) ]]; then
        uproperty_line=0
        continue
      fi
      # Multi-line UPROPERTY — keep looking for a few lines, then reset
      if [[ $((line_num - uproperty_line)) -gt 3 ]]; then
        uproperty_line=0
      fi
    fi

  done < "$header"
done

echo ""
if [[ $VIOLATIONS -eq 0 ]]; then
  echo "UE naming check: all checks passed"
else
  echo "UE naming check: $VIOLATIONS warning(s) found"
  if [[ "$STRICT" == "1" ]]; then
    echo "STRICT mode: failing due to violations"
    exit 1
  fi
fi

exit 0
