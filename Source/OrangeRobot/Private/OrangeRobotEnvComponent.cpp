// Fill out your copyright notice in the Description page of Project Settings.

#include "OrangeRobotEnvComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

namespace
{
	const TCHAR* AngularDriveModeToString(EAngularDriveMode::Type DriveMode)
	{
		switch (DriveMode)
		{
		case EAngularDriveMode::SLERP:
			return TEXT("SLERP");
		case EAngularDriveMode::TwistAndSwing:
			return TEXT("TwistAndSwing");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* AngularMotionToString(EAngularConstraintMotion Motion)
	{
		switch (Motion)
		{
		case ACM_Free:
			return TEXT("Free");
		case ACM_Limited:
			return TEXT("Limited");
		case ACM_Locked:
			return TEXT("Locked");
		default:
			return TEXT("Unknown");
		}
	}

	void GetAngularMotions(
		UPhysicsConstraintComponent* Constraint,
		EAngularConstraintMotion& TwistMotion,
		EAngularConstraintMotion& Swing1Motion,
		EAngularConstraintMotion& Swing2Motion)
	{
		TwistMotion = ACM_Locked;
		Swing1Motion = ACM_Locked;
		Swing2Motion = ACM_Locked;

		if (!Constraint)
		{
			return;
		}

		FConstraintInstance& ConstraintInstance = Constraint->ConstraintInstance;
		TwistMotion = ConstraintInstance.GetAngularTwistMotion();
		Swing1Motion = ConstraintInstance.GetAngularSwing1Motion();
		Swing2Motion = ConstraintInstance.GetAngularSwing2Motion();
	}

	FVector GetRotatedYawDirection(const FTransform& ReferenceTransform, float YawDegrees)
	{
		const FVector Forward = ReferenceTransform.GetUnitAxis(EAxis::X);
		return Forward.RotateAngleAxis(YawDegrees, FVector::UpVector).GetSafeNormal();
	}
}

float UOrangeRobotEnvComponent::ShapeNormalizedAction(float Value, float Exponent)
{
	const float ClampedValue = FMath::Clamp(Value, -1.0f, 1.0f);
	const float SafeExponent = FMath::Max(Exponent, 1.0f);
	return FMath::Sign(ClampedValue) * FMath::Pow(FMath::Abs(ClampedValue), SafeExponent);
}

float UOrangeRobotEnvComponent::SanitizeFiniteScalar(float Value, float MinValue, float MaxValue)
{
	if (!FMath::IsFinite(Value))
	{
		return 0.0f;
	}

	return FMath::Clamp(Value, MinValue, MaxValue);
}

FVector UOrangeRobotEnvComponent::SanitizeFiniteVector(const FVector& Value, float MinValue, float MaxValue)
{
	return FVector(
		SanitizeFiniteScalar(Value.X, MinValue, MaxValue),
		SanitizeFiniteScalar(Value.Y, MinValue, MaxValue),
		SanitizeFiniteScalar(Value.Z, MinValue, MaxValue));
}

FVector UOrangeRobotEnvComponent::ClampAngularVelocityTarget(const FVector& TargetVel) const
{
	return FVector(
		SanitizeFiniteScalar(TargetVel.X, -TwistVelocityLimit, TwistVelocityLimit),
		SanitizeFiniteScalar(TargetVel.Y, -SwingVelocityLimit, SwingVelocityLimit),
		SanitizeFiniteScalar(TargetVel.Z, -SwingVelocityLimit, SwingVelocityLimit));
}

UOrangeRobotEnvComponent::UOrangeRobotEnvComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ---------------------------------------------------------------------------
// 约束轴缓存
// ---------------------------------------------------------------------------

FOrangeRobotConstraintAxisCache UOrangeRobotEnvComponent::BuildConstraintAxisCache(UPhysicsConstraintComponent* Constraint) const
{
	FOrangeRobotConstraintAxisCache AxisCache;
	if (!Constraint)
	{
		return AxisCache;
	}

	FConstraintInstanceAccessor Accessor(Constraint);
	TEnumAsByte<EAngularDriveMode::Type> DriveMode = EAngularDriveMode::Type::TwistAndSwing;
	bool bTwistVelocityDriveEnabled = false;
	bool bSwingVelocityDriveEnabled = false;
	bool bSlerpVelocityDriveEnabled = false;

	UConstraintInstanceBlueprintLibrary::GetAngularDriveMode(Accessor, DriveMode);
	UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveTwistAndSwing(
		Accessor,
		bTwistVelocityDriveEnabled,
		bSwingVelocityDriveEnabled);
	UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveSLERP(
		Accessor,
		bSlerpVelocityDriveEnabled);

	EAngularConstraintMotion TwistMotion = ACM_Locked;
	EAngularConstraintMotion Swing1Motion = ACM_Locked;
	EAngularConstraintMotion Swing2Motion = ACM_Locked;
	GetAngularMotions(Constraint, TwistMotion, Swing1Motion, Swing2Motion);

	if (DriveMode == EAngularDriveMode::Type::TwistAndSwing)
	{
		AxisCache.bUseTwist = bTwistVelocityDriveEnabled && TwistMotion != ACM_Locked;
		AxisCache.bUseSwing1 = bSwingVelocityDriveEnabled && Swing1Motion != ACM_Locked;
		AxisCache.bUseSwing2 = bSwingVelocityDriveEnabled && Swing2Motion != ACM_Locked;
	}
	else if (DriveMode == EAngularDriveMode::Type::SLERP)
	{
		const bool bHasAnyAngularFreedom =
			TwistMotion != ACM_Locked || Swing1Motion != ACM_Locked || Swing2Motion != ACM_Locked;

		if (bSlerpVelocityDriveEnabled && bHasAnyAngularFreedom)
		{
			AxisCache.bUseTwist = TwistMotion != ACM_Locked;
			AxisCache.bUseSwing1 = Swing1Motion != ACM_Locked;
			AxisCache.bUseSwing2 = Swing2Motion != ACM_Locked;
		}
	}

	return AxisCache;
}

void UOrangeRobotEnvComponent::CacheJointActionAxes()
{
	JointActionAxes.Empty();
	JointAxisCaches.Empty();
	JointActionAxes.Reserve(DriveConstraints.Num());
	JointAxisCaches.Reserve(DriveConstraints.Num());

	for (int32 Index = 0; Index < DriveConstraints.Num(); ++Index)
	{
		UPhysicsConstraintComponent* Constraint = DriveConstraints[Index];
		const FOrangeRobotConstraintAxisCache AxisCache = BuildConstraintAxisCache(Constraint);
		JointAxisCaches.Add(AxisCache);
		JointActionAxes.Add(AxisCache.GetAxisCount());

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Joint %d (%s) cached action axes = %d [Twist=%s, Swing1=%s, Swing2=%s]"),
			Index,
			Constraint ? *Constraint->GetName() : TEXT("nullptr"),
			AxisCache.GetAxisCount(),
			AxisCache.bUseTwist ? TEXT("true") : TEXT("false"),
			AxisCache.bUseSwing1 ? TEXT("true") : TEXT("false"),
			AxisCache.bUseSwing2 ? TEXT("true") : TEXT("false"));
	}
}

// ---------------------------------------------------------------------------
// 空间维度查询
// ---------------------------------------------------------------------------

int32 UOrangeRobotEnvComponent::GetLowLevelObservationDim() const
{
	// 根状态：位置(3) + 旋转(3) + 线速度(3) + 角速度(3) = 12
	// 每个驱动关节：Twist / Swing1 / Swing2 各自的角度与角速度，共 6
	return 12 + DriveConstraints.Num() * 6;
}

int32 UOrangeRobotEnvComponent::GetObservationDim() const
{
	return GetLowLevelObservationDim() + (bAppendNavigationObservation ? 16 : 0);
}

FVector UOrangeRobotEnvComponent::GetNavigationOrigin() const
{
	if (NavigationReferenceComponent)
	{
		return NavigationReferenceComponent->GetComponentLocation();
	}

	return RobotActor ? RobotActor->GetActorLocation() : FVector::ZeroVector;
}

FTransform UOrangeRobotEnvComponent::GetNavigationReferenceTransform() const
{
	if (NavigationReferenceComponent)
	{
		return NavigationReferenceComponent->GetComponentTransform();
	}

	return RobotActor ? RobotActor->GetActorTransform() : FTransform::Identity;
}

float UOrangeRobotEnvComponent::GetDirectionalClearance(const FVector& WorldDirection, float TraceDistance) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 1.0f;
	}

	const FVector Start = GetNavigationOrigin();
	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 1.0f;
	}

	const float ClampedDistance = FMath::Max(TraceDistance, 1.0f);
	const FVector End = Start + Direction * ClampedDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NavigationSweep), false, RobotActor);
	FCollisionShape CollisionShape = FCollisionShape::MakeBox(NavigationPerceptionHalfExtent);
	FHitResult HitResult;

	const bool bHit = World->SweepSingleByChannel(
		HitResult,
		Start,
		End,
		FQuat::Identity,
		NavigationSweepChannel,
		CollisionShape,
		QueryParams);

	if (!bHit)
	{
		return 1.0f;
	}

	const float HitDistance = FMath::Max(HitResult.Distance, 0.0f);
	return FMath::Clamp(HitDistance / ClampedDistance, 0.0f, 1.0f);
}

