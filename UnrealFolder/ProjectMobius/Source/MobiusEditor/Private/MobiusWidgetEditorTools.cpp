// Fill out your copyright notice in the Description page of Project Settings.

#include "MobiusWidgetEditorTools.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

namespace MobiusWidgetEditorToolsLocal
{
	static UWidgetBlueprint* LoadWidgetBlueprint(const FString& Path, FString& OutError)
	{
		// Accept either "/Game/Dir/WBP_Foo" or a full "/Game/Dir/WBP_Foo.WBP_Foo" object path.
		FString ObjectPath = Path;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			FString AssetName;
			Path.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			ObjectPath = Path + TEXT(".") + AssetName;
		}
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		if (!WidgetBlueprint)
		{
			OutError = FString::Printf(TEXT("could not load WidgetBlueprint '%s'"), *ObjectPath);
		}
		return WidgetBlueprint;
	}

	static UWidget* FindWidget(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName, FString& OutError)
	{
		if (!WidgetBlueprint->WidgetTree)
		{
			OutError = TEXT("WidgetBlueprint has no WidgetTree");
			return nullptr;
		}
		UWidget* Found = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
		if (!Found)
		{
			OutError = FString::Printf(TEXT("no widget named '%s' in %s"), *WidgetName, *WidgetBlueprint->GetName());
		}
		return Found;
	}
}

bool UMobiusWidgetEditorTools::ReplaceWidgetClass(const FString& WidgetBlueprintPath, const FString& WidgetName,
                                                  TSubclassOf<UWidget> NewWidgetClass, FString& OutError)
{
	using namespace MobiusWidgetEditorToolsLocal;
	OutError.Empty();

	if (!NewWidgetClass)
	{
		OutError = TEXT("NewWidgetClass is null");
		return false;
	}
	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath, OutError);
	if (!WidgetBlueprint)
	{
		return false;
	}
	UWidget* OldWidget = FindWidget(WidgetBlueprint, WidgetName, OutError);
	if (!OldWidget)
	{
		return false;
	}
	if (OldWidget->GetClass() == NewWidgetClass)
	{
		return true; // already migrated — idempotent, so a re-run of the batch is safe
	}
	// Only ever widen along the existing hierarchy. A sideways swap would silently drop every property
	// the two classes do not share, which on a Border means radii, outline width and padding.
	if (!NewWidgetClass->IsChildOf(OldWidget->GetClass()))
	{
		OutError = FString::Printf(TEXT("'%s' is not a subclass of the current class '%s'"),
			*NewWidgetClass->GetName(), *OldWidget->GetClass()->GetName());
		return false;
	}

	UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
	UPanelWidget* ParentPanel = OldWidget->GetParent();
	const bool bIsRoot = (Tree->RootWidget == OldWidget);

	// Construct under a temporary name: the old widget still owns WidgetName until it is detached, and
	// two objects with the same name in the same outer is a rename failure, not an overwrite.
	const FName FinalName = OldWidget->GetFName();
	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(NewWidgetClass,
		MakeUniqueObjectName(Tree, NewWidgetClass, *(WidgetName + TEXT("_MobiusMigrating"))));
	if (!NewWidget)
	{
		OutError = TEXT("ConstructWidget failed");
		return false;
	}

	// Copy everything that still matches by name+type. Because NewWidgetClass derives from the old class,
	// this carries the entire authored UBorder state — Background brush (radii, outline width, DrawAs,
	// margins), BrushColor, Padding, HAlign/VAlign, Visibility, RenderTransform, ToolTip, bIsVariable.
	UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyParams;
	CopyParams.bNotifyObjectReplacement = true;
	CopyParams.bClearReferences = false;
	UEngine::CopyPropertiesForUnrelatedObjects(OldWidget, NewWidget, CopyParams);

	// Move the child across BEFORE the swap. UBorder is a UContentWidget, so it owns exactly one child and
	// that child is not reachable from the parent panel — ReplaceChild alone would orphan it.
	if (UContentWidget* OldContent = Cast<UContentWidget>(OldWidget))
	{
		if (UContentWidget* NewContent = Cast<UContentWidget>(NewWidget))
		{
			if (UWidget* Child = OldContent->GetContentSlot() ? OldContent->GetContentSlot()->Content : nullptr)
			{
				OldContent->ClearChildren();
				UPanelSlot* MovedSlot = NewContent->SetContent(Child);
				// SetContent builds a fresh slot; carry the authored padding/alignment over so the
				// content does not jump. Property copy by name, same reasoning as above.
				if (MovedSlot && OldContent->GetContentSlot())
				{
					UEngine::CopyPropertiesForUnrelatedObjects(OldContent->GetContentSlot(), MovedSlot, CopyParams);
				}
			}
		}
	}

	if (bIsRoot)
	{
		Tree->RootWidget = NewWidget;
	}
	else if (ParentPanel)
	{
		// ReplaceChild reuses the EXISTING slot object and just repoints its Content, which is precisely
		// why it is used here instead of Remove+Add: all parent-slot layout data (canvas anchors/offsets,
		// grid row/column, box fill rules) survives untouched.
		if (!ParentPanel->ReplaceChild(OldWidget, NewWidget))
		{
			OutError = FString::Printf(TEXT("ReplaceChild failed for '%s' under '%s'"),
				*WidgetName, *ParentPanel->GetName());
			return false;
		}
	}
	else
	{
		OutError = FString::Printf(TEXT("'%s' has no parent and is not the root"), *WidgetName);
		return false;
	}

	// Free the name, then claim it — BindWidget, designer references and the A6b role map all key on it.
	OldWidget->Rename(*MakeUniqueObjectName(Tree, OldWidget->GetClass(), *(WidgetName + TEXT("_MobiusOld"))).ToString(),
		nullptr, REN_DontCreateRedirectors);
	NewWidget->Rename(*FinalName.ToString(), nullptr, REN_DontCreateRedirectors);

	Tree->Modify();
	WidgetBlueprint->Modify();
	return true;
}

