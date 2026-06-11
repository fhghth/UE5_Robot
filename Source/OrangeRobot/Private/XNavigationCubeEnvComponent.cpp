// Fill out your copyright notice in the Description page of Project Settings.

#include "XNavigationCubeEnvComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EnvConfigLoader.h"
#include "GameFramework/Actor.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"

namespace
{
	float ComputeBypassOpportunity(float TargetClearance, float LeftClearance, float RightClearance)
	{
		return FMath::Max3(TargetClearance, LeftClearance, RightClearance);
	}

	float ComputeFacingAlignment(const UPrimitiveComponent* CubeComponent, const USceneComponent* TargetComponent, const FVector& ForwardVector)
	{
		if (!CubeComponent || !TargetComponent)
		{
			return 0.0f;
		}

		const FVector ToTarget = TargetComponent->GetComponentLocation() - CubeComponent->GetComponentLocation();
		const FVector TargetDirection = ToTarget.GetSafeNormal();
		return TargetDirection.IsNearlyZero() ? 0.0f : FVector::DotProduct(ForwardVector, TargetDirection);
	}
}

FVector UXNavigationCubeEnvComponent::GetNavigationForwardVector() const
{
	if (!CubeComponent)
	{
		return FVector::ForwardVector;
	}

	switch (ForwardAxis)
	{
	case ENavigationForwardAxis::PlusX:  return CubeComponent->GetForwardVector();
	case ENavigationForwardAxis::PlusY:  return CubeComponent->GetRightVector();
	case ENavigationForwardAxis::MinusX: return -CubeComponent->GetForwardVector();
	case ENavigationForwardAxis::MinusY: return -CubeComponent->GetRightVector();
	default: return CubeComponent->GetForwardVector();
	}
}

UXNavigationCubeEnvComponent::UXNavigationCubeEnvComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	RayDirections.Reserve(NumRays);
	for (int32 i = 0; i < NumRays; ++i)
	{
		const float Angle = (360.0f / NumRays) * i;
		const float Rad = FMath::DegreesToRadians(Angle);
		RayDirections.Add(FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f));
	}

	UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] Component constructed: %s"), *GetName());
}

void UXNavigationCubeEnvComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bEnvConfigLoaded)
	{
		bEnvConfigLoaded = UEnvConfigLoader::LoadConfigFromCommandLine(this);
		if (bEnvConfigLoaded)
		{
			UE_LOG(LogTemp, Log, TEXT("[NavigationEnv] Runtime env config applied from command line"));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[NavigationEnv] No command-line config, using blueprint/default values"));
		}
	}

	AActor* OwnerActor = GetOwner();
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NavigationEnv] BeginPlay | Component=%s | Owner=%s | CubeComponent=%s | TargetComponent=%s | MoveStepScale=%.2f | TurnStepDegrees=%.2f | ReachTargetDistance=%.2f | MaxObserveDistance=%.2f | MaxSteps=%d | DebugDrawRays=%s | DebugRayDuration=%.2f"),
		*GetName(),
		OwnerActor ? *OwnerActor->GetName() : TEXT("None"),
		CubeComponent ? *CubeComponent->GetName() : TEXT("None"),
		TargetComponent ? *TargetComponent->GetName() : TEXT("None"),
		MoveStepScale,
		TurnStepDegrees,
		ReachTargetDistance,
		MaxObserveDistance,
		MaxSteps,
		bDebugDrawRays ? TEXT("true") : TEXT("false"),
		DebugRayDuration);

	if (!CubeComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] BeginPlay failed: CubeComponent is null"));
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[NavigationEnv] CubeComponent detail | ComponentOwner=%s | Class=%s"),
			CubeComponent->GetOwner() ? *CubeComponent->GetOwner()->GetName() : TEXT("None"),
			*CubeComponent->GetClass()->GetName());

		if (const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(CubeComponent))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[NavigationEnv] Cube primitive detail | CollisionEnabled=%d | ObjectType=%d | SimulatePhysics=%s"),
				static_cast<int32>(PrimitiveComponent->GetCollisionEnabled()),
				static_cast<int32>(PrimitiveComponent->GetCollisionObjectType()),
				PrimitiveComponent->IsSimulatingPhysics() ? TEXT("true") : TEXT("false"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] CubeComponent is not a PrimitiveComponent, movement sweep may be unreliable"));
		}
	}

	if (!TargetComponent && TargetActor)
	{
		 TargetComponent = TargetActor->GetRootComponent();
                if (TargetComponent)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] Auto-assigned TargetComponent from TargetActor %s"), *TargetActor->GetName());
                }
                else
                {
                	UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] BeginPlay failed: TargetComponent TargetActor is null"));
                }
		 
	}
}

