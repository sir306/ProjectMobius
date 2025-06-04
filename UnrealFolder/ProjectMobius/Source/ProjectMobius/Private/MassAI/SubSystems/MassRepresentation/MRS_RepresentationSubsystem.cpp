// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/SubSystems/MassRepresentation/MRS_RepresentationSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "MassAI/Actors/AgentRepresentationActorISM.h"
// Niagara
#include "NiagaraComponent.h"
#include "MassAI/Actors/NiagaraAgentRepActor.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "MassAI/Fragments/EntityInfoFragment.h"

UMRS_RepresentationSubsystem::UMRS_RepresentationSubsystem()
{
}

void UMRS_RepresentationSubsystem::SetPedestrianMaterial(UMaterialInstanceDynamic* MaterialInst, EPedestrianGender AgentGender)
{
	// For now we will just get the first agent representation actor ism and set the material
	// We will need to change this to get the correct agent representation actor ism and store a ptr to it?

	if(GetWorld())
	{
		// get all world actors of class
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass(), FoundActors);

		// if we have found actors get the first one and cast to the agent representation actor ism
		if(FoundActors.Num() > 0)
		{
			// if the agent representation actor ism is valid then we can set the material
			if(ANiagaraAgentRepActor* NiagaraAgentRepActor = Cast<ANiagaraAgentRepActor>(FoundActors[0]))
			{
				switch (AgentGender)
				{
				case EPedestrianGender::Epg_Male:
					{
						NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("MaleMaterialBody"), MaterialInst);
						//NiagaraAgentRepActor->GetMaleISMComponent()->SetMaterial(0, MaterialInst);

						// Update the mobius game instance with the selected material instance
						GetMobiusGameInstance(GetWorld())->SetSelectedMaleAdultMaterialInstance(MaterialInst);
						break;
					}
				case EPedestrianGender::Epg_Female:
					{
						NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("FemaleMaterialBody"), MaterialInst);
						//NiagaraAgentRepActor->GetFemaleISMComponent()->SetMaterial(0, MaterialInst);

						// Update the mobius game instance with the selected material instance
						GetMobiusGameInstance(GetWorld())->SetSelectedFemaleAdultMaterialInstance(MaterialInst);
						break;
					}
				case EPedestrianGender::Epg_Default:
					{
						// not supported
						break;
					}
				}
				//AgentRepresentationActorISM->GetMaleISMComponent()->MarkRenderTransformDirty()
				// NiagaraAgentRepActor->GetMaleISMComponent()->MarkRenderStateDirty();
				// NiagaraAgentRepActor->GetFemaleISMComponent()->MarkRenderStateDirty();
			}
		}
		else
		{
			switch (AgentGender)
			{
			case EPedestrianGender::Epg_Male:
				{
					// Update the mobius game instance with the selected material instance
					GetMobiusGameInstance(GetWorld())->SetSelectedMaleAdultMaterialInstance(MaterialInst);
					break;
				}
			case EPedestrianGender::Epg_Female:
				{
					// Update the mobius game instance with the selected material instance
					GetMobiusGameInstance(GetWorld())->SetSelectedFemaleAdultMaterialInstance(MaterialInst);
					break;
				}
			case EPedestrianGender::Epg_Default:
				{
					// not supported
					break;
				}
			}
			
		}
	}
	
}

