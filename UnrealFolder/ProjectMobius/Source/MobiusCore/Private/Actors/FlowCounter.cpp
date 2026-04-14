// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/FlowCounter.h"

#include "Components/BoxComponent.h"
#include "Components/DeformableQuadComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/StatisticActorManagementSubsystem.h"
#include "Subsystems/TimeDilationSubSystem.h"
#include "Subsystems/MobiusUserFeedbackSubsystem.h"

// Shared base material for all FlowCounters (loaded once, reused).
static TWeakObjectPtr<UMaterialInterface> GFlowCounterBaseMaterial;

/** Get or load the flow counter base material. */
static UMaterialInterface* GetOrLoadFlowCounterBaseMaterial()
{
	if (GFlowCounterBaseMaterial.IsValid())
	{
		return GFlowCounterBaseMaterial.Get();
	}

	// Hard-coded path for now – you can swap this to a soft reference later if you want.
	UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/01_Dev/LevelAssets/M_FlowCounterPlane.M_FlowCounterPlane"));

	if (!Loaded)
	{
		if (UMobiusUserFeedbackSubsystem* Feedback = UMobiusUserFeedbackSubsystem::Get(nullptr))
		{
			Feedback->ReportError(
				FText::FromString("Flow Counter Error"),
				FText::FromString("Base material missing"),
				FText::FromString("Failed to load M_FlowCounterPlane at the expected content path."),
				FText::FromString("FlowCounter"));
		}
		UE_LOG(LogTemp, Error,
			TEXT("FlowCounter: Failed to load base material M_FlowCounterPlane at expected path."));
		return nullptr;
	}

	GFlowCounterBaseMaterial = Loaded;
	return Loaded;
}


// TODO: Move these helpers to a utility class - likely will be useful elsewhere
static FORCEINLINE FVector SafeHorizontal(const FVector& V)
{
	// Zero Z then renormalize; if degenerate, fall back to forward axis.
	FVector H(V.X, V.Y, 0.f);
	const float LenSq = H.SizeSquared();
	if (LenSq < KINDA_SMALL_NUMBER) return FVector::ForwardVector;
	return H.GetSafeNormal();
}

// Check if Point lies on the line segment AB
static bool IsPointOnLineSegment(const FVector& A, const FVector& B, const FVector& Point, float Tolerance = KINDA_SMALL_NUMBER)
{
	// Vector from A to B
	const FVector AB = B - A;
	// Vector from A to Point
	const FVector AP = Point - A;

	// Project AP onto AB to find where the point lies along the line
	const float Dot = FVector::DotProduct(AP, AB);
	const float AB_SquaredLength = AB.SizeSquared();

	// If projection is outside the segment, it's not on the line
	if (Dot < 0.0f || Dot > AB_SquaredLength)
	{
		return false;
	}

	// Compute the closest point on AB to Point
	const float T = Dot / AB_SquaredLength;
	const FVector Closest = A + AB * T;

	// Check if Point is very close to the line (within tolerance)
	return Point.Equals(Closest, Tolerance);
}

