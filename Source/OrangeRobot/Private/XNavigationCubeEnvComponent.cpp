// Fill out your copyright notice in the Description page of Project Settings.

#include "XNavigationCubeEnvComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

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

	AActor* OwnerActor = GetOwner();
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NavigationEnv] BeginPlay | Component=%s | Owner=%s | CubeComponent=%s | TargetComponent=%s | MoveStepScale=%.2f | ReachTargetDistance=%.2f | MaxObserveDistance=%.2f | MaxSteps=%d | DebugDrawRays=%s | DebugRayDuration=%.2f"),
		*GetName(),
		OwnerActor ? *OwnerActor->GetName() : TEXT("None"),
		CubeComponent ? *CubeComponent->GetName() : TEXT("None"),
		TargetComponent ? *TargetComponent->GetName() : TEXT("None"),
		MoveStepScale,
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

	if (!TargetComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[NavigationEnv] BeginPlay failed: TargetComponent is null"));
	}
}

int32 UXNavigationCubeEnvComponent::GetObservationDim() const
{
	return 4 + NumRays;
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
	bHasCollided = false;

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

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredComponent(CubeComponent);

	for (const FVector& LocalDir : RayDirections)
	{
		const FVector WorldDir = Transform.TransformVectorNoScale(LocalDir).GetSafeNormal();
		const FVector End = Start + WorldDir * MaxObserveDistance;

		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(
			Hit,
			Start,
			End,
			ECC_WorldStatic,
			QueryParams);

		const float Distance = bHit ? Hit.Distance : MaxObserveDistance;
		RayResults.Add(FMath::Clamp(Distance / MaxObserveDistance, 0.0f, 1.0f));

		if (bDebugDrawRays)
		{
			const FVector DebugEnd = bHit ? Hit.ImpactPoint : End;
			const FColor RayColor = bHit ? FColor::Red : FColor::Green;
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

	Observations.Add(DirectionLocal.X);
	Observations.Add(DirectionLocal.Y);
	Observations.Add(DirectionLocal.Z);
	Observations.Add(FMath::Clamp(Distance / MaxObserveDistance, 0.0f, 1.0f));
	Observations.Append(PerformRaycasts());
	return Observations;
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
	const float RightValue = Action.IsValidIndex(1) ? FMath::Clamp(Action[1], -1.0f, 1.0f) : 0.0f;
	UE_LOG(LogTemp, Warning, TEXT("[ApplyAction] Action=(%.2f, %.2f) | MoveStepScale=%.2f"), ForwardValue, RightValue, MoveStepScale);

	const FVector MoveWorld = (CubeComponent->GetForwardVector() * ForwardValue +
		CubeComponent->GetRightVector() * RightValue) * MoveStepScale;

	if (MoveWorld.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ApplyAction] MoveWorld is nearly zero, skipping movement."));
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

	if (bHasCollided)
	{
		Reward -= 5.0f;
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
