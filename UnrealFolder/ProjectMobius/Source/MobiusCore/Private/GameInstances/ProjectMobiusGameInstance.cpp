// Fill out your copyright notice in the Description page of Project Settings.
/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.  
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,  
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL  
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR  
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING  
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS  
 * IN THE SOFTWARE.
 */

#include "GameInstances/ProjectMobiusGameInstance.h"
#include "Engine/DataTable.h"
#include "Subsystems/WebSocketSubsystem.h"

UProjectMobiusGameInstance::UProjectMobiusGameInstance():
	Super(),
	// TODO: Set default values for these variables -- talk to peter if particular values are needed that
	// could be set here as a default simulation
	TimeDilationScaleFactor(1.0f),
	MeshScale(1.0f),
	SimulationMovementScale(1.0f),
	// Set to 0.5f to ensure that the simulation runs at half speed for DEBUG
	PedestrianDataFilePath(TEXT("Click Browse to choose file")),
	PedestrianDataFileName(TEXT("Click Browse to choose file")),
	SimulationMeshFilePath(TEXT("Click Browse to choose file")),
	SimulationMeshFileName(TEXT("Click Browse to choose file"))
{
}

void UProjectMobiusGameInstance::Init()
{
	Super::Init();

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground"));
	if (CVar)
	{
		CVar->Set(0, ECVF_SetByCode);
	}
	CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TSR.ShadingRejection.Flickering"));
	if (CVar)
	{
		CVar->Set(0, ECVF_SetByCode);
	}
	CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mass.FullyParallel"));
	if (CVar)
	{
		CVar->Set(1, ECVF_SetByCode);
	}

}

void UProjectMobiusGameInstance::Shutdown()
{
	Super::Shutdown();
}