TArray<float> UOrangeRobotEnvComponent::CollectNavigationObservations() const
{
	TArray<float> Obs;
	Obs.Reserve(16);

	const FTransform ReferenceTransform = GetNavigationReferenceTransform();
	const FVector Origin = GetNavigationOrigin();
	const FVector TargetLocation = NavigationTargetActor ? NavigationTargetActor->GetActorLocation() : Origin;
	const FVector ToTargetWorld = TargetLocation - Origin;
	const FVector DirectionLocal = ReferenceTransform.InverseTransformVectorNoScale(ToTargetWorld.GetSafeNormal());
	const float Distance = ToTargetWorld.Size();
	const float NormalizedDistance = NavigationMaxObserveDistance > KINDA_SMALL_NUMBER
		? FMath::Clamp(Distance / NavigationMaxObserveDistance, 0.0f, 1.0f)
		: 0.0f;

	Obs.Add(DirectionLocal.X);
	Obs.Add(DirectionLocal.Y);
	Obs.Add(DirectionLocal.Z);
	Obs.Add(NormalizedDistance);

	for (int32 RayIndex = 0; RayIndex < 8; ++RayIndex)
	{
		const float YawDegrees = RayIndex * 45.0f;
		const FVector SweepDirection = GetRotatedYawDirection(ReferenceTransform, YawDegrees);
		Obs.Add(GetDirectionalClearance(SweepDirection, NavigationMaxObserveDistance));
	}

	const FVector TargetDirectionWorld = ToTargetWorld.GetSafeNormal();
	const float TargetYawDegrees = !TargetDirectionWorld.IsNearlyZero()
		? FMath::RadiansToDegrees(FMath::Atan2(DirectionLocal.Y, DirectionLocal.X))
		: 0.0f;
	const float TargetTraceDistance = FMath::Clamp(Distance, 100.0f, NavigationMaxObserveDistance);
	Obs.Add(GetDirectionalClearance(TargetDirectionWorld, TargetTraceDistance));
	Obs.Add(GetDirectionalClearance(
		GetRotatedYawDirection(ReferenceTransform, TargetYawDegrees + TargetSideClearanceAngleDegrees),
		TargetTraceDistance));
	Obs.Add(GetDirectionalClearance(
		GetRotatedYawDirection(ReferenceTransform, TargetYawDegrees - TargetSideClearanceAngleDegrees),
		TargetTraceDistance));

	FVector ActionDirectionWorld = ReferenceTransform.GetUnitAxis(EAxis::X);
	if (LastAction.Num() >= 2)
	{
		const FVector Forward = ReferenceTransform.GetUnitAxis(EAxis::X);
		const FVector Right = ReferenceTransform.GetUnitAxis(EAxis::Y);
		ActionDirectionWorld = (Forward * LastAction[0] + Right * LastAction[1]).GetSafeNormal();
		if (ActionDirectionWorld.IsNearlyZero())
		{
			ActionDirectionWorld = Forward;
		}
	}
	Obs.Add(GetDirectionalClearance(ActionDirectionWorld, NavigationActionLookaheadDistance));

	return Obs;
}