int32 UXNavigationCubeEnvComponent::GetObservationDim() const
{
	return 4 + NumRays + 3 + 1 + 1;
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
		PreviousCubeLocation = CubeComponent->GetComponentLocation();
		PreviousTargetDirectionClearance = GetTargetDirectionClearance();
		const float LeftClearance = GetClearanceAtAngleOffset(TargetSideClearanceAngleDegrees);
		const float RightClearance = GetClearanceAtAngleOffset(-TargetSideClearanceAngleDegrees);
		PreviousBypassOpportunity = ComputeBypassOpportunity(PreviousTargetDirectionClearance, LeftClearance, RightClearance);
		PreviousFacingAlignment = ComputeFacingAlignment(Cast<UPrimitiveComponent>(CubeComponent), TargetComponent, GetNavigationForwardVector());
		ConsecutiveDistanceStuckSteps = 0;
		PreviousRayResults = PerformRaycasts();
		PreviousMoveDirectionLocal = FVector::ZeroVector;
		PreviousForwardAction = 0.0f;
		PreviousTurnAction = 0.0f;
		PreviousActionSafety = GetDirectionalClearance(GetNavigationForwardVector(), MoveStepScale * 1.2f);
		UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] Initial distance to target = %.2f"), PreviousDistance);
	}
	else
	{
		PreviousDistance = 0.0f;
		PreviousCubeLocation = FVector::ZeroVector;
		PreviousTargetDirectionClearance = 0.0f;
		PreviousBypassOpportunity = 0.0f;
		PreviousFacingAlignment = 0.0f;
		ConsecutiveDistanceStuckSteps = 0;
		PreviousRayResults.Reset();
		PreviousMoveDirectionLocal = FVector::ZeroVector;
		PreviousForwardAction = 0.0f;
		PreviousTurnAction = 0.0f;
		PreviousActionSafety = 1.0f;
	}
}

