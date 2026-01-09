// Fill out your copyright notice in the Description page of Project Settings.

#include "ImPlot/ImPlotDataSubsystem.h"
#include "ImPlot/ImPlotVisualizationSubsystem.h"
#include "Engine/World.h"

void UImPlotDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
        Super::Initialize(Collection);

        if (UWorld* World = GetWorld())
        {
                ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
        }
}

void UImPlotDataSubsystem::Deinitialize()
{
        ImPlotSubsystem = nullptr;
        Super::Deinitialize();
}

void UImPlotDataSubsystem::SetChartTitle(const FText& InTitle)
{
        if (!ImPlotSubsystem)
        {
                if (UWorld* World = GetWorld())
                {
                        ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
                }
        }
        if (ImPlotSubsystem)
        {
                ImPlotSubsystem->SetChartTitle(InTitle);
        }
}

void UImPlotDataSubsystem::SetStatusMessage(const FText& InMessage)
{
        if (!ImPlotSubsystem)
        {
                if (UWorld* World = GetWorld())
                {
                        ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
                }
        }
        if (ImPlotSubsystem)
        {
                ImPlotSubsystem->SetStatusMessage(InMessage);
        }
}

void UImPlotDataSubsystem::SetAxisSettings(const FText& InXTitle, const FText& InYTitle, double InXMin, double InXMax, double InYMin, double InYMax)
{
        if (!ImPlotSubsystem)
        {
                if (UWorld* World = GetWorld())
                {
                        ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
                }
        }
        if (ImPlotSubsystem)
        {
                ImPlotSubsystem->SetAxisSettings(InXTitle, InYTitle, InXMin, InXMax, InYMin, InYMax);
        }
}

void UImPlotDataSubsystem::SetPlotPoints(const TArray<FVector2D>& InPoints)
{
        if (!ImPlotSubsystem)
        {
                if (UWorld* World = GetWorld())
                {
                        ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
                }
        }
        if (ImPlotSubsystem)
        {
                ImPlotSubsystem->SetPlotPoints(InPoints);
        }
}

void UImPlotDataSubsystem::UpdateLiveSample(double InTimeSeconds, double InCount)
{
        if (!ImPlotSubsystem)
        {
                if (UWorld* World = GetWorld())
                {
                        ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
                }
        }
        if (ImPlotSubsystem)
        {
                ImPlotSubsystem->UpdateLiveSample(InTimeSeconds, InCount);
        }
}

void UImPlotDataSubsystem::ToggleOverlay()
{
        if (!ImPlotSubsystem)
        {
                if (UWorld* World = GetWorld())
                {
                        ImPlotSubsystem = World->GetSubsystem<UImPlotVisualizationSubsystem>();
                }
        }
        if (ImPlotSubsystem)
        {
                ImPlotSubsystem->ToggleOverlay();
        }
}