void UOrangeRobotEnvComponent::AppendNavigationObservations(TArray<float>& Obs) const
{
	const TArray<float> NavigationObs = CollectNavigationObservations();
	Obs.Append(NavigationObs);
}

USceneComponent* UOrangeRobotEnvComponent::GetTiltReferenceComponent() const
{
	return TiltCheckComponent ? TiltCheckComponent : (BodyLinks.Num() > 0 ? BodyLinks[0] : nullptr);
}

float UOrangeRobotEnvComponent::GetUprightDot() const
{
	const USceneComponent* TiltComp = GetTiltReferenceComponent();
	const FVector Up = TiltComp ? TiltComp->GetUpVector() : FVector::UpVector;
	return FVector::DotProduct(Up, FVector::UpVector);
}

float UOrangeRobotEnvComponent::GetBodyHeight() const
{
	const USceneComponent* BodyComp = GetTiltReferenceComponent();
	if (BodyComp)
	{
		return BodyComp->GetComponentLocation().Z;
	}

	return RobotActor ? RobotActor->GetActorLocation().Z : 0.0f;
}

bool UOrangeRobotEnvComponent::IsFootTouchingGround(const UPrimitiveComponent* FootComponent) const
{
	if (!FootComponent)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = FootComponent->GetComponentLocation();
	const FVector End = Start - FVector(0.0f, 0.0f, FMath::Max(FootSupportTraceDistance, 1.0f));
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FootSupportTrace), false, RobotActor);
	QueryParams.AddIgnoredComponent(FootComponent);
	FHitResult HitResult;

	return World->LineTraceSingleByChannel(
		HitResult,
		Start,
		End,
		ECC_WorldStatic,
		QueryParams);
}