// Returns true if segment [Prev,Curr] crosses the vertical gate plane through A→B,
// with the intersection’s Z inside the prism. Outputs:
//   - OutIntersectionOnLine: the *projection* of the plane hit onto the finite A→B segment
//   - OutT: normalized [0..1] position along A→B (0=A, 1=B)
static bool SegmentCrossesGateProjectToLine(
	const FVector& Prev,
	const FVector& Curr,
	const FVector& A,    // gate center-line start (3D)
	const FVector& B,    // gate center-line end   (3D)
	const FFlowCounterZSearchLimits& ZLimits, // using MinZBounds/MaxZBounds as ABS Z
	FVector& OutIntersectionOnLine,
	float&   OutT,
	float    XYSearchRadiusTol        = 4.0f, // cm
	float    ZTolerance               = 1.0f, // cm
	float    PlaneSignedDistTolerance = 3.0f  // cm
)
{
	// --- INPUTS ---
	// UE_LOG(LogTemp, Warning, TEXT("[FC] Inputs: Prev%s Curr%s A%s B%s  XYTol=%.2f ZTol=%.2f PlaneTol=%.2f  ZWin[%.1f..%.1f]"),
	//        *Prev.ToString(), *Curr.ToString(), *A.ToString(), *B.ToString(),
	//        XYSearchRadiusTol, ZTolerance, PlaneSignedDistTolerance,
	//        ZLimits.MinZBounds.load(), ZLimits.MaxZBounds.load());

	OutIntersectionOnLine = FVector::ZeroVector;
	OutT = 0.0f;

	// 0) Degenerate segment?
	if (Prev.Equals(Curr, UE_SMALL_NUMBER))
	{
		// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] Degenerate segment: Prev==Curr (%.3f,%.3f,%.3f)"),
		//        Prev.X, Prev.Y, Prev.Z);
		return false;
	}

	const FVector AB = B - A;
	const double  ABLenSq3D = AB.SizeSquared();
	if (ABLenSq3D <= UE_SMALL_NUMBER)
	{
		// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] Degenerate gate: A==B"));
		return false;
	}

	// 1) Build vertical plane through A->B (three-point form)
	FPlane GatePlane(A, B, A + FVector::UpVector);
	FVector N(GatePlane.X, GatePlane.Y, GatePlane.Z);
	const double NLen = N.Size();
	if (NLen <= UE_SMALL_NUMBER)
	{
		// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] Plane normal invalid (|N|=0)"));
		return false;
	}
	const FVector Nn = N / NLen; // unit normal

	// Signed distances in cm (anchor at A for clarity)
	const double d0 = FVector::DotProduct(Prev - A, Nn);
	const double d1 = FVector::DotProduct(Curr - A, Nn);
	const bool   bDifferentSides = (d0 > 0.0 && d1 < 0.0) || (d0 < 0.0 && d1 > 0.0);
	const bool   bTouchesPlane   = (FMath::Abs(d0) <= PlaneSignedDistTolerance) ||
		(FMath::Abs(d1) <= PlaneSignedDistTolerance);

	// UE_LOG(LogTemp, Warning, TEXT("[FC] Plane N=(%.6f,%.6f,%.6f)  d0=%.3f d1=%.3f  cross=%d touch=%d"),
	//        Nn.X, Nn.Y, Nn.Z, d0, d1, bDifferentSides ? 1 : 0, bTouchesPlane ? 1 : 0);

	if (!bDifferentSides && !bTouchesPlane)
	{
		// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] No cross and not within plane tolerance."));
		return false;
	}

	// 2) Segment-plane intersection (explicit, unit-normal form)
	FVector Hit = FVector::ZeroVector;
	bool bHaveHit = false;

	if (bDifferentSides)
	{
		const FVector D = Curr - Prev;
		const double  denom = FVector::DotProduct(Nn, D);
		// denom can be tiny if segment ~parallel to plane
		if (FMath::Abs(denom) > SMALL_NUMBER)
		{
			const double numer = FVector::DotProduct(Nn, (A - Prev));  // NOTE: avoid GatePlane.W scaling issues
			const double tLine = numer / denom;                        // param in [0,1] if intersection within segment
			// UE_LOG(LogTemp, Warning, TEXT("[FC] Cross: denom=%.6f numer=%.6f tLine=%.6f"), denom, numer, tLine);
			if (tLine >= 0.0 && tLine <= 1.0)
			{
				Hit = Prev + static_cast<float>(tLine) * D;
				bHaveHit = true;
			}
			else
			{
				// UE_LOG(LogTemp, Warning, TEXT("[FC] Cross but tLine outside [0,1]. Will try touch projection if eligible."));
			}
		}
		else
		{
			// UE_LOG(LogTemp, Warning, TEXT("[FC] Cross but denom ~ 0 (parallel). Will try touch projection if eligible."));
		}
	}

	if (!bHaveHit)
	{
		if (!bTouchesPlane)
		{
			// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] No valid intersection and not a touch."));
			return false;
		}
		const bool UsePrev = (FMath::Abs(d0) <= FMath::Abs(d1));
		const FVector NearPt = UsePrev ? Prev : Curr;
		Hit = NearPt - FVector::DotProduct(Nn, NearPt - A) * Nn; // project endpoint onto plane
		// UE_LOG(LogTemp, Warning, TEXT("[FC] Touch: projected %s to Hit%s"),
		//        UsePrev ? TEXT("Prev") : TEXT("Curr"), *Hit.ToString());
	}

	// 3) XY-only lateral projection to finite segment
	const FVector2D Axy(A.X, A.Y);
	const FVector2D Bxy(B.X, B.Y);
	const FVector2D Hxy(Hit.X, Hit.Y);
	const FVector2D ABxy = Bxy - Axy;
	const double    ABxyLenSq = ABxy.SizeSquared();

	float tXY = 0.0f;

	if (ABxyLenSq <= SMALL_NUMBER)
	{
		const float dx = Hxy.X - Axy.X;
		const float dy = Hxy.Y - Axy.Y;
		const float distXY = FMath::Sqrt(dx*dx + dy*dy);
		// UE_LOG(LogTemp, Warning, TEXT("[FC] Degenerate XY: dist(HitXY, AXY)=%.3f (tol=%.3f)"), distXY, XYSearchRadiusTol);
		if (distXY > XYSearchRadiusTol)
		{
			// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] XY distance exceeds tolerance in degenerate XY case."));
			return false;
		}

		// Build outputs for t=0
		OutT = 0.0f;
		OutIntersectionOnLine = A;

		// Prism check (ABSOLUTE world-Z bounds)
		const float MinZ = ZLimits.MinZBounds.load() - ZTolerance;
		const float MaxZ = ZLimits.MaxZBounds.load() + ZTolerance;
		const bool  passZ = (Hit.Z >= MinZ && Hit.Z <= MaxZ);
		// UE_LOG(LogTemp, Warning, TEXT("[FC] Prism check (XY-degenerate): HitZ=%.2f in [%.2f..%.2f] -> %d"),
		//        Hit.Z, MinZ, MaxZ, passZ ? 1 : 0);
		return passZ;
	}

	// Compute tXY and lateral distance in XY
	const double dot = FVector2D::DotProduct(Hxy - Axy, ABxy);
	const double rawT = dot / ABxyLenSq;
	tXY = FMath::Clamp(static_cast<float>(rawT), 0.0f, 1.0f);

	const FVector2D Cxy = Axy + ABxy * tXY;
	const float LateralDistXY = (Hxy - Cxy).Size();

	// UE_LOG(LogTemp, Warning, TEXT("[FC] XY: rawT=%.6f tXY=%.6f  LateralDistXY=%.3f (tol=%.3f)"),
	//        rawT, (double)tXY, LateralDistXY, XYSearchRadiusTol);

	if (LateralDistXY > XYSearchRadiusTol)
	{
		// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] LateralDistXY exceeds tolerance."));
		return false;
	}

	// 4) 3D param for output/bucketing
	const double rawT3D = FVector::DotProduct(Hit - A, AB) / ABLenSq3D;
	const float  t3D = FMath::Clamp(static_cast<float>(rawT3D), 0.0f, 1.0f);
	const FVector CenterPoint = A + AB * t3D;

	// UE_LOG(LogTemp, Warning, TEXT("[FC] 3D: rawT3D=%.6f t3D=%.6f  CenterPoint%s"), rawT3D, (double)t3D, *CenterPoint.ToString());

	// 5) Z prism (ABSOLUTE world-Z bounds) - this takes the lowest pillar MinZ and highest pillar MaxZ
	//    and expands by ZTolerance -> this is a fast z tolerance check
	const float MinZ = ZLimits.MinZBounds.load() - ZTolerance;
	const float MaxZ = ZLimits.MaxZBounds.load() + ZTolerance;
	const bool  passZ = (Hit.Z >= MinZ && Hit.Z <= MaxZ);
	

	// UE_LOG(LogTemp, Warning, TEXT("[FC] Prism: HitZ=%.2f in [%.2f..%.2f] -> %d"),
	//        Hit.Z, MinZ, MaxZ, passZ ? 1 : 0);

	if (!passZ)
	{
		// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] Z prism reject."));
		return false;
	}

	// --------- Relative vertical band check against sloped line height ----------
	const float LineZ = FMath::Lerp(A.Z, B.Z, t3D);
	const float RelMin = 10.0f;   // 10 cm below the line
	const float RelMax = 210.0f;  // 210 cm above the line
	// TODO: Need to clean up the line offset so we are consistent about ground vs line height as this is confusing
	const float MinZAllowed = LineZ - RelMin - 100.0f; // The line is from the center of the pillars, so we offset down by 100 cm to get to ground level
	const float MaxZAllowed = LineZ + RelMax - 100.0f;
	// const float MinZAllowed = LineZ - RelMin - GroundOffsetFromLineCM; // The line is from the center of the pillars, so we offset down by 100 cm to get to ground level
	// const float MaxZAllowed = LineZ + RelMax - GroundOffsetFromLineCM;

	if (Hit.Z < MinZAllowed - ZTolerance || Hit.Z > MaxZAllowed + ZTolerance)
	{
		// UE_LOG(LogTemp, Warning, TEXT("[FC][FAIL] Z=%.2f not in [%.2f..%.2f] +/- %.2f (relative band)"),
		//        Hit.Z, MinZAllowed, MaxZAllowed, ZTolerance);
		return false;
	}

	// 6) Success
	OutIntersectionOnLine = CenterPoint; // on center line
	OutT = t3D;
	// UE_LOG(LogTemp, Warning, TEXT("[FC][OK] OutT=%.6f  OutIntersection%s"), (double)OutT, *OutIntersectionOnLine.ToString());
	return true;
}

// Sets default values
AFlowCounter::AFlowCounter()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	/* Attach the meshes to the scene component root */
	// Create a default scene root component
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Create pillar mesh components
	FlowCounterPillarMesh1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlowCounterPillarMesh1"));
	FlowCounterPillarMesh1->SetupAttachment(RootComponent);
	FlowCounterPillarMesh1->SetVisibility(true);
	// As we use world space, we need to set the pillars to use world space
	
	

	FlowCounterPillarMesh2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlowCounterPillarMesh2"));
	FlowCounterPillarMesh2->SetupAttachment(RootComponent);
	FlowCounterPillarMesh2->SetVisibility(true);
	

	// load mesh for the pillars
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMesh(TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'"));
	if (DefaultMesh.Succeeded())
	{
		FlowCounterPillarMesh1->SetStaticMesh(DefaultMesh.Object);
		FlowCounterPillarMesh1->SetWorldLocation(FVector(50.0f, 0.0f, 0.0f)); // Offset for ease of development
		FlowCounterPillarMesh1->SetWorldScale3D(FVector(0.1f,0.1f,1.0f)); // Scale down the pillar for better visibility
			
		FlowCounterPillarMesh2->SetStaticMesh(DefaultMesh.Object);
		FlowCounterPillarMesh2->SetWorldLocation(FVector(-50.0f, 0.0f, 0.0f)); // Offset for ease of development
		FlowCounterPillarMesh2->SetWorldScale3D(FVector(0.1f,0.1f,1.0f)); // Scale down the pillar for better visibility
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("Default mesh not found!"));
	}

	// Set up the box component for flow counter trigger area
	FlowCounterTriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("FlowCounterTriggerBox"));
	//FlowCounterTriggerBox->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
	FlowCounterTriggerBox->SetupAttachment(RootComponent);

	// Create and set as root
	CounterBarrierVisualMesh = CreateDefaultSubobject<UDeformableQuadComponent>(TEXT("DeformableQuad"));
	CounterBarrierVisualMesh->SetupAttachment(RootComponent);

	// (Optional) set a starting size before the proxy is created
	CounterBarrierVisualMesh->Initialize(100.f, 100.f);
	
	// ensure no collision on the visual mesh
	CounterBarrierVisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CounterBarrierVisualMesh->SetGenerateOverlapEvents(false);
	CounterBarrierVisualMesh->SetCanEverAffectNavigation(false);

	UpdateFlowCounterTriggerBox();

	// TODO: setup alternative for VR
	// As the trigger box is only used for query we set it to query only (if there was no mouse clicks then set to none)
	FlowCounterTriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FlowCounterTriggerBox->SetCollisionObjectType(ECC_WorldDynamic);
	FlowCounterTriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	FlowCounterTriggerBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	//DEBUG - for now we will make the trigger box visible in the sim
	FlowCounterTriggerBox->SetVisibility(true);
	FlowCounterTriggerBox->SetHiddenInGame(false);
	
	

	RootComponent->UpdateChildTransforms();
}

