// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ExperimentScorer.generated.h"

// Enum for the CVD test type
UENUM(BlueprintType)
enum class ECVDTestType : uint8
{
	Normal			=	0	UMETA(DisplayName = "Normal"),
	Protanomaly		=	1	UMETA(DisplayName = "Protanomaly"),
	Deuteranomaly	=	2	UMETA(DisplayName = "Deuteranomaly"),
	Tritanomaly		=	3	UMETA(DisplayName = "Tritanomaly")
};

USTRUCT(BlueprintType)
struct FCVDScores
{
	GENERATED_BODY()

	// Scores for the CVD test before Simulated CVD
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVD Scores")
	TArray<int32> BaselineScores;

	// Scores for the CVD test after Simulated CVD
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVD Scores")
	TArray<int32> SimulationExperimentScores;
};

UENUM(BlueprintType)
enum EPostQuestionAnswer : uint8
{
	EPQA_StronglyDisagree	= 0	UMETA(DisplayName = "Strongly Disagree"),
	EPQA_Disagree			= 1	UMETA(DisplayName = "Disagree"),
	EPQA_Neither			= 2	UMETA(DisplayName = "Neither Disagree or Agree"),
	EPQA_Agree				= 3	UMETA(DisplayName = "Agree"),
	EPQA_StronglyAgree		= 4	UMETA(DisplayName = "Strongly Agree or Easy"),
	EPQA_Unanswered			= 5	UMETA(DisplayName = "Unanswered")
};

USTRUCT(BlueprintType)
struct FPostSimulationQuestion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	int32 QuestionID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FString QuestionText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	TEnumAsByte<EPostQuestionAnswer> Answer = EPQA_Unanswered;
};

USTRUCT(BlueprintType)
struct FSimulationQuestionnaire
{
	GENERATED_BODY()

	// The experiment ID - used for identifying which simulation was just answered
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	int32 ExperimentID = 0;

	// Question 1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question1;

	// Question 2
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question2;

	// Question 3
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question3;

	// Question 4
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question4;

	// Question 5
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question5;

	// Question 6
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question6;

	// Question 7
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question7;

	// Question 8
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Simulation Question")
	FPostSimulationQuestion Question8;

	// Method to set the question text and ID
	FORCEINLINE void SetupQuestionTextsAndIDs()
	{
		Question1.QuestionID = 1;
		Question1.QuestionText = "Did you understand what the heatmap was representing?";

		Question2.QuestionID = 2;
		Question2.QuestionText = "If you had the opportunity to revisualize this simulation would the heatmap you just saw be one that you would use?";

		Question3.QuestionID = 3;
		Question3.QuestionText = "Was it easy to identify high density zones?";

		Question4.QuestionID = 4;
		Question4.QuestionText = "Was it easy to identify medium density zones?";

		Question5.QuestionID = 5;
		Question5.QuestionText = "Was it easy to identify low density zones?";

		Question6.QuestionID = 6;
		Question6.QuestionText = "Were all the colour bandings clear?";

		Question7.QuestionID = 7;
		Question7.QuestionText = "Was the simulation easy to understand?";

		Question8.QuestionID = 8;
		Question8.QuestionText = "If you knew anyone who uses heatmaps would you recommend this to someone?";
	}
	
};

/** Struct to store the final answers to the free choice option, where the participant can choose to use colour correction,
 * 3D heatmap, solid building, transparent building, mesh textures, agent transparent or solid*/
USTRUCT(BlueprintType)
struct FExperimentConditions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pre Simulation Choice")
	int32 ExperimentID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pre Simulation Choice")
	bool bUseColourCorrection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pre Simulation Choice")
	bool bUse3DHeatmap = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pre Simulation Choice")
	bool bUseSolidBuilding = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pre Simulation Choice")
	bool bUseMeshTextures = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pre Simulation Choice")
	bool bSolidAgents = false;
};

/**
 * This class handles scoring and participant feedback for the experiment - as I will likely be unable to get the CVD
 * scored in realtime and adjust the experiment accordingly, I will need to validate CVD scores after the experiment.
 */
UCLASS(Blueprintable)
class HIT_THESISWORK_API UExperimentScorer : public UActorComponent
{
	GENERATED_BODY()

public:
	// Default Constructor	
	UExperimentScorer();

	/**
	 * Create the required number of post simulation questionnaires
	 *
	 * @param[int32] InNumberOfQuestionnaires The number of questionnaires to create
	 */
	UFUNCTION(BlueprintCallable, Category = "Experiment Scorer")
	void CreatePostSimulationQuestionnaires(const int32 InNumberOfQuestionnaires);

	/**
	 * Method to check files and auto generate a participant ID, date and simulation type
	 *
	 * @param[FString] InFilePath The file path to check
	 * @param[FString] InSimulationType The simulation type to set
	 *
	 * @return[bool] True if the file path is valid and the participant ID, date and simulation type are set
	 */
	UFUNCTION(BlueprintCallable, Category = "Experiment Scorer")
	bool CheckFilesAndAutoGenerateParticipantID(const FString& InFilePath, FString& InSimulationType);

	/**
	 * When a questionnaire has been answered, this method will store the answers to the file,
	 * for simplicity it will just rewrite the file with all questionnaires and answers, this way it will remain in
	 * the same order and require less post-processing of the data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Experiment Scorer")
	void StoreQuestionnaireAnswers();

	/**
	 * In case a participant is colour deficient, we need to override the random designation and give them a specific
	 * colour deficiency type to match there own.
	 *
	 * @param[ECVDTestType] InCVDTestType The CVD test type to set
	 */
	UFUNCTION(BlueprintCallable, Category = "Experiment Scorer")
	void OverrideCVDTestType(const ECVDTestType InCVDTestType);

	/**
	 * Generates random order int array based on the input array length
	 *
	 * @param[int32] InArrayLength The length of the array to generate
	 * 
	 * @return RandomOrderArray The array of random order
	 */
	UFUNCTION(BlueprintCallable, Category = "Experiment Scorer")
	TArray<int32> GenerateRandomOrderArray(const int32 InArrayLength);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	bool bColourDeficiencyOverrided = false;

	/** Participant ID number */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	int32 ParticipantID;

	/** Date of Experiment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FDateTime ExperimentDate;

	/** CVD Simulation Type STRING - ie normal, protan, deutan and etc */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FString SimulationTypeString;

	/** CVD Simulation Type ENUM - ie normal, protan, deutan and etc */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	ECVDTestType ESimulationType = ECVDTestType::Protanomaly;
	
	/** CVD test scores */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FCVDScores CVDTestScores;
	
	/** Array storing all the questionnaires and responses from the participant */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	TArray<FSimulationQuestionnaire> SimulationQuestionnaireAnswers;

	/** Array storing all the experiment conditions except for the final participant choice - help validate what people got was the same */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	TArray<FExperimentConditions> ExperimentConditions;

	/** Baseline Experiment Conditions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FExperimentConditions BaselineExperimentConditions;
	
	/** Baseline questionnaire and response from the participant */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FSimulationQuestionnaire BaselineSimulationQuestionnaireAnswers;
	
	/** Final questionnaire and response from the participant */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FSimulationQuestionnaire FinalSimulationQuestionnaireAnswers;
	
	/** Final optional Participant choice response */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FExperimentConditions FinalParticipantChoice;

	/** Limit of experiment simulations per category */
	const int32 SimulationLimit = 5;// TODO: change this if recruitment increases

	/** Complete file path for data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Experiment Scorer")
	FString CSVFilePath;
};