bool UOrangeRobotEnvComponent::IsFootStableSupport(const UPrimitiveComponent* FootComponent) const
{
	if (!FootComponent)
	{
		return false;
	}

	if (!IsFootTouchingGround(FootComponent))
	{
		return false;
	}

	const FVector FootVelocity = FootComponent->GetComponentVelocity();
	return FootVelocity.Size() <= FootStableSpeedThreshold;
}

bool UOrangeRobotEnvComponent::HasStableFootSupport() const
{
	return IsFootStableSupport(FootL) || IsFootStableSupport(FootR);
}

void UOrangeRobotEnvComponent::LogNavigationObservations() const
{
	const TArray<float> NavigationObs = CollectNavigationObservations();
	FString Joined;
	for (int32 Index = 0; Index < NavigationObs.Num(); ++Index)
	{
		Joined += FString::Printf(TEXT("[%d]=%.4f "), Index, NavigationObs[Index]);
	}
	UE_LOG(LogTemp, Warning, TEXT("Navigation16: %s"), *Joined);
}

int32 UOrangeRobotEnvComponent::GetActionDim() const
{
	int32 TotalAxes = 0;
	for (const int32 Axes : JointActionAxes)
	{
		TotalAxes += Axes;
	}
	return TotalAxes;
}

// ---------------------------------------------------------------------------
// 初始 Transform 记录
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::CaptureInitialTransform()
{
    if (RobotActor)
    {
        InitialRobotTransform = RobotActor->GetActorTransform();
    }

    // 保存每个 BodyLink 的初始世界变换
    InitialBodyLinkTransforms.Empty();
    for (UStaticMeshComponent* Link : BodyLinks)
    {
        if (Link)
        {
            InitialBodyLinkTransforms.Add(Link->GetComponentTransform());
        }
        else
        {
            InitialBodyLinkTransforms.Add(FTransform::Identity);
        }
    }

    CacheJointActionAxes();
    LogDriveConstraintStates();
}
void UOrangeRobotEnvComponent::LogDriveConstraintStates() const
{
	UE_LOG(LogTemp, Warning, TEXT("========== OrangeRobot Drive Constraint Debug =========="));
	UE_LOG(LogTemp, Warning, TEXT("DriveConstraints.Num() = %d"), DriveConstraints.Num());

	for (int32 Index = 0; Index < DriveConstraints.Num(); ++Index)
	{
		UPhysicsConstraintComponent* Constraint = DriveConstraints[Index];
		if (!Constraint)
		{
			UE_LOG(LogTemp, Warning, TEXT("Constraint[%d]: nullptr"), Index);
			continue;
		}

		FConstraintInstanceAccessor Accessor(Constraint);
		TEnumAsByte<EAngularDriveMode::Type> DriveMode = EAngularDriveMode::Type::TwistAndSwing;
		bool bTwistVelocityDriveEnabled = false;
		bool bSwingVelocityDriveEnabled = false;
		bool bSlerpVelocityDriveEnabled = false;

		UConstraintInstanceBlueprintLibrary::GetAngularDriveMode(Accessor, DriveMode);
		UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveTwistAndSwing(
			Accessor,
			bTwistVelocityDriveEnabled,
			bSwingVelocityDriveEnabled);
		UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveSLERP(
			Accessor,
			bSlerpVelocityDriveEnabled);

		EAngularConstraintMotion TwistMotion = ACM_Locked;
		EAngularConstraintMotion Swing1Motion = ACM_Locked;
		EAngularConstraintMotion Swing2Motion = ACM_Locked;
		GetAngularMotions(Constraint, TwistMotion, Swing1Motion, Swing2Motion);

		const FOrangeRobotConstraintAxisCache AxisCache = BuildConstraintAxisCache(Constraint);

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Constraint[%d] Name=%s | DriveMode=%s | VelDrive(Twist=%s, Swing=%s, SLERP=%s) | Motion(Twist=%s, Swing1=%s, Swing2=%s) | CachedAxes=%d [X=%s,Y=%s,Z=%s]"),
			Index,
			*Constraint->GetName(),
			AngularDriveModeToString(DriveMode.GetValue()),
			bTwistVelocityDriveEnabled ? TEXT("true") : TEXT("false"),
			bSwingVelocityDriveEnabled ? TEXT("true") : TEXT("false"),
			bSlerpVelocityDriveEnabled ? TEXT("true") : TEXT("false"),
			AngularMotionToString(TwistMotion),
			AngularMotionToString(Swing1Motion),
			AngularMotionToString(Swing2Motion),
			AxisCache.GetAxisCount(),
			AxisCache.bUseTwist ? TEXT("true") : TEXT("false"),
			AxisCache.bUseSwing1 ? TEXT("true") : TEXT("false"),
			AxisCache.bUseSwing2 ? TEXT("true") : TEXT("false"));
	}

	UE_LOG(LogTemp, Warning, TEXT("======================================================="));
}
// ---------------------------------------------------------------------------
// Step：施加动作
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::ApplyAction(const TArray<float>& Action)
{
	if (!RobotActor)
	{
		return;
	}

	if (JointActionAxes.Num() != DriveConstraints.Num() || JointAxisCaches.Num() != DriveConstraints.Num())
	{
		CacheJointActionAxes();
	}

	const int32 ExpectedActionDim = GetActionDim();
	if (Action.Num() != ExpectedActionDim)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("ApplyAction received mismatched action length. Action.Num()=%d ExpectedActionDim=%d"),
			Action.Num(),
			ExpectedActionDim);
	}

	TArray<float> CurrentAction;
	CurrentAction.Init(0.0f, ExpectedActionDim);

	int32 ActionIndex = 0;
	for (int32 JointIndex = 0; JointIndex < DriveConstraints.Num(); ++JointIndex)
	{
		UPhysicsConstraintComponent* Constraint = DriveConstraints[JointIndex];
		if (!Constraint || !JointAxisCaches.IsValidIndex(JointIndex))
		{
			continue;
		}

		const FOrangeRobotConstraintAxisCache& AxisCache = JointAxisCaches[JointIndex];
		FVector TargetVel = FVector::ZeroVector;

		auto ConsumeAction = [&](double& OutTargetVel, float AxisVelocityLimit)
		{
			if (!CurrentAction.IsValidIndex(ActionIndex))
			{
				++ActionIndex;
				return;
			}

			float Value = Action.IsValidIndex(ActionIndex) ? FMath::Clamp(Action[ActionIndex], -1.0f, 1.0f) : 0.0f;
			if (FMath::Abs(Value) < ActionDeadzone)
			{
				Value = 0.0f;
			}
			else
			{
				Value = ShapeNormalizedAction(Value, ActionResponseExponent);
			}

			CurrentAction[ActionIndex] = Value;
			const float TargetAxisVel = Value * JointVelocityScale;
			OutTargetVel = SanitizeFiniteScalar(TargetAxisVel, -AxisVelocityLimit, AxisVelocityLimit);
			++ActionIndex;
		};

		if (AxisCache.bUseTwist)
		{
			ConsumeAction(TargetVel.X, TwistVelocityLimit);
		}

		if (AxisCache.bUseSwing1)
		{
			ConsumeAction(TargetVel.Y, SwingVelocityLimit);
		}

		if (AxisCache.bUseSwing2)
		{
			ConsumeAction(TargetVel.Z, SwingVelocityLimit);
		}

		Constraint->SetAngularVelocityTarget(ClampAngularVelocityTarget(TargetVel));
	}

	PreviousAction = LastAction;
	LastAction = MoveTemp(CurrentAction);
	CurrentStep++;
}