AFlowCounter::~AFlowCounter()
{
	RemoveFlowCounterToSubsystem();

	if (GetWorld() == nullptr){return;}  
	// we need to unbind to the time dilation subsystem delegate for current simulation time
	if (UTimeDilationSubSystem* TimeDilationSub = GetWorld()->GetSubsystem<UTimeDilationSubSystem>())
	{
		TimeDilationSub->OnNewCurrentTime.RemoveDynamic(this, &AFlowCounter::NewSimTime);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounter: Time Dilation Subsystem not found!"));
	}
}

void AFlowCounter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!CounterBarrierVisualMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounter: CounterBarrierVisualMesh is null in PostInitializeComponents"));
		return;
	}

	// Get the shared base material
	UMaterialInterface* Base = GetOrLoadFlowCounterBaseMaterial();

	if (Base)
	{
		// We keep per-instance MID because we may change params per counter.
		const FString UniqueName = FString::Printf(TEXT("FlowCounterMID_%s"), *GetName());
		CounterBarrierVisualMID =
			CounterBarrierVisualMesh->CreateDynamicMaterialInstance(0, Base, FName(*UniqueName));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounter: FlowPlane base material missing and could not be loaded."));
	}
}

// Called when the game starts or when spawned
void AFlowCounter::BeginPlay()
{
	Super::BeginPlay();

	// Here we need to tell the statistic subsystem that this flow counter exists and is in the world
	AddFlowCounterToSubsystem();

	// we need to bind to the time dilation subsystem to get the current simulation time
	if (UTimeDilationSubSystem* TimeDilationSub = GetWorld()->GetSubsystem<UTimeDilationSubSystem>())
	{
		TimeDilationSub->OnNewCurrentTime.AddDynamic(this, &AFlowCounter::NewSimTime);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounter: Time Dilation Subsystem not found!"));
	}

	// Setup the bucket segments based on the number of segments property
	SetupBucketSegments();

	// Setup the rolling average arrays
	SetupRollingAverageArrays();
	
	LastSimSecondProcessed = FMath::FloorToInt(CurrentSimTime) - 1; // so first Update advances to "now"

	SetInitialPlacementInFrontOfUser();
}

void AFlowCounter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bTearingDown.Store(true);   // visible to all threads

	// 1) Unregister from subsystems so no NEW calls are scheduled
	RemoveFlowCounterToSubsystem();

	if (UWorld* World = GetWorld())
	{
		if (UTimeDilationSubSystem* Time = World->GetSubsystem<UTimeDilationSubSystem>())
		{
			Time->OnNewCurrentTime.RemoveDynamic(this, &AFlowCounter::NewSimTime);
		}

		// Cancel pending colour-reset timer so it can't fire on a destroyed actor
		World->GetTimerManager().ClearTimer(FlowColorResetHandle);
	}

	// 2) Drain any internal queues so Tick (or anyone) won’t commit after teardown
	{ FFlowCrossingResult Tmp; while (ThreadSafeResults.Dequeue(Tmp)) {} }

	// 3) Optionally: take exclusive locks and clear containers to help catch misuse in debug
	{
		FWriteScopeLock _(AgentsMapRW);
		AgentsPassedThroughCounter.Empty();
	}
	{
		FWriteScopeLock _(TrackedPrevMapRW);
		PreviousTrackedAgentLocations.Empty();
	}

	Super::EndPlay(EndPlayReason);
}

// Called every frame
void AFlowCounter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsFlowCounterActive)
	{
		FFlowCrossingResult R;
		while (ThreadSafeResults.Dequeue(R))
		{
			// 1) Time-window gate: drop anything that happened “in the future”
			//    relative to the CURRENT timeline after scrubs.
			if (R.SampleTime > CurrentSimTime)
			{
				continue; // stale due to rewind
			}
			else
			{
				FWriteScopeLock _(AgentsMapRW);
				// 2) Commit counted agent (safe: GT only)
				AgentsPassedThroughCounter.Add(
					R.AgentID,
					FFlowCounterCountedAgentData(R.SampleTime, R.IntersectionOnLine, R.IntersectionThreshold)
				);
				FlowCounterCount.store(AgentsPassedThroughCounter.Num());
			
				AssignAgentToBucketUsingThresholdWithTime(R.AgentID, R.IntersectionThreshold, R.SampleTime);
			}
		}
	}
	else
	{
		// to avoid results stacking up just completely empty the list
		ThreadSafeResults.Empty();
	}
	
}

void AFlowCounter::MoveGatePillarMeshToLocation(int32 PillarIndex, const FVector& NewLocation)
{
	if (PillarIndex != 0 && PillarIndex != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid Pillar Index! Must be 0 or 1."));
		return;
	}
	//TODO: likely need to communicate pillars location changing wait for changes to mass ai so we don't get data corruption/mismatches
	
	if (PillarIndex == 0 && FlowCounterPillarMesh1)
	{
		FlowCounterPillarMesh1->SetWorldLocation(NewLocation);
	}
	else if (PillarIndex == 1 && FlowCounterPillarMesh2)
	{
		FlowCounterPillarMesh2->SetWorldLocation(NewLocation);
	}

	// After moving the pillar mesh, we need to update the trigger box to match the new pillar locations
	UpdateFlowCounterTriggerBox();

	// Reset the flow counter count and agents passed through counter
	ResetFlowCounterTrackingData();

	// TODO: depending on mass ai logic we may want to reset the agents passed through counter as well

	// Communicate to the statistic subsystem that the flow counter has been updated
	AddFlowCounterToSubsystem();
}

void AFlowCounter::ResizeFlowCounterTriggerBox(float& OutDistanceBetweenPillars, FVector& OutCenterLocation, FVector& OutBoxExtents, FRotator& OutBoxRotation) const
{
	// Validate pillar meshes
	if (!FlowCounterPillarMesh1 || !FlowCounterPillarMesh2)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowCounter Pillar Meshes are not valid!"));
		return;
	}

	// Distance between pillar centers (fallback to relative if world distance is invalid)
	OutDistanceBetweenPillars = FVector::Dist(
		FlowCounterPillarMesh1->GetComponentLocation(),
		FlowCounterPillarMesh2->GetComponentLocation());

	if (OutDistanceBetweenPillars <= 0.0f)
	{
		OutDistanceBetweenPillars = FVector::Dist(
			FlowCounterPillarMesh1->GetRelativeLocation(),
			FlowCounterPillarMesh2->GetRelativeLocation());
	}

	// Box center is the midpoint between the two pillar centers
	OutCenterLocation = (FlowCounterPillarMesh1->GetComponentLocation() +
		FlowCounterPillarMesh2->GetComponentLocation()) * 0.5f;

	// Box rotation is aligned with the line spanning the two pillars
	OutBoxRotation = (FlowCounterLineEndLocation - FlowCounterLineStartLocation).Rotation();

	// Get the lower of the two pillar centers
	FVector LowestPillarLocation = (FlowCounterPillarMesh1->GetComponentLocation().Z <
		                               FlowCounterPillarMesh2->GetComponentLocation().Z)
		                               ? FlowCounterPillarMesh1->GetComponentLocation()
		                               : FlowCounterPillarMesh2->GetComponentLocation();

	// Shift down by half pillar height (100 cm) → pillar origin is its center
	LowestPillarLocation.Z -= 100.0f;

	// Compute how far the lowest base extends beyond the current box extent:
	//   1. Take the box's up direction and move 100 cm down from the center
	//   2. Measure the XY offset from that reference point to the lowest pillar base
	FVector RefBaseAtCenter = LowestPillarLocation - OutBoxRotation.RotateVector(FVector::UpVector) * 100.0f;
	FVector ToLowestBase = LowestPillarLocation - RefBaseAtCenter;
	ToLowestBase.Z = 0.0f; // Only care about horizontal span
	float ExtraLength = ToLowestBase.Size();

	// Final box extents (UE uses half sizes):
	//   X = half the distance between pillars + extra reach to cover lowest base
	//   Y = fixed thickness (50 cm → 25 half-extent)
	//   Z = fixed height (110 cm total → 55 half-extent)
	OutBoxExtents = FVector(OutDistanceBetweenPillars * 0.5f + ExtraLength,
	                        50.0f,
	                        110.0f);
}