void UMRS_RepresentationSubsystem::SetPedestrianMaterial(UMaterialInstanceDynamic* MaterialInstBody,
                                                         UMaterialInstanceDynamic* MaterialInstEyes, EPedestrianGender AgentGender)
{
	// For now we will just get the first agent representation actor ism and set the material
	// We will need to change this to get the correct agent representation actor ism and store a ptr to it?

	if(GetWorld())
	{
		// get all world actors of class
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass(), FoundActors);

		// if we have found actors get the first one and cast to the agent representation actor ism
		if(FoundActors.Num() > 0)
		{
			// if the agent representation actor ism is valid then we can set the material
			if(ANiagaraAgentRepActor* NiagaraAgentRepActor = Cast<ANiagaraAgentRepActor>(FoundActors[0]))
			{
				switch (AgentGender)
				{
				case EPedestrianGender::Epg_Male:
					{
						NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("MaleMaterialBody"), MaterialInstBody);
						NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("MaleMaterialEyes"), MaterialInstEyes);
						// AgentRepresentationActorISM->GetMaleISMComponent()->SetMaterial(0, MaterialInstBody);
						// AgentRepresentationActorISM->GetMaleISMComponent()->SetMaterial(1, MaterialInstEyes);

						// Update the mobius game instance with the selected material instance
						GetMobiusGameInstance(GetWorld())->SetSelectedMaleAdultMaterialInstance(MaterialInstBody);
						GetMobiusGameInstance(GetWorld())->SetSelectedMaleAdultEyesMaterialInstance(MaterialInstEyes);
						break;
					}
				case EPedestrianGender::Epg_Female:
					{
						NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("FemaleMaterialBody"), MaterialInstBody);
						NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("FemaleMaterialEyes"), MaterialInstEyes);

						// Update the mobius game instance with the selected material instance
						GetMobiusGameInstance(GetWorld())->SetSelectedFemaleAdultMaterialInstance(MaterialInstBody);
						GetMobiusGameInstance(GetWorld())->SetSelectedFemaleAdultEyesMaterialInstance(MaterialInstEyes);
						break;
					}
				case EPedestrianGender::Epg_Default:
					{
						// not supported
						break;
					}
				}
				//AgentRepresentationActorISM->GetMaleISMComponent()->MarkRenderTransformDirty()
				// AgentRepresentationActorISM->GetMaleISMComponent()->MarkRenderStateDirty();
				// AgentRepresentationActorISM->GetFemaleISMComponent()->MarkRenderStateDirty();
			}
		}
		else
		{
			switch (AgentGender)
			{
			case EPedestrianGender::Epg_Male:
				{
					// Update the mobius game instance with the selected material instance
					GetMobiusGameInstance(GetWorld())->SetSelectedMaleAdultMaterialInstance(MaterialInstBody);
					GetMobiusGameInstance(GetWorld())->SetSelectedMaleAdultEyesMaterialInstance(MaterialInstEyes);
					break;
				}
			case EPedestrianGender::Epg_Female:
				{
					// Update the mobius game instance with the selected material instance
					GetMobiusGameInstance(GetWorld())->SetSelectedFemaleAdultMaterialInstance(MaterialInstBody);
					GetMobiusGameInstance(GetWorld())->SetSelectedFemaleAdultEyesMaterialInstance(MaterialInstEyes);
					break;
				}
			case EPedestrianGender::Epg_Default:
				{
					// not supported
					break;
				}
			}
			
		}
	}
	
}

void UMRS_RepresentationSubsystem::SetPedestrianMaterial(UMaterialInstanceDynamic* MaterialInstBody,
                                                         UMaterialInstanceDynamic* MaterialInstEyes, EPedestrianGender AgentGender, EAgeDemographic AgentAgeDemographic)
{
	if(GetWorld())
	{
		// get all world actors of class
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANiagaraAgentRepActor::StaticClass(), FoundActors);

		// if we have found actors get the first one and cast to the agent representation actor ism
		if(FoundActors.Num() > 0)
		{
			// if the agent representation actor ism is valid then we can set the material
			if(ANiagaraAgentRepActor* NiagaraAgentRepActor = Cast<ANiagaraAgentRepActor>(FoundActors[0]))
			{
				switch (AgentAgeDemographic)
				{
				case EAgeDemographic::Ead_Adult:
					{
						SetPedestrianMaterial(MaterialInstBody,	MaterialInstEyes, AgentGender);
						break;
					}
				
				case EAgeDemographic::Ead_Elderly:
					{
						switch (AgentGender)
						{
						case EPedestrianGender::Epg_Male:
							// Update the Niagara component with the selected material instance
							NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ElderlyMaleMaterialBody"), MaterialInstBody);
							NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ElderlyMaleMaterialEyes"), MaterialInstEyes);
						// Update the mobius game instance with the selected material instance
							GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyMaterialInstance(MaterialInstBody);
							GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyEyesMaterialInstance(MaterialInstEyes);
							break;
						case EPedestrianGender::Epg_Female:
							// Update the Niagara component with the selected material instance
							NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ElderlyFemaleMaterialBody"), MaterialInstBody);
							NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ElderlyFemaleMaterialEyes"), MaterialInstEyes);
							// Update the mobius game instance with the selected material instance
							GetMobiusGameInstance(GetWorld())->SetSelectedFemaleElderlyMaterialInstance(MaterialInstBody);
							GetMobiusGameInstance(GetWorld())->SetSelectedFemaleElderlyEyesMaterialInstance(MaterialInstEyes);
							break;
						case EPedestrianGender::Epg_Default:
							// TODO: no seperate gender logic yet
							// Update the Niagara component with the selected material instance
							NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ElderlyMaleMaterialBody"), MaterialInstBody);
							NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ElderlyMaleMaterialEyes"), MaterialInstEyes);
							// Update the mobius game instance with the selected material instance
							GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyMaterialInstance(MaterialInstBody);
							GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyEyesMaterialInstance(MaterialInstEyes);
							break;
						}
						break;
					}
				case EAgeDemographic::Ead_Child:
					// Update the Niagara component with the selected material instance
					NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ChildrenMaterialBody"), MaterialInstBody);
					NiagaraAgentRepActor->GetNiagaraComponent()->SetVariableMaterial(TEXT("ChildrenMaterialEyes"), MaterialInstEyes);
				// Update the mobius game instance with the selected material instance
					GetMobiusGameInstance(GetWorld())->SetSelectedChildrenMaterialInstance(MaterialInstBody);
					GetMobiusGameInstance(GetWorld())->SetSelectedChildrenEyesMaterialInstance(MaterialInstEyes);
					break;

					
				case EAgeDemographic::Ead_Default:
					break;
				default: ;
				}
			}
		}
		else
		{
			switch (AgentAgeDemographic)
			{
			case EAgeDemographic::Ead_Adult:
				{
					SetPedestrianMaterial(MaterialInstBody,	MaterialInstEyes, AgentGender);
					break;
				}
				
			case EAgeDemographic::Ead_Elderly:
				{
					switch (AgentGender)
					{
					case EPedestrianGender::Epg_Male:
						// Update the mobius game instance with the selected material instance
						GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyMaterialInstance(MaterialInstBody);
						GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyEyesMaterialInstance(MaterialInstEyes);
						break;
					case EPedestrianGender::Epg_Female:
						// Update the mobius game instance with the selected material instance
						GetMobiusGameInstance(GetWorld())->SetSelectedFemaleElderlyMaterialInstance(MaterialInstBody);
						GetMobiusGameInstance(GetWorld())->SetSelectedFemaleElderlyEyesMaterialInstance(MaterialInstEyes);
						break;
					case EPedestrianGender::Epg_Default:
						// Update the mobius game instance with the selected material instance
						GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyMaterialInstance(MaterialInstBody);
						GetMobiusGameInstance(GetWorld())->SetSelectedMaleElderlyEyesMaterialInstance(MaterialInstEyes);
						break;
					}
					break;
				}
			case EAgeDemographic::Ead_Child:
				// Update the mobius game instance with the selected material instance
				GetMobiusGameInstance(GetWorld())->SetSelectedChildrenMaterialInstance(MaterialInstBody);
				GetMobiusGameInstance(GetWorld())->SetSelectedChildrenEyesMaterialInstance(MaterialInstEyes);
				break;

					
			case EAgeDemographic::Ead_Default:
				break;
			default: ;
			}
		}
	}
	
}

