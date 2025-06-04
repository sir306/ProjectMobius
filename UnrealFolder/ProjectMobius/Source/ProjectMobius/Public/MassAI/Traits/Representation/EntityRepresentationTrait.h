// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "EntityRepresentationTrait.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTMOBIUS_API UEntityRepresentationTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;


	/** Variables we can pass to the entity build to define the fragment */
	/** The Static Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentRepresentation")
	class UStaticMesh* AgentMesh;

	/** The Material that is used for the static mesh*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentRepresentation")
	class UMaterial* AgentMaterial;

	/** The Skeletal Mesh that is used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentRepresentation")
	class USkeletalMesh* AgentSkeletalMesh;

	///** The Skeletal Mesh Animation that is used for the skelatal mesh */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentRepresentation")
	//class UAnimInstance* AgentAnimation;
};