void AFlowCounter::ResizeFlowCounterTriggerBoxExtent(const FVector& NewExtent)
{
}

void AFlowCounter::UpdateFlowCounterTriggerBoxLocation(const FVector& NewLocation)
{
}

void AFlowCounter::UpdateFlowCounterTriggerBox()
{
	if (FlowCounterTriggerBox == nullptr ||
		FlowCounterPillarMesh1 == nullptr ||
		FlowCounterPillarMesh2 == nullptr)
	{
		return;// Early exit if any required component is missing
	}
	// Update the FlowCounterLineStartLocation and FlowCounterLineEndLocation based on the pillar locations
	FlowCounterLineStartLocation = FlowCounterPillarMesh1->GetComponentLocation();
	FlowCounterLineEndLocation = FlowCounterPillarMesh2->GetComponentLocation();
	
	float DistanceBetweenPillars;
	FVector CenterLocation;
	FVector BoxExtents;
	FRotator BoxRotation;
	
	ResizeFlowCounterTriggerBox(DistanceBetweenPillars, CenterLocation, BoxExtents, BoxRotation);

	
	// Center Location should also be the root components location, that way child objects that aren't dynamic can update with it
	RootComponent->SetWorldLocation(CenterLocation);
	
	//FlowCounterTriggerBox->SetBoxExtent(FVector(DistanceBetweenPillars / 2.0f, 50.0f, 100.0f));
	FlowCounterTriggerBox->SetBoxExtent(BoxExtents);
	
	// We may want to offset the box location in Z 
	FlowCounterTriggerBox->SetWorldLocation(CenterLocation);

	//TODO: Our z limits shouldn't be based on the trigger box extents as if the pillars are at different z locations, the trigger box will be rotated
	// and the extents will not be accurate for the z limits we want to use for the line intersection check

	// --- Compute absolute Z bounds from the two pillars (base -> top) ---
	// NOTE: your cylinder pillars are centered; total pillar height ~200 cm => half = 100 cm + 10 cm buffer
	constexpr float PillarHalfHeightCm = 110.0f;

	const FVector P1 = FlowCounterPillarMesh1->GetComponentLocation();
	const FVector P2 = FlowCounterPillarMesh2->GetComponentLocation();

	const float P1BaseZ = P1.Z - PillarHalfHeightCm;
	const float P1TopZ  = P1.Z + PillarHalfHeightCm;
	const float P2BaseZ = P2.Z - PillarHalfHeightCm;
	const float P2TopZ  = P2.Z + PillarHalfHeightCm;

	// Coarse world-Z band used by StatisticSubsystem::IsAgentLocationInAFlowCounterBand
	FlowCounterZSearchLimits.MinZBounds = FMath::Min(P1BaseZ, P2BaseZ);
	FlowCounterZSearchLimits.MaxZBounds = FMath::Max(P1TopZ,  P2TopZ);
	
	
	// We only want to rotate around the yaw axis, so we set pitch and roll to 0 for the trigger box
	//FRotator BoxRotationYawOnly = FRotator(0.0f, ActorRotation.Yaw, 0.0f); // Only use yaw rotation to keep box upright
	FlowCounterTriggerBox->SetWorldRotation(BoxRotation);

	// Root component should be updated to reflect the same orientation as the trigger box
	RootComponent->SetWorldRotation(BoxRotation);

	// up vector should always be world up
	const FVector Up = FVector::UpVector;
	const float   Height = 100.f;                      // Half height of the barrier visual mesh (also the pillars half height)

	const FVector A_w = FlowCounterLineStartLocation - Up * Height;
	const FVector B_w = FlowCounterLineEndLocation - Up * Height;
	const FVector D_w = A_w + Up * (Height *2);
	const FVector C_w = B_w + Up * (Height *2);

	// Transform world → *component local* before calling SetCorners
	const FTransform ToLocal = CounterBarrierVisualMesh->GetComponentTransform().Inverse();
	const FVector A_l = ToLocal.TransformPosition(A_w);
	const FVector B_l = ToLocal.TransformPosition(B_w);
	const FVector C_l = ToLocal.TransformPosition(C_w);
	const FVector D_l = ToLocal.TransformPosition(D_w);

	CounterBarrierVisualMesh->SetCorners(A_l, B_l, C_l, D_l);

	// After updating the trigger box, we need to reset the flow counter tracking data
	SetupBucketSegments();
}