void UXNavigationCubeEnvComponent::ResetEnv()
{
	UE_LOG(LogTemp, Warning, TEXT("[NavigationEnv] ResetEnv called"));
	CurrentStep = 0;
	bHasCollided = false;
	PreviousCubeLocation = FVector::ZeroVector;
	PreviousTargetDirectionClearance = 0.0f;
	PreviousBypassOpportunity = 0.0f;
	PreviousFacingAlignment = 0.0f;
	ConsecutiveStuckSteps = 0;
	ConsecutiveDistanceStuckSteps = 0;
	PreviousRayResults.Reset();
	PreviousMoveDirectionLocal = FVector::ZeroVector;
	PreviousForwardAction = 0.0f;
	PreviousTurnAction = 0.0f;
	PreviousActionSafety = 1.0f;

	if (!CubeComponent)
	{
		PreviousDistance = 0.0f;
		UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] ResetEnv failed: CubeComponent is null"));
		return;
	}

	CubeComponent->SetWorldTransform(InitialCubeTransform, false, nullptr, ETeleportType::ResetPhysics);
	PreviousDistance = TargetComponent ? FVector::Dist(CubeComponent->GetComponentLocation(), TargetComponent->GetComponentLocation()) : 0.0f;
	PreviousCubeLocation = CubeComponent->GetComponentLocation();
	PreviousTargetDirectionClearance = (CubeComponent && TargetComponent) ? GetTargetDirectionClearance() : 0.0f;
	const float LeftClearance = (CubeComponent && TargetComponent) ? GetClearanceAtAngleOffset(TargetSideClearanceAngleDegrees) : 0.0f;
	const float RightClearance = (CubeComponent && TargetComponent) ? GetClearanceAtAngleOffset(-TargetSideClearanceAngleDegrees) : 0.0f;
	PreviousBypassOpportunity = ComputeBypassOpportunity(PreviousTargetDirectionClearance, LeftClearance, RightClearance);
	PreviousFacingAlignment = ComputeFacingAlignment(Cast<UPrimitiveComponent>(CubeComponent), TargetComponent, GetNavigationForwardVector());
	ConsecutiveStuckSteps = 0;
	ConsecutiveDistanceStuckSteps = 0;
	PreviousRayResults = PerformRaycasts();
	PreviousMoveDirectionLocal = FVector::ZeroVector;
	PreviousForwardAction = 0.0f;
	PreviousTurnAction = 0.0f;
	PreviousActionSafety = GetDirectionalClearance(GetNavigationForwardVector(), MoveStepScale * 1.2f);
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NavigationEnv] Reset complete | Location=(%.2f, %.2f, %.2f) | PreviousDistance=%.2f"),
		CubeComponent->GetComponentLocation().X,
		CubeComponent->GetComponentLocation().Y,
		CubeComponent->GetComponentLocation().Z,
		PreviousDistance);
}

TArray<float> UXNavigationCubeEnvComponent::PerformRaycasts() const
{
	TArray<float> RayResults;
	RayResults.Reserve(NumRays);

	if (!CubeComponent || !GetWorld())
	{
		return RayResults;
	}

	const FVector Start = CubeComponent->GetComponentLocation();
	const FTransform Transform = CubeComponent->GetComponentTransform();

	for (const FVector& LocalDir : RayDirections)
	{
		const FVector WorldDir = Transform.TransformVectorNoScale(LocalDir).GetSafeNormal();
		const float Clearance = SweepClearance(Start, WorldDir, MaxObserveDistance);
		RayResults.Add(FMath::Clamp(Clearance / MaxObserveDistance, 0.0f, 1.0f));

		if (bDebugDrawRays)
		{
			const FVector DebugEnd = Start + WorldDir * Clearance;
			const FColor RayColor = Clearance < MaxObserveDistance ? FColor::Red : FColor::Green;
			const bool bPersistent = DebugRayDuration > 0.0f;
			DrawDebugDirectionalArrow(GetWorld(), Start, DebugEnd, 16.0f, RayColor, bPersistent, DebugRayDuration, 0, 3.0f);
			DrawDebugPoint(GetWorld(), DebugEnd, 14.0f, RayColor, bPersistent, DebugRayDuration, 0);
		}
	}

	return RayResults;
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
	const float ForwardTargetAlignment = DirectionWorld.IsNearlyZero()
		? 0.0f
		: FVector::DotProduct(GetNavigationForwardVector(), DirectionWorld);

	Observations.Add(DirectionLocal.X);
	Observations.Add(DirectionLocal.Y);
	Observations.Add(DirectionLocal.Z);
	Observations.Add(FMath::Clamp(Distance / MaxObserveDistance, 0.0f, 1.0f));
	Observations.Append(PerformRaycasts());
	Observations.Add(GetTargetDirectionClearance());
	Observations.Add(GetClearanceAtAngleOffset(TargetSideClearanceAngleDegrees));
	Observations.Add(GetClearanceAtAngleOffset(-TargetSideClearanceAngleDegrees));

	const FVector ActionDirectionWorld = PreviousMoveDirectionLocal.IsNearlyZero()
		? GetNavigationForwardVector()
		: CubeComponent->GetComponentTransform().TransformVectorNoScale(PreviousMoveDirectionLocal).GetSafeNormal();
	Observations.Add(GetDirectionalClearance(ActionDirectionWorld, MoveStepScale * 1.2f));
	Observations.Add(ForwardTargetAlignment);
	return Observations;
}

