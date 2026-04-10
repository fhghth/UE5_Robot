// Fill out your copyright notice in the Description page of Project Settings.

#include "XNavigationCubeEnvComponent.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

UXNavigationCubeEnvComponent::UXNavigationCubeEnvComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] Component constructed: %s"), *GetName());
}

void UXNavigationCubeEnvComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* OwnerActor = GetOwner();
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NavigationEnv] BeginPlay | Component=%s | Owner=%s | CubeComponent=%s | TargetComponent=%s | MoveStepScale=%.2f | ReachTargetDistance=%.2f | MaxObserveDistance=%.2f | MaxSteps=%d"),
		*GetName(),
		OwnerActor ? *OwnerActor->GetName() : TEXT("None"),
		CubeComponent ? *CubeComponent->GetName() : TEXT("None"),
		TargetComponent ? *TargetComponent->GetName() : TEXT("None"),
		MoveStepScale,
		ReachTargetDistance,
		MaxObserveDistance,
		MaxSteps);

	if (!CubeComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] BeginPlay failed: CubeComponent is null"));
	}

	if (!TargetComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] BeginPlay failed: TargetComponent is null"));
	}
}

int32 UXNavigationCubeEnvComponent::GetObservationDim() const
{
	return 4;
}

int32 UXNavigationCubeEnvComponent::GetActionDim() const
{
	return 2;
}

void UXNavigationCubeEnvComponent::CaptureInitialTransform()
{
	UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] CaptureInitialTransform called"));

	if (CubeComponent)
	{
		InitialCubeTransform = CubeComponent->GetComponentTransform();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[NavigationEnv] Initial transform captured | Location=(%.2f, %.2f, %.2f)"),
			InitialCubeTransform.GetLocation().X,
			InitialCubeTransform.GetLocation().Y,
			InitialCubeTransform.GetLocation().Z);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] CaptureInitialTransform failed: CubeComponent is null"));
	}

	if (CubeComponent && TargetComponent)
	{
		PreviousDistance = FVector::Dist(CubeComponent->GetComponentLocation(), TargetComponent->GetComponentLocation());
		UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] Initial distance to target = %.2f"), PreviousDistance);
	}
	else
	{
		PreviousDistance = 0.0f;
	}
}

void UXNavigationCubeEnvComponent::ResetEnv()
{
	UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] ResetEnv called"));
	CurrentStep = 0;

	if (!CubeComponent)
	{
		PreviousDistance = 0.0f;
		UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] ResetEnv failed: CubeComponent is null"));
		return;
	}

	CubeComponent->SetWorldTransform(InitialCubeTransform, false, nullptr, ETeleportType::ResetPhysics);
	PreviousDistance = TargetComponent ? FVector::Dist(CubeComponent->GetComponentLocation(), TargetComponent->GetComponentLocation()) : 0.0f;
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NavigationEnv] Reset complete | Location=(%.2f, %.2f, %.2f) | PreviousDistance=%.2f"),
		CubeComponent->GetComponentLocation().X,
		CubeComponent->GetComponentLocation().Y,
		CubeComponent->GetComponentLocation().Z,
		PreviousDistance);
}

TArray<float> UXNavigationCubeEnvComponent::CollectObservations() const
{
	TArray<float> Observations;
	Observations.Reserve(GetObservationDim());

	if (!CubeComponent || !TargetComponent)
	{
		return Observations;
	}

	const FVector CubeLocation = CubeComponent->GetComponentLocation();
	const FVector TargetLocation = TargetComponent->GetComponentLocation();
	const FVector ToTargetWorld = TargetLocation - CubeLocation;
	const float Distance = ToTargetWorld.Size();
	const FVector DirectionWorld = Distance > KINDA_SMALL_NUMBER ? ToTargetWorld / Distance : FVector::ZeroVector;
	const FVector DirectionLocal = CubeComponent->GetComponentTransform().InverseTransformVectorNoScale(DirectionWorld);

	Observations.Add(DirectionLocal.X);
	Observations.Add(DirectionLocal.Y);
	Observations.Add(DirectionLocal.Z);
	Observations.Add(FMath::Clamp(Distance / MaxObserveDistance, 0.0f, 1.0f));
	return Observations;
}

void UXNavigationCubeEnvComponent::ApplyAction(const TArray<float>& Action)
{

	UE_LOG(LogTemp, Error, TEXT("!!! ApplyAction ENTERED !!!"));
	if (!CubeComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyAction: CubeComponent is NULL!"));
		return;
	}

	const float ForwardValue = Action.IsValidIndex(0) ? FMath::Clamp(Action[0], -1.0f, 1.0f) : 0.0f;
	const float RightValue = Action.IsValidIndex(1) ? FMath::Clamp(Action[1], -1.0f, 1.0f) : 0.0f;

	const FVector MoveWorld =
		(CubeComponent->GetForwardVector() * ForwardValue +
		 CubeComponent->GetRightVector() * RightValue) * MoveStepScale;

	CubeComponent->AddWorldOffset(MoveWorld, true);
	CurrentStep++;
}

float UXNavigationCubeEnvComponent::ComputeReward()
{
	if (!CubeComponent || !TargetComponent)
	{
		PreviousDistance = 0.0f;
		return 0.0f;
	}

	const float CurrentDistance = FVector::Dist(CubeComponent->GetComponentLocation(), TargetComponent->GetComponentLocation());
	float Reward = (PreviousDistance - CurrentDistance) * 0.1f;

	if (CurrentDistance <= ReachTargetDistance)
	{
		Reward += 10.0f;
	}

	PreviousDistance = CurrentDistance;
	return Reward;
}

bool UXNavigationCubeEnvComponent::CheckReachedTarget() const
{
	if (!CubeComponent || !TargetComponent)
	{
		return false;
	}

	return FVector::Dist(CubeComponent->GetComponentLocation(), TargetComponent->GetComponentLocation()) <= ReachTargetDistance;
}