// ---------------------------------------------------------------------------
// Step：收集观测
// ---------------------------------------------------------------------------

TArray<float> UOrangeRobotEnvComponent::CollectLowLevelObservations() const
{
	TArray<float> Obs;
	Obs.Reserve(GetLowLevelObservationDim());

	if (!RobotActor) return Obs;

	// 1. 根 Actor 位置（世界空间，cm）
	const FVector Location = SanitizeFiniteVector(RobotActor->GetActorLocation(), -100000.0f, 100000.0f);
	Obs.Add(Location.X);
	Obs.Add(Location.Y);
	Obs.Add(Location.Z);

	// 2. 根 Actor 旋转（度）
	const FRotator Rotation = RobotActor->GetActorRotation();
	Obs.Add(SanitizeFiniteScalar(Rotation.Pitch, -180.0f, 180.0f));
	Obs.Add(SanitizeFiniteScalar(Rotation.Roll, -180.0f, 180.0f));
	Obs.Add(SanitizeFiniteScalar(Rotation.Yaw, -180.0f, 180.0f));

	// 3. 根 Actor 线速度（cm/s）
	const FVector LinVel = SanitizeFiniteVector(RobotActor->GetVelocity(), -5000.0f, 5000.0f);
	Obs.Add(LinVel.X);
	Obs.Add(LinVel.Y);
	Obs.Add(LinVel.Z);

	// 4. 根 Actor 角速度（rad/s，通过躯干 StaticMesh 读取）
	//    若躯干不在 BodyLinks 中，则填 0 保持维度一致
	FVector RootAngVel = FVector::ZeroVector;
	if (BodyLinks.Num() > 0 && BodyLinks[0])
	{
		RootAngVel = SanitizeFiniteVector(BodyLinks[0]->GetPhysicsAngularVelocityInRadians(), -100.0f, 100.0f);
	}
	Obs.Add(RootAngVel.X);
	Obs.Add(RootAngVel.Y);
	Obs.Add(RootAngVel.Z);

	// 5. 每个驱动关节：Twist / Swing1 / Swing2 的角度与角速度
	const int32 NumJoints = DriveConstraints.Num();
	for (int32 i = 0; i < NumJoints; ++i)
	{
		float TwistAngle = 0.0f;
		float Swing1Angle = 0.0f;
		float Swing2Angle = 0.0f;
		if (DriveConstraints[i])
		{
			TwistAngle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentTwist(), -180.0f, 180.0f);
			Swing1Angle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentSwing1(), -180.0f, 180.0f);
			Swing2Angle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentSwing2(), -180.0f, 180.0f);
		}
		Obs.Add(TwistAngle);
		Obs.Add(Swing1Angle);
		Obs.Add(Swing2Angle);

		FVector AngularVelocity = FVector::ZeroVector;
		if (BodyLinks.IsValidIndex(i) && BodyLinks[i])
		{
			AngularVelocity = SanitizeFiniteVector(BodyLinks[i]->GetPhysicsAngularVelocityInRadians(), -100.0f, 100.0f);
		}
		Obs.Add(AngularVelocity.X);
		Obs.Add(AngularVelocity.Y);
		Obs.Add(AngularVelocity.Z);
	}

	return Obs;
}

