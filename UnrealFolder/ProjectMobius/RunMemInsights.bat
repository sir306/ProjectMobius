@echo off
title Starting Engine for memory insights

cd "C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\" 
UnrealInsights.exe -trace=default,memory,metadata,assetmetadata

cd "C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\" 
UnrealEditor.exe -trace=default,memory,metadata,assetmetadata

pause