// Fill out your copyright notice in the Description page of Project Settings.


#include "ExperimentScorer.h"

#include "Algo/RandomShuffle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UExperimentScorer::UExperimentScorer(): ParticipantID(0), ESimulationType(ECVDTestType::Normal)
{
}

void UExperimentScorer::CreatePostSimulationQuestionnaires(const int32 InNumberOfQuestionnaires)
{
	// Clear the array
	SimulationQuestionnaireAnswers.Empty();

	// Create the questionnaires and simulation conditions - the sim conditions set later but this ensures the array is the correct size
	for (int32 i = 0; i < InNumberOfQuestionnaires; i++)
	{
		FSimulationQuestionnaire Questionnaire;
		Questionnaire.ExperimentID = i;
		Questionnaire.SetupQuestionTextsAndIDs();
		
		SimulationQuestionnaireAnswers.Add(Questionnaire);

		// Create and add blank sim conditions
		FExperimentConditions SimConditions;
		ExperimentConditions.Add(SimConditions);
	}

	// Baseline conditions
	BaselineExperimentConditions.bSolidAgents = false;
	BaselineExperimentConditions.bUse3DHeatmap = false;
	BaselineExperimentConditions.bUseColourCorrection = false;
	BaselineExperimentConditions.bUseMeshTextures = true;
	BaselineExperimentConditions.bUseSolidBuilding = true;

	// Baseline Participant questionnaire
	FSimulationQuestionnaire BaselineAnswers;
	BaselineAnswers.ExperimentID = -1;
	BaselineAnswers.SetupQuestionTextsAndIDs();
	BaselineSimulationQuestionnaireAnswers = BaselineAnswers;

	// Create final participant questionnaire and the ID will be the number of questionnaires
	FSimulationQuestionnaire ParticipantChoice;
	ParticipantChoice.ExperimentID = InNumberOfQuestionnaires;
	ParticipantChoice.SetupQuestionTextsAndIDs();
	FinalSimulationQuestionnaireAnswers = ParticipantChoice;

	// DEBUG - Log the number of questionnaires created
	UE_LOG(LogTemp, Log, TEXT("Number of Questionnaires Created: %d"), SimulationQuestionnaireAnswers.Num());
}

bool UExperimentScorer::CheckFilesAndAutoGenerateParticipantID(const FString& InFilePath, FString& InSimulationType)
{
	bool Result = false;

	// Check if the file path is valid and a valid directory
	if (InFilePath.IsEmpty() || FPaths::DirectoryExists(InFilePath) == false)
	{
		// Log error
		UE_LOG(LogTemp, Error, TEXT("File path is empty! Or the directory does not exist!"));
		return Result;
	}
	
	// Resolve the directory path to an absolute path
	FString AbsolutePath = FPaths::ConvertRelativePathToFull(InFilePath);

	// Array to hold the filenames
	TArray<FString> FileNames;

	// Use IFileManager to scan the directory
	IFileManager& FileManager = IFileManager::Get();
	FileManager.FindFiles(FileNames, *AbsolutePath, TEXT("*.*")); // Get all files with any extension

	// Get the number of files
	int32 NumFiles = FileNames.Num();

	// Set the participant ID to the number of files
	ParticipantID = NumFiles + 1; // This way all IDs are unique and starts at 1

	// Get today's date and time
	ExperimentDate = FDateTime::Now();
	// create date time in a string format and format it to dd-mm-yyyy_hh-mm-ss
	FString DateTime = ExperimentDate.ToString(TEXT("%d-%m-%Y_%H-%M-%S"));
	
	// Number of simulations of the same type
	int32 NormalVisionSimulations = 0;
	int32 ProtanomalySimulations = 0;
	int32 DeuteranomalySimulations = 0;
	int32 TritanomalySimulations = 0;
	int32 ErrorSimulations = 0;

	// loop through the files and check the simulation type
	for (int32 i = 0; i < NumFiles; i++)
	{
		// Get the file name
		FString FileName = FileNames[i];

		// Check the simulation type
		if (FileName.Contains("Normal"))
		{
			NormalVisionSimulations++;
		}
		else if (FileName.Contains("Protanomaly"))
		{
			ProtanomalySimulations++;
		}
		else if (FileName.Contains("Deuteranomaly"))
		{
			DeuteranomalySimulations++;
		}
		else if (FileName.Contains("Tritanomaly"))
		{
			TritanomalySimulations++;
		}
		else
		{
			ErrorSimulations++;
		}
	}

	// bool to find a unique simulation type
	bool bUniqueSimulationType = false;

	// The found simulation type
	FString RandomSimulationType = "";

	// The limit of simulations of the same type or if we already overridden then we can just use the overridden type
	while (bUniqueSimulationType == false && bColourDeficiencyOverrided == false)
	{
		// Generate random simulation type
		uint8 RandomUint8 = FMath::RandRange(0, 3);
		

		if (RandomUint8 == 0 && NormalVisionSimulations < SimulationLimit)
		{
			RandomSimulationType = "Normal";
			bUniqueSimulationType = true;
			ESimulationType = ECVDTestType::Normal;
		}
		else if (RandomUint8 == 1 && ProtanomalySimulations < SimulationLimit)
		{
			RandomSimulationType = "Protanomaly";
			bUniqueSimulationType = true;
			ESimulationType = ECVDTestType::Protanomaly;
		}
		else if (RandomUint8 == 2 && DeuteranomalySimulations < SimulationLimit)
		{
			RandomSimulationType = "Deuteranomaly";
			bUniqueSimulationType = true;
			ESimulationType = ECVDTestType::Deuteranomaly;
		}
		else if (RandomUint8 == 3 && TritanomalySimulations < SimulationLimit)
		{
			RandomSimulationType = "Tritanomaly";
			bUniqueSimulationType = true;
			ESimulationType = ECVDTestType::Tritanomaly;
		}
	}

	if (!bColourDeficiencyOverrided)
	{
		// should have found one so can set the simulation type
		SimulationTypeString = RandomSimulationType;
		InSimulationType = RandomSimulationType;
	}

	
	// create a file name
	FString FileName = FString::Printf(TEXT("Participant_%d_%s_%s"), ParticipantID, *DateTime, *SimulationTypeString);

	// Save a CVS file using the file name to the directory
	FString CSVFileName = FileName + ".csv";
	CSVFilePath = FPaths::Combine(AbsolutePath, CSVFileName);

	Result = FFileHelper::SaveStringToFile(FString("Test Data"), *CSVFilePath);//TODO: Replace "Test Data" with the actual data and look at my other code for how to format the data and add more complexity

	return Result;
}