bool AFlowCounter::ProcessAgentFlowCrossing(const FFlowCounterData& Data)
{
	if (bTearingDown.Load()) return false;

	TWeakObjectPtr<AFlowCounter> WeakThis(this); // captured by AsyncTask lambdas further down
	AFlowCounter* FlowCounter = this;

	if (!FlowCounter->bIsFlowCounterActive)
	{
		return false;
	}

	FWriteScopeLock LockPrev(TrackedPrevMapRW);
	FReadScopeLock  LockCounted(AgentsMapRW);

	// We need to store the current sim time when we start processing agents - as this could change while processing
	float ProcessTime = FlowCounter->CurrentSimTime;

	if (FlowCounter->FlowCounterTriggerBox == nullptr)
	{
		return false;
	}

	// Get the flow counter trigger box so we can check if the agent is within the box
	UE::Math::TBox FlowCounterBox = FlowCounter->FlowCounterTriggerBox->Bounds.GetBox();

	// check if new agent is already been added to the completed agent set - if so we can skip it
	if (FlowCounter->AgentsPassedThroughCounter.Contains(Data.AgentID))
	{
		// Agent has already been processed, skip it
		return true;
	}

	// check if the agent is within the flow counter trigger box
	if (FMath::PointBoxIntersection(Data.Location, FlowCounterBox))
	{
		// check if the agent is already tracked
		if (FlowCounter->PreviousTrackedAgentLocations.Contains(Data.AgentID))
		{
			FPreviousTrackedAgentLocation* PrevTracked = FlowCounter->PreviousTrackedAgentLocations.Find(Data.AgentID);
			if (PrevTracked == nullptr)
			{
				// This should never happen as we already checked if the agent is contained in the map
				UE_LOG(LogTemp, Warning, TEXT("FlowCounter: PreviousTrackedAgentLocation is null for AgentID %d"), Data.AgentID);
				return false;
			}

			if (PrevTracked->LastKnownSimTime >= ProcessTime)
			{
				// Agent should not be processed as previous data is from the future, so we need to update the previous tracked data and exit
				PrevTracked->LastKnownLocation = Data.Location;
				PrevTracked->LastKnownSimTime = Data.SimTime;
				return false;
			}
			FVector PreviousLocation = PrevTracked->LastKnownLocation;

			// TODO: this is a temporary fix to account for the Z search limits being in world space and the line intersection check being in local space
			// we need to convert the flow counter line start and end locations to be correct as the pillar mesh origins are center not base
			//const FVector A = FVector(FlowCounter->FlowCounterLineStartLocation.X, FlowCounter->FlowCounterLineStartLocation.Y, FlowCounter->FlowCounterLineStartLocation.Z - FlowCounterZSearchLimits.MinZBounds);
			//const FVector B = FVector(FlowCounter->FlowCounterLineEndLocation.X, FlowCounter->FlowCounterLineEndLocation.Y, FlowCounter->FlowCounterLineEndLocation.Z - FlowCounterZSearchLimits.MinZBounds);

			const FVector A = FlowCounter->FlowCounterLineStartLocation;
			const FVector B = FlowCounter->FlowCounterLineEndLocation;
			
			// perform line intersection check to see if the agent has crossed the flow counter line
			FVector CurrentLocation = Data.Location;

			FVector IntersectionOnLine = FVector::ZeroVector;
			float   TOnLine = 0.0f;

			bool bAgentCrossed = SegmentCrossesGateProjectToLine(PreviousLocation, CurrentLocation, A, B, FlowCounter->FlowCounterZSearchLimits,
			                                                     IntersectionOnLine, TOnLine,
			                                                     /*PlaneLateralTolerance=*/4.0f,
			                                                     /*ZTolerance=*/1.0f);
			

			// bool bAgentCrossed = FMath::SegmentIntersection2D(*PreviousLocation, CurrentLocation,
			//                                                   FlowCounter->FlowCounterLineStartLocation, FlowCounter->FlowCounterLineEndLocation, IntersectionLocation);

			// if process time is greater than the current sim time, we need to exit as the user has rewound time and any agents being processed are no longer valid
			if (ProcessTime > FlowCounter->CurrentSimTime)
			{				
				return false;
			}
			
			// if we intersect, then add it to the completed agent set and increment the flow counter
			if (bAgentCrossed)
			{
				FFlowCrossingResult R;
				R.AgentID                = Data.AgentID;
				R.IntersectionOnLine     = IntersectionOnLine;
				R.IntersectionThreshold  = TOnLine;
				R.SampleTime             = Data.SimTime;

				ThreadSafeResults.Enqueue(MoveTemp(R));
				
				AsyncTask(ENamedThreads::GameThread, [WeakThis, Data, TOnLine]()
				{
					if (AFlowCounter* Self = WeakThis.Get())
					{
						Self->FlashBarrierColor();
						//Self->AssignAgentToBucketUsingThreshold(Data.AgentID, TOnLine);
					}
				});
			}
			else
			{
				// Agent has not crossed the line, update the previous tracked agent location with the new location
				FlowCounter->PreviousTrackedAgentLocations[Data.AgentID].LastKnownLocation = Data.Location;
				FlowCounter->PreviousTrackedAgentLocations[Data.AgentID].LastKnownSimTime = Data.SimTime;
			}
		}
		else
		{
			// Agent is not tracked, add it
			FPreviousTrackedAgentLocation New;
			New.LastKnownLocation = Data.Location;
			New.LastKnownSimTime = Data.SimTime;
			FlowCounter->PreviousTrackedAgentLocations.Add(Data.AgentID, New);
		}
	}
	// check that we weren't already tracking the agent in case movement extends pass the trigger box
	else if (FlowCounter->PreviousTrackedAgentLocations.Contains(Data.AgentID)) // TODO: check if we actually want to check if intersected or disregard 
	{
		FPreviousTrackedAgentLocation* PrevTracked = FlowCounter->PreviousTrackedAgentLocations.Find(Data.AgentID);
		if (PrevTracked == nullptr)
		{
			// This should never happen as we already checked if the agent is contained in the map
			UE_LOG(LogTemp, Warning, TEXT("FlowCounter: PreviousTrackedAgentLocation is null for AgentID %d"), Data.AgentID);
			return false;
		}

		if (PrevTracked->LastKnownSimTime >= ProcessTime)
		{
			// Agent should not be processed as previous data is from the future, so we need to update the previous tracked data and exit
			PrevTracked->LastKnownLocation = Data.Location;
			PrevTracked->LastKnownSimTime = Data.SimTime;
			return false;
		}
		FVector PreviousLocation = PrevTracked->LastKnownLocation;

		// TODO: this is a temporary fix to account for the Z search limits being in world space and the line intersection check being in local space -> also minz may not be correctly set
		// we need to convert the flow counter line start and end locations to be correct as the pillar mesh origins are center not base
		const FVector A = FlowCounter->FlowCounterLineStartLocation;
		const FVector B = FlowCounter->FlowCounterLineEndLocation;
		
		// perform line intersection check to see if the agent has crossed the flow counter line
		FVector CurrentLocation = Data.Location;

		FVector IntersectionOnLine = FVector::ZeroVector;
		float   TOnLine = 0.0f;

		bool bAgentCrossed = SegmentCrossesGateProjectToLine(PreviousLocation, CurrentLocation, A, B, FlowCounter->FlowCounterZSearchLimits,
		                                                     IntersectionOnLine, TOnLine,
		                                                     /*PlaneLateralTolerance=*/4.0f,
		                                                     /*ZTolerance=*/1.0f);
			

		// bool bAgentCrossed = FMath::SegmentIntersection2D(*PreviousLocation, CurrentLocation,
		//                                                   FlowCounter->FlowCounterLineStartLocation, FlowCounter->FlowCounterLineEndLocation, IntersectionLocation);

		// if process time is greater than the current sim time, we need to exit as the user has rewound time and any agents being processed are no longer valid
		if (ProcessTime > FlowCounter->CurrentSimTime)
		{			
			return false;
		}
		
		// if we intersect, then add it to the completed agent set and increment the flow counter
		if (bAgentCrossed)
		{
			FFlowCrossingResult R;
			R.AgentID                = Data.AgentID;
			R.IntersectionOnLine     = IntersectionOnLine;
			R.IntersectionThreshold  = TOnLine;
			R.SampleTime             = Data.SimTime;

			ThreadSafeResults.Enqueue(MoveTemp(R));
				
			AsyncTask(ENamedThreads::GameThread, [WeakThis, Data, TOnLine]()
			{
				if (AFlowCounter* Self = WeakThis.Get())
				{
					Self->FlashBarrierColor();
					//Self->AssignAgentToBucketUsingThreshold(Data.AgentID, TOnLine);
				}
			});
		}

		// if we intersect or not we want to remove it from the previous tracked agent locations as we are no longer tracking it and likely moving away from the flow counter
		FlowCounter->PreviousTrackedAgentLocations.Remove(Data.AgentID);
	}
	else
	{
		return true; // Agent is not within the flow counter trigger box, skip it
	}
	
	return false;
}

bool AFlowCounter::HasAgentAlreadyPassedThrough(int32 AgentID) const
{
	FReadScopeLock _(AgentsMapRW);
	return AgentsPassedThroughCounter.Contains(AgentID);
}

void AFlowCounter::NewAgentData(TArray<FFlowCounterData>& NewAgentData)
{
	/* We want to process the new agent data and check if the agent is within the flow counter trigger box
	 * - the data passed here is expected to be within the Z bounds of the flow counter trigger box
	 * (this is due to the potential amount of agent data passed and multiple flow counters may be present).
	 */

	
	for (FFlowCounterData& Data : NewAgentData)
	{
		ProcessAgentFlowCrossing(Data);

		//if (ProcessAgentFlowCrossing(Data)) continue;
		
	};

	// DEBUG
	//UE_LOG(LogTemp, Warning, TEXT("Flow Counter Count: %d"), FlowCounterCount.Load());//just log output till we get a UI to display the count
}