bool UMobiusWidgetEditorTools::CompileAndSaveWidgetBlueprint(const FString& WidgetBlueprintPath, FString& OutError)
{
	using namespace MobiusWidgetEditorToolsLocal;
	OutError.Empty();

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath, OutError);
	if (!WidgetBlueprint)
	{
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);

	UPackage* Package = WidgetBlueprint->GetOutermost();
	Package->MarkPackageDirty();
	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("SavePackage failed for '%s'"), *FileName);
		return false;
	}
	return true;
}

bool UMobiusWidgetEditorTools::SetWidgetPropertyText(const FString& WidgetBlueprintPath, const FString& WidgetName,
                                                     const FString& PropertyName, const FString& ValueAsText,
                                                     FString& OutError)
{
	using namespace MobiusWidgetEditorToolsLocal;
	OutError.Empty();

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath, OutError);
	if (!WidgetBlueprint)
	{
		return false;
	}
	UWidget* Widget = FindWidget(WidgetBlueprint, WidgetName, OutError);
	if (!Widget)
	{
		return false;
	}
	FProperty* Property = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		OutError = FString::Printf(TEXT("'%s' has no property '%s'"), *Widget->GetClass()->GetName(), *PropertyName);
		return false;
	}
	Widget->Modify();
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Widget);
	// ImportText handles enum names, bools and numerics uniformly — no per-type overloads to keep in sync.
	if (!Property->ImportText_Direct(*ValueAsText, ValuePtr, Widget, PPF_None))
	{
		OutError = FString::Printf(TEXT("could not parse '%s' for property '%s'"), *ValueAsText, *PropertyName);
		return false;
	}
	return true;
}

bool UMobiusWidgetEditorTools::GetWidgetPropertyText(const FString& WidgetBlueprintPath, const FString& WidgetName,
                                                     const FString& PropertyName, FString& OutValue, FString& OutError)
{
	using namespace MobiusWidgetEditorToolsLocal;
	OutError.Empty();
	OutValue.Empty();

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath, OutError);
	if (!WidgetBlueprint)
	{
		return false;
	}
	UWidget* Widget = FindWidget(WidgetBlueprint, WidgetName, OutError);
	if (!Widget)
	{
		return false;
	}
	FProperty* Property = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		OutError = FString::Printf(TEXT("'%s' has no property '%s'"), *Widget->GetClass()->GetName(), *PropertyName);
		return false;
	}
	Property->ExportText_Direct(OutValue, Property->ContainerPtrToValuePtr<void>(Widget), nullptr, Widget, PPF_None);
	return true;
}