// Helper function to format question and answer pairs
FString FormatQuestionAnswer(const FString& QuestionText, int32 AnswerValue)
{
	return FString::Printf(TEXT("%s,%d\n"), *QuestionText, AnswerValue);
}

// Helper function to format experiment conditions
FString FormatExperimentConditions(const FString& ExperimentID, const FExperimentConditions& Conditions)
{
	return FString::Printf(
		TEXT("Experiment ID:, %s\nUse Colour Correction:, %s\nUse 3D Heatmap:, %s\nUse Solid Building:, %s\nUse Mesh Texture:, %s\nUse Solid Agents:, %s\n\n"),
		*ExperimentID,
		Conditions.bUseColourCorrection ? TEXT("True") : TEXT("False"),
		Conditions.bUse3DHeatmap ? TEXT("True") : TEXT("False"),
		Conditions.bUseSolidBuilding ? TEXT("True") : TEXT("False"),
		Conditions.bUseMeshTextures ? TEXT("True") : TEXT("False"),
		Conditions.bSolidAgents ? TEXT("True") : TEXT("False")
	);
}

// Helper function to format CVD scores
FString FormatCVDScores(const FString& Title, const TArray<int32>& Scores)
{
	FString Data = Title + "\n";
	for (int32 Score : Scores)
	{
		Data += FString::Printf(TEXT("%d\n"), Score);
	}
	return Data + "\n";
}

