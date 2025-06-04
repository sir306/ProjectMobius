// Fill out your copyright notice in the Description page of Project Settings.


#include "MassAI/Fragments/SimulationTimeStepFragment.h"

FSimulationTimeStepFragment::FSimulationTimeStepFragment():
	CurrentTimeStep(0)
{
}

FSimulationTimeStepFragment::FSimulationTimeStepFragment(int32 StartingTimeStep)
{
	CurrentTimeStep = StartingTimeStep;
}

FSimulationTimeStepFragment::~FSimulationTimeStepFragment()
{
}