UMaterialInstanceDynamic* UMRS_RepresentationSubsystem::GetSelectedMaleMaterialInstance(UWorld* World)
{
	if(GetMobiusGameInstance(World)->GetSelectedMaleMaterialInstance() == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("THIS IS NULL GRRR!"));
	}
	
	return GetMobiusGameInstance(GetWorld())->GetSelectedMaleMaterialInstance();
}

UMaterialInstanceDynamic* UMRS_RepresentationSubsystem::GetSelectedMaleEyesMaterialInstance(UWorld* World)
{
	return GetMobiusGameInstance(GetWorld())->GetSelectedMaleEyesMaterialInstance();
}

UMaterialInstanceDynamic* UMRS_RepresentationSubsystem::GetSelectedFemaleMaterialInstance()
{
	return GetMobiusGameInstance(GetWorld())->GetSelectedFemaleMaterialInstance();
}

UMaterialInstanceDynamic* UMRS_RepresentationSubsystem::GetSelectedFemaleEyesMaterialInstance()
{
	return GetMobiusGameInstance(GetWorld())->GetSelectedFemaleEyesMaterialInstance();
}

FVatMovementFrames UMRS_RepresentationSubsystem::CalculateMovementFrames(float CurrentSpeed, float AgentMaxSpeed, float& StepsPerSecond)
{
	// Fast loop through the GaitSpeedBands, testing CurrentSpeed against the HighVal, in ascending order, to assign the MovementBracket
	int iBracket;
	for (iBracket = 0; iBracket < sizeof(AvatarGaitSpeedBands) / sizeof(FVatMovementFrames); iBracket++) {
		if (CurrentSpeed < AvatarGaitSpeedBands[iBracket].HighSpeed) {
			break;
		}
	}

	StepsPerSecond = CurrentSpeed / AvatarGaitSpeedBands[iBracket].AnimatedStepLength;

	return AvatarGaitSpeedBands[iBracket];
}

FVatMovementFrames UMRS_RepresentationSubsystem::GetMovementFrames(EPedestrianMovementBracket MovementBracket)
{
	switch (MovementBracket)
	{
	case EPedestrianMovementBracket::Emb_NotMoving:
		{
			return AvatarGaitSpeedBands[0];
		}
	case EPedestrianMovementBracket::Emb_Shuffle:
		{
			return AvatarGaitSpeedBands[1];
		}
	case EPedestrianMovementBracket::Emb_SlowWalk:
		{
			return AvatarGaitSpeedBands[2];
		}
	case EPedestrianMovementBracket::Emb_Walk:
		{
			return AvatarGaitSpeedBands[3];
		}
	case EPedestrianMovementBracket::Emb_BriskWalk:
		{
			return AvatarGaitSpeedBands[4];
		}
	case EPedestrianMovementBracket::Emb_Error:
		{
			return AvatarGaitSpeedBands[5];
		}
	}
	return AvatarGaitSpeedBands[5];
}

void UMRS_RepresentationSubsystem::SetMaxRenderHeight(float NewMaxRenderHeight)
{
	MaxRenderHeight = NewMaxRenderHeight;
}

void UMRS_RepresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
}

void UMRS_RepresentationSubsystem::Deinitialize()
{
}