TArray<float> UOrangeRobotEnvComponent::CollectObservations() const
{
	TArray<float> Obs = CollectLowLevelObservations();
	Obs.Reserve(GetObservationDim());

	if (bAppendNavigationObservation)
	{
		AppendNavigationObservations(Obs);
	}

	return Obs;
}

// ---------------------------------------------------------------------------
// Step：计算奖励
// ---------------------------------------------------------------------------

float UOrangeRobotEnvComponent::ComputeReward() const
{
	if (!RobotActor) return 0.0f;

	float Reward = 0.0f;

	const float UprightDot = GetUprightDot();
	const bool bHasStableSupport = HasStableFootSupport();
	const float BodyHeight = GetBodyHeight();
	const float BodyHeightFactor = BodyHeightRewardMax > KINDA_SMALL_NUMBER
		? FMath::Clamp(BodyHeight / BodyHeightRewardMax, 0.0f, 1.0f)
		: 0.0f;
	const float SupportFactor = bHasStableSupport ? 1.0f : 0.0f;
	const float UprightFactor = FMath::Max(0.0f, UprightDot) * SupportFactor * BodyHeightFactor;

	const FVector Velocity = RobotActor->GetVelocity();
	const float ForwardVel = FMath::Clamp(Velocity.X, -MaxForwardRewardSpeed, MaxForwardRewardSpeed);
	Reward += ForwardVel * ForwardRewardScale;
	Reward += AliveReward;
	Reward += UprightFactor * UprightRewardScale;
	Reward -= FMath::Abs(Velocity.Y) * LateralVelocityPenaltyScale;

	const bool bFallen = CheckFallen();
	if (bFallen)
	{
		Reward -= FallPenalty;
	}

	if (LastAction.Num() == GetActionDim() && PreviousAction.Num() == LastAction.Num())
	{
		float SmoothPenalty = 0.0f;
		for (int32 Index = 0; Index < LastAction.Num(); ++Index)
		{
			const float Delta = LastAction[Index] - PreviousAction[Index];
			SmoothPenalty += Delta * Delta;
		}
		Reward -= SmoothPenalty * ActionSmoothPenaltyScale;
	}

	const bool bTruncated = CurrentStep >= MaxSteps;
	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("Step End Debug | Reward=%f | UprightDot=%f | UprightFactor=%f | HasStableSupport=%s | BodyHeight=%f | ForwardVel=%f | LateralVel=%f | Terminated=%s | Truncated=%s | CurrentStep=%d | MaxSteps=%d"),
		Reward,
		UprightDot,
		UprightFactor,
		bHasStableSupport ? TEXT("true") : TEXT("false"),
		BodyHeight,
		Velocity.X,
		Velocity.Y,
		bFallen ? TEXT("true") : TEXT("false"),
		bTruncated ? TEXT("true") : TEXT("false"),
		CurrentStep,
		MaxSteps);

	return Reward;
}