FCollisionShape UXNavigationCubeEnvComponent::GetPerceptionShape() const
{
	if (bUseBoxSweep)
	{
		return FCollisionShape::MakeBox(FVector(PerceptionHalfExtent, PerceptionHalfExtent, PerceptionHalfExtent));
	}

	return FCollisionShape::MakeSphere(PerceptionHalfExtent);
}

float UXNavigationCubeEnvComponent::SweepClearance(const FVector& Start, const FVector& WorldDirection, float TraceDistance) const
{
	if (!CubeComponent || !GetWorld())
	{
		return 0.0f;
	}

	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 0.0f;
	}

	const float EffectiveDistance = FMath::Max(TraceDistance, 1.0f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredComponent(CubeComponent);

	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		Hit,
		Start,
		Start + Direction * EffectiveDistance,
		CubeComponent->GetComponentQuat(),
		ECC_WorldStatic,
		GetPerceptionShape(),
		QueryParams);

	return bHit ? Hit.Distance : EffectiveDistance;
}

float UXNavigationCubeEnvComponent::GetDirectionalClearance(const FVector& WorldDirection, float TraceDistance) const
{
	if (!CubeComponent || !GetWorld())
	{
		return 0.0f;
	}

	const FVector Start = CubeComponent->GetComponentLocation();
	const float Clearance = SweepClearance(Start, WorldDirection, TraceDistance);
	const float EffectiveDistance = FMath::Max(TraceDistance, 1.0f);
	return FMath::Clamp(Clearance / EffectiveDistance, 0.0f, 1.0f);
}