void UExperimentScorer::StoreQuestionnaireAnswers()
{
	if (CSVFilePath.IsEmpty() || !FPaths::FileExists(CSVFilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("File path is empty or the file does not exist: %s"), *CSVFilePath);
		return;
	}

	FString Data;

	// In case we have had to override test type
	if (bColourDeficiencyOverrided)
	{
		Data += "CVD Test Type Overridden\n";
	}

	// Add initial data
	Data += FString::Printf(TEXT("%s, %s\n\n"), *ExperimentDate.ToString(TEXT("%d-%m-%Y_%H-%M-%S")), *SimulationTypeString);

	// Add CVD test scores
	Data += FormatCVDScores(TEXT("Baseline CVD Test Scores"), CVDTestScores.BaselineScores);
	Data += FormatCVDScores(TEXT("Simulated CVD Test Scores"), CVDTestScores.SimulationExperimentScores);

	// Add baseline questionnaire answers
	Data += "Baseline Scenario Questionnaire\nExperiment: Baseline\n";
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question1.QuestionText, BaselineSimulationQuestionnaireAnswers.Question1.Answer.GetValue());
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question2.QuestionText, BaselineSimulationQuestionnaireAnswers.Question2.Answer.GetValue());
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question3.QuestionText, BaselineSimulationQuestionnaireAnswers.Question3.Answer.GetValue());
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question4.QuestionText, BaselineSimulationQuestionnaireAnswers.Question4.Answer.GetValue());
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question5.QuestionText, BaselineSimulationQuestionnaireAnswers.Question5.Answer.GetValue());
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question6.QuestionText, BaselineSimulationQuestionnaireAnswers.Question6.Answer.GetValue());
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question7.QuestionText, BaselineSimulationQuestionnaireAnswers.Question7.Answer.GetValue());
	Data += FormatQuestionAnswer(*BaselineSimulationQuestionnaireAnswers.Question8.QuestionText, BaselineSimulationQuestionnaireAnswers.Question8.Answer.GetValue());
	Data += "\nBaseline Experiment Conditions\n";
	Data += FormatExperimentConditions(TEXT("Baseline"), BaselineExperimentConditions);

	// Add simulation questionnaire answers and experiment conditions
	for (int32 i = 0; i < SimulationQuestionnaireAnswers.Num(); i++)
	{
		const FSimulationQuestionnaire& Questionnaire = SimulationQuestionnaireAnswers[i];
		const FExperimentConditions& Condition = ExperimentConditions[i];

		Data += FString::Printf(TEXT("Experiment: %d\n"), Questionnaire.ExperimentID);
		Data += FormatQuestionAnswer(*Questionnaire.Question1.QuestionText, Questionnaire.Question1.Answer.GetValue());
		Data += FormatQuestionAnswer(*Questionnaire.Question2.QuestionText, Questionnaire.Question2.Answer.GetValue());
		Data += FormatQuestionAnswer(*Questionnaire.Question3.QuestionText, Questionnaire.Question3.Answer.GetValue());
		Data += FormatQuestionAnswer(*Questionnaire.Question4.QuestionText, Questionnaire.Question4.Answer.GetValue());
		Data += FormatQuestionAnswer(*Questionnaire.Question5.QuestionText, Questionnaire.Question5.Answer.GetValue());
		Data += FormatQuestionAnswer(*Questionnaire.Question6.QuestionText, Questionnaire.Question6.Answer.GetValue());
		Data += FormatQuestionAnswer(*Questionnaire.Question7.QuestionText, Questionnaire.Question7.Answer.GetValue());
		Data += FormatQuestionAnswer(*Questionnaire.Question8.QuestionText, Questionnaire.Question8.Answer.GetValue());
		Data += "\nExperiment Condition\n";
		Data += FormatExperimentConditions(FString::FromInt(Condition.ExperimentID), Condition);
	}

	// Add final questionnaire answers
	Data += "Final Scenario Questionnaire\n";
	Data += FString::Printf(TEXT("Experiment: %d\n"), FinalSimulationQuestionnaireAnswers.ExperimentID);
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question1.QuestionText, FinalSimulationQuestionnaireAnswers.Question1.Answer.GetValue());
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question2.QuestionText, FinalSimulationQuestionnaireAnswers.Question2.Answer.GetValue());
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question3.QuestionText, FinalSimulationQuestionnaireAnswers.Question3.Answer.GetValue());
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question4.QuestionText, FinalSimulationQuestionnaireAnswers.Question4.Answer.GetValue());
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question5.QuestionText, FinalSimulationQuestionnaireAnswers.Question5.Answer.GetValue());
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question6.QuestionText, FinalSimulationQuestionnaireAnswers.Question6.Answer.GetValue());
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question7.QuestionText, FinalSimulationQuestionnaireAnswers.Question7.Answer.GetValue());
	Data += FormatQuestionAnswer(*FinalSimulationQuestionnaireAnswers.Question8.QuestionText, FinalSimulationQuestionnaireAnswers.Question8.Answer.GetValue());

	// Add final participant choice
	Data += "\nFinal Scenario Participant Choice\n";
	Data += FormatExperimentConditions(FString::FromInt(FinalParticipantChoice.ExperimentID), FinalParticipantChoice);

	// Write data to file
	if (FFileHelper::SaveStringToFile(Data, *CSVFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("File updated successfully: %s"), *CSVFilePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to update file: %s"), *CSVFilePath);
	}
}

void UExperimentScorer::OverrideCVDTestType(const ECVDTestType InCVDTestType)
{
	bColourDeficiencyOverrided = false; // TODO: change to true once images for thesis done

	switch (InCVDTestType)
	{
	case ECVDTestType::Normal:
		ESimulationType = ECVDTestType::Normal;
		SimulationTypeString = "Normal";
		break;
	case ECVDTestType::Deuteranomaly:
		ESimulationType = ECVDTestType::Deuteranomaly;
		SimulationTypeString = "Deuteranomaly";
		break;
	case ECVDTestType::Protanomaly:
		ESimulationType = ECVDTestType::Protanomaly;
		SimulationTypeString = "Protanomaly";
		break;
	case ECVDTestType::Tritanomaly:
		ESimulationType = ECVDTestType::Tritanomaly;
		SimulationTypeString = "Tritanomaly";
		break;
	default:
		break;
	}
}

TArray<int32> UExperimentScorer::GenerateRandomOrderArray(const int32 InArrayLength)
{
	TArray<int32> RandomOrderArray;

	// Issue with experiment array and delegate that is putting it out by 1 so adding an extra 0 at the start
	// after the shuffle

	// Fill the array with numbers from 0 to the length - 1
	for (int32 i = 0; i < InArrayLength; i++)
	{
		RandomOrderArray.Add(i);
	}

	Algo::RandomShuffle(RandomOrderArray);

	// add an extra 0 at the start of array
	RandomOrderArray.Insert(0, 0);

	return RandomOrderArray;
}