// ---------------------------------------------------------------------------
// Step：终止条件
// ---------------------------------------------------------------------------

bool UOrangeRobotEnvComponent::CheckFallen() const
{
	if (HeadComponent)
	{
		const double HeadHeight = HeadComponent->GetComponentLocation().Z;
		const bool bHeadHitGround = HeadHeight < HeadGroundHeightThreshold;
		if (bHeadHitGround)
		{
			UE_LOG(
				LogTemp,
				Verbose,
				TEXT("CheckFallen (Head): HeadHeight=%f, Threshold=%f, bFallen=true"),
				HeadHeight,
				HeadGroundHeightThreshold);
			return true;
		}
	}

	const float BodyHeight = GetBodyHeight();
	const bool bHasStableSupport = HasStableFootSupport();
	const float UprightDot = GetUprightDot();
	const float UprightDotThreshold = FMath::Cos(FMath::DegreesToRadians(FallTiltThreshold));
	const bool bBodyCollapsed = BodyHeight < BodyHeightThreshold;
	const bool bTiltedWithoutSupport = UprightDot < UprightDotThreshold && !bHasStableSupport;
	const bool bFallen = bBodyCollapsed || bTiltedWithoutSupport;
	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("CheckFallen: BodyHeight=%f, BodyThreshold=%f, UprightDot=%f, UprightThreshold=%f, HasStableSupport=%s, bBodyCollapsed=%s, bTiltedWithoutSupport=%s, bFallen=%s"),
		BodyHeight,
		BodyHeightThreshold,
		UprightDot,
		UprightDotThreshold,
		bHasStableSupport ? TEXT("true") : TEXT("false"),
		bBodyCollapsed ? TEXT("true") : TEXT("false"),
		bTiltedWithoutSupport ? TEXT("true") : TEXT("false"),
		bFallen ? TEXT("true") : TEXT("false"));
	return bFallen;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::ResetEnv()
{
    UE_LOG(LogTemp, Warning, TEXT("======= ResetEnv CALLED! ======="));
    CurrentStep = 0;
    LastAction.Empty();
    PreviousAction.Empty();

    if (!RobotActor)
    {
        UE_LOG(LogTemp, Error, TEXT("ResetEnv aborted: RobotActor is nullptr"));
        return;
    }

    if (JointActionAxes.Num() != DriveConstraints.Num() || JointAxisCaches.Num() != DriveConstraints.Num())
    {
        CacheJointActionAxes();
    }

    RobotActor->SetActorTransform(InitialRobotTransform, false, nullptr, ETeleportType::ResetPhysics);

    for (int32 i = 0; i < BodyLinks.Num(); ++i)
    {
        UStaticMeshComponent* Link = BodyLinks[i];
        if (Link && InitialBodyLinkTransforms.IsValidIndex(i))
        {
            Link->SetWorldTransform(InitialBodyLinkTransforms[i], false, nullptr, ETeleportType::ResetPhysics);
        }
    }

    for (UStaticMeshComponent* Link : BodyLinks)
    {
        if (Link && Link->IsSimulatingPhysics())
        {
            Link->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Link->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            Link->WakeAllRigidBodies();
        }
    }

    for (UPhysicsConstraintComponent* Constraint : DriveConstraints)
    {
        if (Constraint)
        {
            Constraint->SetAngularVelocityTarget(FVector::ZeroVector);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("ResetEnv completed."));
}