float UXNavigationCubeEnvComponent::GetTargetDirectionClearance() const
{
	if (!CubeComponent || !TargetComponent || !GetWorld())
	{
		return 0.0f;
	}

	const FVector Start = CubeComponent->GetComponentLocation();
	const FVector ToTarget = TargetComponent->GetComponentLocation() - Start;
	const float TargetDistance = ToTarget.Size();
	if (TargetDistance <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return GetDirectionalClearance(ToTarget, TargetDistance);
}

float UXNavigationCubeEnvComponent::GetClearanceAtAngleOffset(float AngleOffsetDegrees) const
{
	if (!CubeComponent || !TargetComponent)
	{
		return 0.0f;
	}

	const FVector Start = CubeComponent->GetComponentLocation();
	const FVector ToTarget = TargetComponent->GetComponentLocation() - Start;
	const float TraceDistance = ToTarget.Size();
	if (TraceDistance <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const FVector LocalTargetDirection = CubeComponent->GetComponentTransform().InverseTransformVectorNoScale(ToTarget.GetSafeNormal());
	const FVector RotatedLocalDirection = FRotator(0.0f, AngleOffsetDegrees, 0.0f).RotateVector(LocalTargetDirection).GetSafeNormal();
	const FVector WorldDirection = CubeComponent->GetComponentTransform().TransformVectorNoScale(RotatedLocalDirection);
	return GetDirectionalClearance(WorldDirection, TraceDistance);
}

float UXNavigationCubeEnvComponent::ComputeRayOpeningReward(const TArray<float>& CurrentRays, const FVector& MoveDirectionLocal)
{
	if (PreviousRayResults.Num() != CurrentRays.Num())
	{
		PreviousRayResults = CurrentRays;
		return 0.0f;
	}

	const FVector MoveDirection2D = FVector(MoveDirectionLocal.X, MoveDirectionLocal.Y, 0.0f).GetSafeNormal();
	if (MoveDirection2D.IsNearlyZero())
	{
		PreviousRayResults = CurrentRays;
		return 0.0f;
	}

	float TotalReward = 0.0f;
	for (int32 i = 0; i < CurrentRays.Num(); ++i)
	{
		const float Delta = CurrentRays[i] - PreviousRayResults[i];
		if (Delta <= RayOpeningThreshold)
		{
			continue;
		}

		const FVector RayDirection2D = FVector(RayDirections[i].X, RayDirections[i].Y, 0.0f).GetSafeNormal();
		const float Alignment = FVector::DotProduct(MoveDirection2D, RayDirection2D);
		if (Alignment > 0.707f)
		{
			TotalReward += RaySuddenOpeningReward * Alignment;
		}
	}

	PreviousRayResults = CurrentRays;
	return TotalReward;
}

void UXNavigationCubeEnvComponent::ApplyAction(const TArray<float>& Action)
{
	if (!CubeComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[ApplyAction] CubeComponent is null!"));
		return;
	}

	UPrimitiveComponent* PrimCube = Cast<UPrimitiveComponent>(CubeComponent);
	if (!PrimCube)
	{
		UE_LOG(LogTemp, Error, TEXT("[ApplyAction] FATAL: CubeComponent is NOT a UPrimitiveComponent! Actual class: %s"), *CubeComponent->GetClass()->GetName());
		return;
	}

	const float ForwardValue = Action.IsValidIndex(0) ? FMath::Clamp(Action[0], -1.0f, 1.0f) : 0.0f;
	const float TurnValue = Action.IsValidIndex(1) ? FMath::Clamp(Action[1], -1.0f, 1.0f) : 0.0f;
	PreviousForwardAction = ForwardValue;
	PreviousTurnAction = TurnValue;
	const float TurnDeltaDegrees = TurnValue * TurnStepDegrees;
	const FQuat DeltaRotation = FRotator(0.0f, TurnDeltaDegrees, 0.0f).Quaternion();
	const FQuat NewRotation = DeltaRotation * PrimCube->GetComponentQuat();
	PrimCube->SetWorldRotation(NewRotation, false, nullptr, ETeleportType::TeleportPhysics);

	PreviousMoveDirectionLocal = FVector(ForwardValue, 0.0f, 0.0f);
	const FVector ActionDirectionWorld = GetNavigationForwardVector();
	const float CurrentActionSafety = GetDirectionalClearance(ActionDirectionWorld, MoveStepScale * 1.2f);
	UE_LOG(LogTemp, Warning, TEXT("[ApplyAction] Action=(Forward=%.2f, Turn=%.2f) | MoveStepScale=%.2f | TurnStepDegrees=%.2f"), ForwardValue, TurnValue, MoveStepScale, TurnStepDegrees);

	const FVector MoveWorld = GetNavigationForwardVector() * ForwardValue * MoveStepScale;

	if (MoveWorld.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ApplyAction] MoveWorld is nearly zero, skipping translation after turn."));
		PreviousMoveDirectionLocal = FVector::ZeroVector;
		bHasCollided = false;
		CurrentStep++;
		return;
	}

	// 记录移动前位置
	FVector StartPos = PrimCube->GetComponentLocation();
	FHitResult MoveHit;
	bool bMoved = PrimCube->MoveComponent(MoveWorld, PrimCube->GetComponentQuat(), true, &MoveHit);
	FVector EndPos = PrimCube->GetComponentLocation();

	// 移动后的射线检测：沿移动方向向前探测一小段距离
	const float PostMoveRayLength = 50.0f; // 可根据立方体尺寸调整
	FVector RayDirection = MoveWorld.GetSafeNormal();
	FVector RayStart = EndPos;
	FVector RayEnd = RayStart + RayDirection * PostMoveRayLength;

	FCollisionQueryParams RayParams;
	RayParams.AddIgnoredComponent(CubeComponent); // 忽略自身

	FHitResult PostMoveHit;
	bool bPostMoveHit = GetWorld()->LineTraceSingleByChannel(
		PostMoveHit,
		RayStart,
		RayEnd,
		ECC_WorldStatic,
		RayParams);

	// 输出移动后射线结果
	if (bPostMoveHit)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ApplyAction] Post-move ray hit: Actor=%s, Distance=%.2f"),
			*PostMoveHit.GetActor()->GetName(), PostMoveHit.Distance);

		// 可视化射线（红色表示命中）
		DrawDebugLine(GetWorld(), RayStart, PostMoveHit.ImpactPoint, FColor::Orange, false, 2.0f, 0, 3.0f);
		DrawDebugSphere(GetWorld(), PostMoveHit.ImpactPoint, 10.0f, 8, FColor::Purple, false, 2.0f);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ApplyAction] Post-move ray: no hit within %.2f cm."), PostMoveRayLength);
		DrawDebugLine(GetWorld(), RayStart, RayEnd, FColor::Cyan, false, 2.0f, 0, 2.0f);
	}

	// 原有的移动结果日志
	UE_LOG(LogTemp, Warning,
		TEXT("[ApplyAction] MoveResult: bMoved=%d | bBlockingHit=%d | HitActor=%s | DistanceMoved=%.2f | Start=%s | End=%s"),
		bMoved, MoveHit.bBlockingHit,
		MoveHit.GetActor() ? *MoveHit.GetActor()->GetName() : TEXT("None"),
		FVector::Dist(StartPos, EndPos),
		*StartPos.ToString(), *EndPos.ToString());

	// 原有的移动路径可视化（绿色=未阻挡，红色=被阻挡）
	DrawDebugLine(GetWorld(), StartPos, EndPos, MoveHit.bBlockingHit ? FColor::Red : FColor::Green, false, 2.0f, 0, 5.0f);
	if (MoveHit.bBlockingHit)
	{
		DrawDebugSphere(GetWorld(), MoveHit.ImpactPoint, 15.0f, 8, FColor::Yellow, false, 2.0f);
		bHasCollided = true;
	}
	else
	{
		// 如果移动 Sweep 未阻挡，但移动后射线却命中，则说明发生了物理穿透！
		if (bPostMoveHit && PostMoveHit.Distance < MoveStepScale * 1.5f) // 距离明显小于移动步长
		{
			UE_LOG(LogTemp, Error, TEXT("[ApplyAction] SWEEP FAILED! Ray detected wall at %.2f cm, but MoveComponent did not block!"), PostMoveHit.Distance);
			bHasCollided = true; // 手动标记为碰撞，用于奖励计算
			// 可选：将立方体回退到碰撞点
			// FVector CorrectedLocation = PostMoveHit.ImpactPoint - RayDirection * 10.0f; // 回退一点
			// PrimCube->SetWorldLocation(CorrectedLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else
		{
			bHasCollided = false;
		}
	}

	PreviousActionSafety = CurrentActionSafety;
	CurrentStep++;
}