void AFlowCounter::AddFlowCounterToSubsystem()
{
	if (GetWorld() == nullptr){return;}  
	// We want to notify the statistic subsystem of the flow counter update passing through the upper and lower limits and a ptr to this
	
	auto* StatActorManagerSubsystem = GetWorld()->GetSubsystem<UStatisticActorManagementSubsystem>();
	if (StatActorManagerSubsystem != nullptr)
	{
		StatActorManagerSubsystem->AddFlowCounter(this);		
	}
}

void AFlowCounter::RemoveFlowCounterToSubsystem()
{
	if (GetWorld() == nullptr){return;}  
	// We want to notify the statistic subsystem of the flow counter update passing through the upper and lower limits and a ptr to this
	
	auto* StatActorManagerSubsystem = GetWorld()->GetSubsystem<UStatisticActorManagementSubsystem>();
	if (StatActorManagerSubsystem != nullptr)
	{
		StatActorManagerSubsystem->RemoveFlowCounter(this);		
	}
}

void AFlowCounter::ResetFlowCounterTrackingData()
{
	// Reset the flow counter count to 0
	FlowCounterCount.exchange(0);
	// Clear the previous tracked agent locations
	{
		FWriteScopeLock _(TrackedPrevMapRW);
		PreviousTrackedAgentLocations.Empty();
	}
	// Clear the agents passed through counter
	{
		FWriteScopeLock _(AgentsMapRW);
		AgentsPassedThroughCounter.Empty();
	}
	// reset global and per-segment rolling windows
	ResetRolling5s();
	ReinitPerSegmentRolling(NumberOfBucketSegments);

	for (FFlowCounterBucketData& Bucket : FlowCounterBucketData)
	{
		Bucket.AgentIDs.Empty();
		Bucket.AgentCount = 0;
	}
}

void AFlowCounter::NewSimTime(float UpdatedTime)
{
	// TODO: until we handle new sim time for large amounts of actors then we should only 
	// update when there is an active flow counter
	if (!bIsFlowCounterActive)
	{
		return;
	}
	FScopeLock _(&FlowStateCS);

	const bool bRewind = (UpdatedTime < CurrentSimTime);
	CurrentSimTime = UpdatedTime;

	if (!bRewind)
	{
		const int32 NewSec = FMath::FloorToInt(CurrentSimTime);

		// First-time init if we spawned late
		if (LastSimSecondProcessed == TNumericLimits<int32>::Min())
		{
			LastSimSecondProcessed = NewSec - 1;
		}

		// Advance once per whole simulated second
		for (int32 sec = LastSimSecondProcessed + 1; sec <= NewSec; ++sec)
		{
			AdvanceRollingWindowToSecond(sec);

			// notify widgets: total + per-bucket current rolling totals
			OnSimSecondUpdate.Broadcast(
				sec,
				RollingWindowTotal,
				SegmentWindowTotals // TArray<int32> by value is fine here
			);
		}

		LastSimSecondProcessed = NewSec;
		return;
	}

	// ---- Backward scrub (rewind) ----
	// (keep your existing queue drain, kept <= time, bucket rebuild)
	{
		FFlowCrossingResult Tmp; while (ThreadSafeResults.Dequeue(Tmp)) {}
	}
	decltype(AgentsPassedThroughCounter) Kept;
	{
		FReadScopeLock Lock1(AgentsMapRW);
		Kept.Reserve(AgentsPassedThroughCounter.Num());
		for (const auto& Kvp : AgentsPassedThroughCounter)
		{
			if (Kvp.Value.TimePassedThroughCounter <= CurrentSimTime)
			{ Kept.Add(Kvp.Key, Kvp.Value); }
		}
	}
	{
		FWriteScopeLock Lock2(AgentsMapRW);
		AgentsPassedThroughCounter = MoveTemp(Kept);
		FlowCounterCount.store(AgentsPassedThroughCounter.Num());
	}
	{ FWriteScopeLock Lock3(TrackedPrevMapRW); PreviousTrackedAgentLocations.Reset(); }

	// reset global and per-segment rolling windows
	ResetRolling5s();
	ReinitPerSegmentRolling(NumberOfBucketSegments);

	// align second cursor to new time
	LastSimSecondProcessed = FMath::FloorToInt(CurrentSimTime);

	UpdateFlowBucketsWithCurrentAgentsFromTimeChange();
}