void UProjectMobiusGameInstance::SetPedestrianDataFilePath(const FString& NewPedestrianDataFilePath)
{
	// We only need to update and broadcast if the file path has changed
	if(PedestrianDataFileName != NewPedestrianDataFilePath)
	{
		PedestrianDataFilePath = NewPedestrianDataFilePath;
		OnPedestrianVectorFileChanged.Broadcast(NewPedestrianDataFilePath); // Broadcast the new pedestrian vector file
		OnPedestrianVectorFileUpdated.Broadcast();
	}

	// get the websocket subsystem
	if (auto WS_Sub = this->GetSubsystem<UWebSocketSubsystem>())
	{
		bool bDebug = false;
		// DEBUG Test Cases:
		if (bDebug)
		{
			// basic hello world message
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("hello"), TEXT("world"));
			WS_Sub->SendJsonMessage(Obj);
			
			// ─────────────────────────────────────────────────────────────────────────────
			// 1) appendPoint: {"action":"appendPoint","x":3.2,"y":14}
			// ─────────────────────────────────────────────────────────────────────────────
			{
				TSharedPtr<FJsonObject> Msg1 = MakeShared<FJsonObject>();
				Msg1->SetStringField(TEXT("action"), TEXT("appendPoint"));
				Msg1->SetNumberField(TEXT("x"), 3.2);
				Msg1->SetNumberField(TEXT("y"), 14.0);

				WS_Sub->SendJsonMessage(Msg1);
			}
		
			// delay for 5 seconds to give time to see changes
			FPlatformProcess::Sleep(5.0f);

			// ─────────────────────────────────────────────────────────────────────────────
			// 2) setData: {"action":"setData","points":[{"x":0,"y":10},{"x":1,"y":20}]}
			// ─────────────────────────────────────────────────────────────────────────────
			{
				TSharedPtr<FJsonObject> Msg2 = MakeShared<FJsonObject>();
				Msg2->SetStringField(TEXT("action"), TEXT("setData"));

				// build the points array
				TArray<TSharedPtr<FJsonValue>> PointsArray;
				{
					auto P0 = MakeShared<FJsonObject>();
					P0->SetNumberField(TEXT("x"), 0.0);
					P0->SetNumberField(TEXT("y"), 10.0);
					PointsArray.Add(MakeShared<FJsonValueObject>(P0));
				}
				{
					auto P1 = MakeShared<FJsonObject>();
					P1->SetNumberField(TEXT("x"), 1.0);
					P1->SetNumberField(TEXT("y"), 20.0);
					PointsArray.Add(MakeShared<FJsonValueObject>(P1));
				}

				Msg2->SetArrayField(TEXT("points"), PointsArray);
				WS_Sub->SendJsonMessage(Msg2);
			}

			// delay for 5 seconds to give time to see changes
			FPlatformProcess::Sleep(5.0f);

			// ─────────────────────────────────────────────────────────────────────────────
			// 3) appendPoint: {"action":"appendPoint","x":3.2,"y":14}
			// ─────────────────────────────────────────────────────────────────────────────
			{
				TSharedPtr<FJsonObject> Msg1 = MakeShared<FJsonObject>();
				Msg1->SetStringField(TEXT("action"), TEXT("appendPoint"));
				Msg1->SetNumberField(TEXT("x"), 3.2);
				Msg1->SetNumberField(TEXT("y"), 14.0);

				WS_Sub->SendJsonMessage(Msg1);
			}
		
			// delay for 5 seconds to give time to see changes
			FPlatformProcess::Sleep(5.0f);

			// ─────────────────────────────────────────────────────────────────────────────
			// 4) updateAxis: {"action":"updateAxis","xMin":0,"xMax":5,"yMin":0,"yMax":50}
			// ─────────────────────────────────────────────────────────────────────────────
			{
				TSharedPtr<FJsonObject> Msg3 = MakeShared<FJsonObject>();
				Msg3->SetStringField(TEXT("action"), TEXT("updateAxis"));
				Msg3->SetNumberField(TEXT("xMin"), 0.0);
				Msg3->SetNumberField(TEXT("xMax"), 300);
				Msg3->SetNumberField(TEXT("yMin"), 0.0);
				Msg3->SetNumberField(TEXT("yMax"), 1000);

				WS_Sub->SendJsonMessage(Msg3);
			}
			// delay for 5 seconds to give time to see previous change
			FPlatformProcess::Sleep(5.0f);

			// ─────────────────────────────────────────────────────────────────────────────
			// 5) updateAxisTitles: {"action":"updateAxis",
			//                       "xTitle":"Elapsed Time (s)",
			//                       "yTitle":"Occupants",
			//                       "xGridVisible":true,
			//                       "yGridVisible":false}
			// ─────────────────────────────────────────────────────────────────────────────
			{
				TSharedPtr<FJsonObject> Msg4 = MakeShared<FJsonObject>();
				Msg4->SetStringField(TEXT("action"),      TEXT("updateAxis"));
				Msg4->SetStringField(TEXT("xTitle"),      TEXT("Elapsed Time (s)"));
				Msg4->SetStringField(TEXT("yTitle"),      TEXT("Occupants"));
				Msg4->SetBoolField  (TEXT("xGridVisible"), true);
				Msg4->SetBoolField  (TEXT("yGridVisible"), false);

				WS_Sub->SendJsonMessage(Msg4);
			}
			// delay for 5 seconds to give time to see previous change
			FPlatformProcess::Sleep(5.0f);
		}

		// ─────────────────────────────────────────────────────────────────────────────
		// 6) updateChartTitle: {"action":"updateChartTitle","chartTitle":"My New Chart Heading"}
		// ─────────────────────────────────────────────────────────────────────────────
		{
			TSharedPtr<FJsonObject> Msg6 = MakeShared<FJsonObject>();
			Msg6->SetStringField(TEXT("action"),     TEXT("updateChartTitle"));
			Msg6->SetStringField(TEXT("chartTitle"), TEXT("Loading Pedestrian Data..."));

			WS_Sub->SendJsonMessage(Msg6);
		}
		// ─────────────────────────────────────────────────────────────────────────────
		// 7) resetData: {"action":"resetData"} - test that we can clear existing chart data
		// ─────────────────────────────────────────────────────────────────────────────
		{
			TSharedPtr<FJsonObject> Msg7 = MakeShared<FJsonObject>();
			Msg7->SetStringField(TEXT("action"), TEXT("resetData"));
			WS_Sub->SendJsonMessage(Msg7);
		}

	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WebSocketSubsystem not found!"));
	}
}

void UProjectMobiusGameInstance::SetPedestrianDataFileName(const FString& NewPedestrianDataFileName)
{
	PedestrianDataFileName = NewPedestrianDataFileName;
}

void UProjectMobiusGameInstance::SetSimulationMeshFilePath(const FString& NewSimulationMeshFilePath)
{
	if(SimulationMeshFilePath != NewSimulationMeshFilePath)
	{
		SimulationMeshFilePath = NewSimulationMeshFilePath;
		OnMeshFileChanged.Broadcast(); // Broadcast that the mesh file has changed
	}
}

void UProjectMobiusGameInstance::SetSimulationMeshFileName(const FString& NewSimulationMeshFileName)
{
	SimulationMeshFileName = NewSimulationMeshFileName;
}

void UProjectMobiusGameInstance::SetTimeDilationScaleFactor(const float NewTimeDilationScaleFactor)
{
	TimeDilationScaleFactor = NewTimeDilationScaleFactor;

	// Notify all listeners that the time dilation scale factor has changed
	OnTimeDilationScaleFactorChanged.Broadcast();
}

void UProjectMobiusGameInstance::SetMeshScale(const float NewMeshScale)
{
}

void UProjectMobiusGameInstance::SetSimulationMovementScale(const float NewSimulationMovementScale)
{
}

void UProjectMobiusGameInstance::SetGlobalScale(const float NewGlobalScale)
{
}

void UProjectMobiusGameInstance::SetDataLoadingState(const bool bNewLoadingState)
{
	if(bNewLoadingState != bIsDataBeingLoaded)
	{
		bIsDataBeingLoaded = bNewLoadingState;
		OnDataLoading.Broadcast(bIsDataBeingLoaded);
	}
}