float UXNavigationCubeEnvComponent::ComputeReward()
{
	if (!CubeComponent || !TargetComponent)
	{
		PreviousDistance = 0.0f;
		PreviousCubeLocation = FVector::ZeroVector;
		PreviousTargetDirectionClearance = 0.0f;
		PreviousBypassOpportunity = 0.0f;
		PreviousFacingAlignment = 0.0f;
		ConsecutiveStuckSteps = 0;
		ConsecutiveDistanceStuckSteps = 0;
		PreviousRayResults.Reset();
		PreviousMoveDirectionLocal = FVector::ZeroVector;
		PreviousForwardAction = 0.0f;
		PreviousTurnAction = 0.0f;
		PreviousActionSafety = 1.0f;
		return 0.0f;
	}

	const FVector CurrentLocation = CubeComponent->GetComponentLocation();
	const float CurrentDistance = FVector::Dist(CurrentLocation, TargetComponent->GetComponentLocation());
	const float DistanceImprovement = PreviousDistance - CurrentDistance;
	const float CurrentClearance = GetTargetDirectionClearance();
	const float LeftClearance = GetClearanceAtAngleOffset(TargetSideClearanceAngleDegrees);
	const float RightClearance = GetClearanceAtAngleOffset(-TargetSideClearanceAngleDegrees);
	const float RawBypassOpportunity = ComputeBypassOpportunity(CurrentClearance, LeftClearance, RightClearance);
	const float CurrentBypassOpportunity = FMath::Lerp(PreviousBypassOpportunity, RawBypassOpportunity, BypassOpportunityEmaAlpha);
	const float CurrentFacingAlignment = ComputeFacingAlignment(Cast<UPrimitiveComponent>(CubeComponent), TargetComponent, GetNavigationForwardVector());
	const float FacingImprovement = CurrentFacingAlignment - PreviousFacingAlignment;
	const FVector ToTarget = TargetComponent->GetComponentLocation() - CurrentLocation;
	const FVector TargetDirection = ToTarget.GetSafeNormal();
	const FVector ActualMoveDirection = CurrentLocation - PreviousCubeLocation;
	const float ActualMoveTargetAlignment = TargetDirection.IsNearlyZero() || ActualMoveDirection.IsNearlyZero()
		? 0.0f
		: FVector::DotProduct(ActualMoveDirection.GetSafeNormal(), TargetDirection);
	const float MovedDistance = PreviousCubeLocation.IsZero() ? 0.0f : ActualMoveDirection.Size();
	const bool bIsStuck = MovedDistance < StuckDistanceThreshold;
	const TArray<float> CurrentRays = PerformRaycasts();
	const float HeadingGateBase = FMath::Clamp(CurrentFacingAlignment, 0.0f, 1.0f);
	const float HeadingGate = FMath::Pow(HeadingGateBase, HeadingGateExponent);
	const float FacingRewardDecay = FMath::Max(0.0f, 1.0f - 0.001f * ConsecutiveStuckSteps);
	const float CurrentActionSafety = GetDirectionalClearance(GetNavigationForwardVector(), MoveStepScale * 1.2f);
	const float SafetyImprovement = CurrentActionSafety - PreviousActionSafety;
	const bool bTurningInPlace =
		FMath::Abs(PreviousTurnAction * TurnStepDegrees) > EffectiveTurnThresholdDegrees &&
		FMath::Abs(PreviousForwardAction) < 0.1f;
	const bool bUnsafeForward = FMath::Abs(PreviousForwardAction) > 0.2f &&
		(CurrentFacingAlignment < 0.0f || PreviousActionSafety < UnsafeActionSafetyThreshold);

	if (bIsStuck)
	{
		ConsecutiveStuckSteps++;
	}
	else
	{
		ConsecutiveStuckSteps = 0;
	}

	if (DistanceImprovement < DistanceStuckThreshold && !bHasCollided)
	{
		ConsecutiveDistanceStuckSteps++;
	}
	else
	{
		ConsecutiveDistanceStuckSteps = 0;
	}

	float Reward = 0.0f;
	Reward += DistanceImprovement * DistanceRewardScale * HeadingGate;//距离缩短
	Reward += CurrentFacingAlignment * FacingRewardScale * FacingRewardDecay;//静态朝向对齐
	Reward += FacingImprovement * FacingImprovementRewardScale;//朝向改善
	Reward += (CurrentClearance - PreviousTargetDirectionClearance) * ClearanceRewardScale;//目标方向通路开阔度改善
	Reward += (CurrentBypassOpportunity - PreviousBypassOpportunity) * BypassRewardScale;//绕障机会改善
	Reward += ActualMoveTargetAlignment * TargetAlignmentRewardScale;// 实际移动方向-目标对齐奖励
	Reward += ComputeRayOpeningReward(CurrentRays, PreviousMoveDirectionLocal);//射线突开探索奖励

	if (bTurningInPlace && FacingImprovement > 0.0f)
	{
		Reward -= StepPenalty * TurningStepPenaltyScale;
		Reward += SafetyImprovement * TurnSafetyRewardScale;
	}
	else
	{
		Reward -= StepPenalty;
	}

	if (bUnsafeForward)
	{
		float UnsafePenaltyScale = BadForwardPenaltyScale;
		if (PreviousActionSafety < UnsafeActionSafetyThreshold)
		{
			UnsafePenaltyScale *= 2.0f;
		}
		Reward -= UnsafePenaltyScale * FMath::Abs(PreviousForwardAction);
	}

	if (bIsStuck)
	{
		Reward -= StuckPenalty + StuckPenaltyRamp * FMath::Max(0, ConsecutiveStuckSteps - 1);
	}

	if (ConsecutiveDistanceStuckSteps >= MaxDistanceStuckSteps)
	{
		const float DistanceStuckPenaltyScale = 1.0f + 0.1f * (ConsecutiveDistanceStuckSteps - MaxDistanceStuckSteps);
		Reward -= DistanceStuckPenalty * DistanceStuckPenaltyScale;
	}

	if (CurrentFacingAlignment > 0.95f && PreviousFacingAlignment <= 0.95f)
	{
		Reward += 0.2f;
	}

	if (CurrentDistance <= ReachTargetDistance)
	{
		Reward += ReachTargetReward;
	}

	if (bHasCollided)
	{
		Reward -= CollisionPenalty;
	}

	PreviousDistance = CurrentDistance;
	PreviousCubeLocation = CurrentLocation;
	PreviousTargetDirectionClearance = CurrentClearance;
	PreviousBypassOpportunity = CurrentBypassOpportunity;
	PreviousFacingAlignment = CurrentFacingAlignment;
	PreviousActionSafety = CurrentActionSafety;
	return Reward;
}