void AFlowCounter::SetCorners(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
{
	if (!CounterBarrierVisualMesh) return;
	CounterBarrierVisualMesh->SetCorners(A, B, C, D);      // this updates bounds + pushes verts to GPU (your component handles it)
}

void AFlowCounter::SetSize(float Width, float Height)
{
	if (!CounterBarrierVisualMesh) return;
	CounterBarrierVisualMesh->Initialize(Width, Height);   // rebuilds the 4 (8 with back-face) verts in local space
}

void AFlowCounter::FlashBarrierColor()
{
	if (!IsValid(CounterBarrierVisualMID)) return;

	// show RED immediately
	CounterBarrierVisualMID->SetVectorParameterValue(FlowColorParam, FLinearColor::Red);

	// schedule revert to BLUE after 0.3s (restart if already running)
	FTimerManager& TM = GetWorldTimerManager();
	TM.ClearTimer(FlowColorResetHandle);
	TWeakObjectPtr<AFlowCounter> WeakSelf(this);
	TM.SetTimer(FlowColorResetHandle, [WeakSelf]()
	{
		AFlowCounter* Self = WeakSelf.Get();
		if (Self && IsValid(Self->CounterBarrierVisualMID))
		{
			Self->CounterBarrierVisualMID->SetVectorParameterValue(Self->FlowColorParam, FLinearColor::Blue);
		}
	}, 0.3f, false);
}

void AFlowCounter::AssignAgentsToBuckets(TArray<int32> AllAgents)
{
	for (int32 AgentID : AllAgents)
	{
		AssignAgentToBuckets(AgentID);
	}
}

void AFlowCounter::AssignAgentToBuckets(int32 AgentID)
{
	// GT-only: reads AgentsPassedThroughCounter without AgentsMapRW lock. All current
	// callers run on the game thread (Tick, NewSimTime, BlueprintCallable). Add locking
	// if this ever grows a non-GT caller.
	FFlowCounterCountedAgentData* AgentData = AgentsPassedThroughCounter.Find(AgentID);
		
	// is the data ptr valid ?
	if (AgentData)
	{
		// Find the appropriate bucket for the agent based on their intersection location
		for (int32 i = 0; i < NumberOfBucketSegments; i++)
		{
			// we need to check if the agent's intersection location is within the bucket segment or on the start/end point of the segment
			// TODO: Review this logic with Pete to ensure this is how we should be checking

			// if (IsPointOnLineSegment(BucketData.SegmentStart, BucketData.SegmentEnd, AgentData->IntersectionLocation))
			// {
			// 	// Add the agent to the bucket
			// 	FlowCounterBucketData[i].AgentIDs.Add(AgentID);
			// 	FlowCounterBucketData[i].AgentCount = FlowCounterBucketData[i].AgentIDs.Num();
			// 	break; // Exit the loop once the agent has been assigned to a bucket
			// }

			float IntersctionThreshold = AgentData->IntersectionThreshold;
			if (FlowCounterBucketData[i].StartThreshold <= IntersctionThreshold && IntersctionThreshold < FlowCounterBucketData[i].EndThreshold)
			{
				// Add the agent to the bucket
				FlowCounterBucketData[i].AgentIDs.Add(AgentID);
				FlowCounterBucketData[i].AgentCount = FlowCounterBucketData[i].AgentIDs.Num();
				break; // Exit the loop once the agent has been assigned to a bucket
			}
		}
	}
	else
	{
		// Agent data not found, skip it -> This shouldn't happen but just in case need to add error handling
	}
}

void AFlowCounter::AssignAgentToBucketUsingThreshold(int32 AgentID, float IntersectionThreshold)
{
	const int32 N = NumberOfBucketSegments;
	const int32 BucketIndex = BucketIndex_LeftClosed(IntersectionThreshold, N);
	
	if (FlowCounterBucketData.IsValidIndex(BucketIndex))
	{
		// Add the agent to the bucket
		FlowCounterBucketData[BucketIndex].AgentIDs.Add(AgentID);
		FlowCounterBucketData[BucketIndex].AgentCount = FlowCounterBucketData[BucketIndex].AgentIDs.Num();
	}
}

void AFlowCounter::UpdateNumberOfBucketSegments(int32 NewNumberOfSegments)
{
	// new number of segments must be at least 1
	NumberOfBucketSegments = FMath::Max(1, NewNumberOfSegments);

	// Get all the Agent IDs from all the buckets
	TArray<int32> AllAgents;
	
	for (const FFlowCounterBucketData& Bucket : FlowCounterBucketData)
	{
		AllAgents.Append(Bucket.AgentIDs);
	}
	
	// Re-setup the bucket segments
	SetupBucketSegments();

	// Once we have setup the new bucket segments, we need to reassign the agents to the new buckets
	AssignAgentsToBuckets(AllAgents);
}

void AFlowCounter::RemoveAgentFromBuckets(int32 AgentID)
{
	for (FFlowCounterBucketData& Bucket : FlowCounterBucketData)
	{
		if (Bucket.AgentIDs.Contains(AgentID))
		{
			Bucket.AgentIDs.Remove(AgentID);
			Bucket.AgentCount = Bucket.AgentIDs.Num();
			//TODO: If we store in multiple buckets in the case when start and end locations are the same we may want to remove from all buckets
			//only if we store in this behaviour - currently we don't
			break; // Exit the loop once the agent has been removed from a bucket
		}
	}
}

void AFlowCounter::UpdateFlowBucketsWithCurrentAgentsFromTimeChange()
{
	// GT-only: reads AgentsPassedThroughCounter without AgentsMapRW lock. Called from
	// NewSimTime on the game thread; if this ever grows a non-GT caller, add locking.
	TArray<int32> AllAgents;
	AgentsPassedThroughCounter.GetKeys(AllAgents);

	// Clear all the current bucket data
	for (FFlowCounterBucketData& Bucket : FlowCounterBucketData)
	{
		Bucket.AgentIDs.Empty();
		Bucket.AgentCount = 0;
	}

	// Reassign the agents to the buckets based on their intersection locations
	AssignAgentsToBuckets(AllAgents);
}

void AFlowCounter::SetupBucketSegments()
{
	// Create the number of bucket segments based on the number of segments property
	FlowCounterBucketData.Empty();

	// Ensure we have at least 1 segment
	int32 NumSegments = FMath::Max(1, NumberOfBucketSegments);

	// calculate the width of each segment based on the distance between the two pillars
	float DistanceBetweenPillars = FVector::Dist(FlowCounterLineStartLocation, FlowCounterLineEndLocation);
	float SegmentWidth = DistanceBetweenPillars / NumSegments;

	// Initialize the bucket data array with the specified number of segments
	for (int32 i = 0; i < NumSegments; i++)
	{
		// Calculate the start and end location of each segment
		FVector SegmentStartLocation = FlowCounterLineStartLocation + (FlowCounterLineEndLocation - FlowCounterLineStartLocation).GetSafeNormal() * SegmentWidth * i;
		FVector SegmentEndLocation = FlowCounterLineStartLocation + (FlowCounterLineEndLocation - FlowCounterLineStartLocation).GetSafeNormal() * SegmentWidth * (i + 1);

		// Calculate the start and end threshold for each segment -> used for assigning agents to buckets based on their intersection threshold
		float StartThreshold = (float)i / (float)NumSegments;
		float EndThreshold = (float)(i + 1) / (float)NumSegments;
		
		FlowCounterBucketData.Add(FFlowCounterBucketData(i, SegmentStartLocation, SegmentEndLocation,StartThreshold,EndThreshold));
	}

	// Handles our rolling 5s flow rate section buckets
	ReinitPerSegmentRolling(NumberOfBucketSegments);
}

int32 AFlowCounter::BucketIndex_LeftClosed(float T, int32 N, float Eps)
{
	if (N <= 0 || !FMath::IsFinite(T)) return 0;

	// Ensure T in [0,1]
	T = FMath::Clamp(T, 0.0f, 1.0f);

	// Shift by a tiny epsilon so exact boundaries (k/N) fall into the LOWER bucket.
	// (Except T=0, which clamps to 0 below.)
	const float Scaled = T * float(N) - Eps;

	// Floor then clamp to [0, N-1]. For T=1.0: Scaled = N - Eps -> floor = N-1.
	const int32 Idx = FMath::FloorToInt(Scaled);
	return FMath::Clamp(Idx, 0, N - 1);
}

void AFlowCounter::UpdateLastFiveSecondAgentsHistory()
{
	float CheckTime = CurrentSimTime < 5.0f ? CurrentSimTime : CurrentSimTime - 5.0f;

	// first check the current agents in the LastFiveSecondAgentsHistory map and remove any agents that are older than 5 seconds
	for (auto It = LastFiveSecondAgentsHistory.CreateIterator(); It; ++It)
	{
		// if the agent time is less than the check time or greater than the current time + 5 seconds, remove it from the map
		if (It.Value() < CheckTime || It.Value() > (CurrentSimTime + 5.0f))
		{
			It.RemoveCurrent();
		}
	}
}

void AFlowCounter::SetupRollingAverageArrays()
{
	for (int32 i = 0; i < RollingWindowSeconds; ++i)
	{
		RollingBinCounts[i]  = 0;
		RollingBinSeconds[i] = TNumericLimits<int32>::Min(); // "empty"
	}
}

void AFlowCounter::RecordCrossingForRollingFlowRate(const float SampleTime)
{
	// If the event is already older than our window, ignore it.
	if (SampleTime < (CurrentSimTime - RollingWindowSeconds))
	{
		return;
	}

	const int32 Sec     = FMath::FloorToInt(SampleTime);      // which sim-second this event belongs to
	const int32 SlotIdx = Sec % RollingWindowSeconds;         // ring position

	// If this slot currently represents an older second, evict it before reuse.
	if (RollingBinSeconds[SlotIdx] != Sec)
	{
		RollingWindowTotal -= RollingBinCounts[SlotIdx];
		RollingBinCounts[SlotIdx] = 0;
		RollingBinSeconds[SlotIdx] = Sec;
	}

	++RollingBinCounts[SlotIdx];
	++RollingWindowTotal;
}

void AFlowCounter::AdvanceRollingWindow()
{
	const int32 MinSec = FMath::FloorToInt(CurrentSimTime) - (RollingWindowSeconds - 1);

	for (int32 i = 0; i < RollingWindowSeconds; ++i)
	{
		const int32 KeySec = RollingBinSeconds[i];
		if (KeySec != TNumericLimits<int32>::Min() && KeySec < MinSec)
		{
			RollingWindowTotal -= RollingBinCounts[i];
			RollingBinCounts[i]  = 0;
			RollingBinSeconds[i] = TNumericLimits<int32>::Min();
		}
	}
}

void AFlowCounter::ResetRolling5s()
{
	RollingWindowTotal = 0;
	for (int32 i = 0; i < RollingWindowSeconds; ++i)
	{
		RollingBinCounts[i]  = 0;
		RollingBinSeconds[i] = TNumericLimits<int32>::Min();
	}
}

void AFlowCounter::RewindRollingWindow()
{
}

void AFlowCounter::AdvanceRollingWindowToSecond(int32 CurrentSecond)
{
	// expire global
	const int32 MinSec = CurrentSecond - (RollingWindowSeconds - 1);
	for (int32 i = 0; i < RollingWindowSeconds; ++i)
	{
		const int32 KeySec = RollingBinSeconds[i];
		if (KeySec != TNumericLimits<int32>::Min() && KeySec < MinSec)
		{
			RollingWindowTotal      -= RollingBinCounts[i];
			RollingBinCounts[i]      = 0;
			RollingBinSeconds[i]     = TNumericLimits<int32>::Min();
		}
	}

	// expire each segment
	for (int32 s = 0; s < SegmentBinCounts.Num(); ++s)
	{
		for (int32 i = 0; i < RollingWindowSeconds; ++i)
		{
			const int32 KeySec = SegmentBinSeconds[s][i];
			if (KeySec != TNumericLimits<int32>::Min() && KeySec < MinSec)
			{
				SegmentWindowTotals[s]    -= SegmentBinCounts[s][i];
				SegmentBinCounts[s][i]     = 0;
				SegmentBinSeconds[s][i]    = TNumericLimits<int32>::Min();
			}
		}
	}
}

void AFlowCounter::ReinitPerSegmentRolling(int32 NumSegments)
{
	SegmentBinCounts.SetNum(NumSegments);
	SegmentBinSeconds.SetNum(NumSegments);
	SegmentWindowTotals.SetNum(NumSegments);

	for (int32 s = 0; s < NumSegments; ++s)
	{
		for (int32 i = 0; i < RollingWindowSeconds; ++i)
		{
			SegmentBinCounts[s][i]  = 0;
			SegmentBinSeconds[s][i] = TNumericLimits<int32>::Min();
		}
		SegmentWindowTotals[s] = 0;
	}
}

void AFlowCounter::RecordCrossingForRollingSegment(int32 BucketIndex, float SampleTime)
{
	if (!SegmentBinCounts.IsValidIndex(BucketIndex)) return;

	// ignore events already older than window
	if (SampleTime < (CurrentSimTime - RollingWindowSeconds)) return;

	const int32 Sec     = FMath::FloorToInt(SampleTime);
	const int32 SlotIdx = Sec % RollingWindowSeconds;

	if (SegmentBinSeconds[BucketIndex][SlotIdx] != Sec)
	{
		SegmentWindowTotals[BucketIndex]    -= SegmentBinCounts[BucketIndex][SlotIdx];
		SegmentBinCounts[BucketIndex][SlotIdx]  = 0;
		SegmentBinSeconds[BucketIndex][SlotIdx] = Sec;
	}

	++SegmentBinCounts[BucketIndex][SlotIdx];
	++SegmentWindowTotals[BucketIndex];
}

void AFlowCounter::AssignAgentToBucketUsingThresholdWithTime(int32 AgentID, float Threshold, float SampleTime)
{
	const int32 BucketIdx = BucketIndex_LeftClosed(Threshold, NumberOfBucketSegments);
	if (FlowCounterBucketData.IsValidIndex(BucketIdx))
	{
		FlowCounterBucketData[BucketIdx].AgentIDs.Add(AgentID);
		FlowCounterBucketData[BucketIdx].AgentCount = FlowCounterBucketData[BucketIdx].AgentIDs.Num();
		// Rolling (total + per-segment)
		RecordCrossingForRollingFlowRate(SampleTime);
		RecordCrossingForRollingSegment(BucketIdx, SampleTime);
	}
}

void AFlowCounter::SetInitialPlacementInFrontOfUser()
{
	// Get the player controller
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	// exit early if you cant get the player controller
	if (PlayerController == nullptr) return;

	// get the player camera manager
	APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
	if (CameraManager == nullptr) return;
	
	// get the camera forward direction and location
	FVector CameraLocation = FVector::ZeroVector;
	FVector CameraForward = FVector::ForwardVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	
	CameraManager->GetCameraViewPoint(CameraLocation, CameraRotation);
	CameraForward = CameraRotation.Vector();

	// get the location 1 m in front of the camera
	FVector NewLocation = CameraLocation + CameraForward * 1000.0f;

	// minus z by half the pillar height to place the flow counter central to the camera
	NewLocation.Z -= 100.0f; // half pillar height

	// we want pillar 1 to be left of this location by 75cm and pillar 2 to be right of this location by 75cm
	FVector RightVector = CameraRotation.RotateVector(FVector::RightVector);
	FVector Pillar1Location = NewLocation - RightVector * 75.0f;
	FVector Pillar2Location = NewLocation + RightVector * 75.0f;
	

	MoveGatePillarMeshToLocation(0, Pillar1Location);
	MoveGatePillarMeshToLocation(1, Pillar2Location);
}

void AFlowCounter::ComputeWidgetReverseAndRotation(bool& bReverseOut, FRotator& WidgetWorldRotationOut,
	FVector WidgetWorldLocation) const
{
	bReverseOut = false;
	WidgetWorldRotationOut = FRotator::ZeroRotator;

	// Validate pillars
	if (!FlowCounterPillarMesh1 || !FlowCounterPillarMesh2)
	{
		return; // cannot compute without both pillars
	}

	const FVector P1 = FlowCounterPillarMesh1->GetComponentLocation();
	const FVector P2 = FlowCounterPillarMesh2->GetComponentLocation();

	// Get camera viewpoint
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC) return;

	APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
	if (!CamMgr) return;

	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	CamMgr->GetCameraViewPoint(CamLoc, CamRot);

	ComputeReverseAndRotationUtility(P1, P2, CamLoc, CamRot, WidgetWorldLocation, bReverseOut, WidgetWorldRotationOut);
}