bool UXNavigationCubeEnvComponent::CheckShouldTruncateForStuck() const
{
	return ConsecutiveStuckSteps >= MaxConsecutiveStuckSteps;
}

bool UXNavigationCubeEnvComponent::CheckReachedTarget() const
{
	if (!CubeComponent || !TargetComponent)
	{
		return false;
	}

	return FVector::Dist(CubeComponent->GetComponentLocation(), TargetComponent->GetComponentLocation()) <= ReachTargetDistance;
}

EAgentStatus UXNavigationCubeEnvComponent::GetStatus_Implementation()
{
	return AgentStatus;
}

void UXNavigationCubeEnvComponent::SetStatus_Implementation(EAgentStatus NewStatus)
{
	AgentStatus = NewStatus;
}

void UXNavigationCubeEnvComponent::Define_Implementation(FInteractionDefinition& OutInteractionDefinition)
{
	const int32 ObsDim = GetObservationDim();
	const int32 ActionDim = GetActionDim();

	TArray<float> ObsLow;
	TArray<float> ObsHigh;
	ObsLow.Init(-1.0f, ObsDim);
	ObsHigh.Init(1.0f, ObsDim);

	TArray<float> ActionLow;
	TArray<float> ActionHigh;
	ActionLow.Init(-1.0f, ActionDim);
	ActionHigh.Init(1.0f, ActionDim);

	OutInteractionDefinition = FInteractionDefinition(
		TInstancedStruct<FSpace>::Make<FBoxSpace>(ObsLow, ObsHigh),
		TInstancedStruct<FSpace>::Make<FBoxSpace>(ActionLow, ActionHigh));
}

void UXNavigationCubeEnvComponent::Act_Implementation(const FInstancedStruct& InAction)
{
	if (AgentStatus != EAgentStatus::Running)
	{
		return;
	}

	const FBoxPoint* BoxAction = InAction.GetPtr<FBoxPoint>();
	if (!BoxAction || BoxAction->Values.Num() < 2)
	{
		return;
	}

	RawHighLevelCommand = FVector2D(
		FMath::Clamp(BoxAction->Values[0], -1.0f, 1.0f),
		FMath::Clamp(BoxAction->Values[1], -1.0f, 1.0f));
}

void UXNavigationCubeEnvComponent::Observe_Implementation(FInstancedStruct& OutObservations)
{
	if (AgentStatus != EAgentStatus::Running)
	{
		OutObservations.InitializeAs<FBoxPoint>(TArray<float>());
		return;
	}

	OutObservations.InitializeAs<FBoxPoint>(CollectObservations());
}