void AFlowCounter::ComputeReverseAndRotationUtility(const FVector Pillar1World, const FVector Pillar2World,
	const FVector CameraWorldLocation, const FRotator CameraWorldRotation, const FVector WidgetWorldLocation,
	bool& bReverseOut, FRotator& WidgetWorldRotationOut)
{
	// --- 1) Decide reverse by projecting pillars onto camera's Right axis ---
	const FVector CamRight = CameraWorldRotation.RotateVector(FVector::RightVector); // viewer's right
	const float S1 = FVector::DotProduct(Pillar1World - CameraWorldLocation, CamRight);
	const float S2 = FVector::DotProduct(Pillar2World - CameraWorldLocation, CamRight);

	// If Pillar1 is more to the viewer's right than Pillar2, then perceived order is "2 then 1" → reverse
	constexpr float Epsilon = 1.0f; // ~1 cm tolerance (tune for your world scale)
	bReverseOut = (S1 > S2 + Epsilon);

	// --- 2) Build a stable widget rotation ---
	// Forward: face the camera horizontally (avoid pitching up/down)
	const FVector ToCam = SafeHorizontal(CameraWorldLocation - WidgetWorldLocation);

	// Gate span direction: pillar1 → pillar2 (or flipped if reversed).
	FVector GateRight = (Pillar2World - Pillar1World);
	GateRight = SafeHorizontal(GateRight);
	if (bReverseOut)
	{
		GateRight *= -1.f; // flip so widget local +X aligns with left→right as displayed
	}

	// Derive Up from Forward × Right (right-handed basis).
	// Order matters: Up = Forward cross Right, then re-orthonormalize Right.
	FVector Up = FVector::CrossProduct(ToCam, GateRight).GetSafeNormal();
	// If degenerate (camera aligned with gate), fall back to world up
	if (Up.IsNearlyZero())
	{
		Up = FVector::UpVector;
	}

	// Rebuild an orthonormal basis (make sure GateRight is perpendicular to ToCam)
	GateRight = FVector::CrossProduct(Up, ToCam).GetSafeNormal();

	// Construct rotation where:
	//   X (Forward) = ToCam  (faces viewer)
	//   Y (Right)   = GateRight (left→right across the gate, obeying reverse)
	//   Z (Up)      = Up
	const FMatrix Basis = FMatrix(
		FPlane(ToCam, 0.f),
		FPlane(GateRight, 0.f),
		FPlane(Up, 0.f),
		FPlane(0.f, 0.f, 0.f, 1.f)
	);

	WidgetWorldRotationOut = Basis.Rotator();
}
