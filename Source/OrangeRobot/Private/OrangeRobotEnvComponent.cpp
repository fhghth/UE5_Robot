// Fill out your copyright notice in the Description page of Project Settings.

#include "OrangeRobotEnvComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"

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

	FVector GetLocalPlanarVelocity(const AActor* Actor, const FVector& WorldVelocity)
	{
		if (!Actor)
		{
			return FVector::ZeroVector;
		}

		const FVector Forward = Actor->GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = Actor->GetActorRightVector().GetSafeNormal2D();
		const FVector PlanarVelocity(WorldVelocity.X, WorldVelocity.Y, 0.0f);
		return FVector(
			FVector::DotProduct(PlanarVelocity, Forward),
			FVector::DotProduct(PlanarVelocity, Right),
			0.0f);
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

float UOrangeRobotEnvComponent::SanitizeFiniteAngleDegrees(float Value)
{
	if (!FMath::IsFinite(Value))
	{
		return 0.0f;
	}

	return FMath::UnwindDegrees(Value);
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
	PrimaryComponentTick.bCanEverTick = true;
}

void UOrangeRobotEnvComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if WITH_EDITOR
	if (bEnableHighLevelCommand)
	{
		DrawHighLevelCommandDebug();
	}
#endif
}

#if WITH_EDITOR
void UOrangeRobotEnvComponent::DrawHighLevelCommandDebug() const
{
	if (!bEnableHighLevelCommand || !RobotActor)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FVector ActorLocation = RobotActor->GetActorLocation();
	const FVector ActorForward = RobotActor->GetActorForwardVector();
	const FVector ActorRight = RobotActor->GetActorRightVector();
	const FVector ActorUp = RobotActor->GetActorUpVector();

	const FVector Forward2D = ActorForward.GetSafeNormal2D();
	const FVector Right2D = ActorRight.GetSafeNormal2D();

	// 1. 前进命令箭头（长度 ∝ |CmdForward|）
	const float ForwardStrength = HighLevelCommand.X;
	if (!FMath::IsNearlyZero(ForwardStrength))
	{
		const FVector ForwardDir = (ForwardStrength > 0.0f) ? Forward2D : -Forward2D;
		const float ArrowLength = FMath::Abs(ForwardStrength) * 50.0f;
		const FVector Lift = ActorUp * 20.0f;
		const FVector LineStart = ActorLocation + Lift;
		const FVector LineEnd = LineStart + ForwardDir * ArrowLength;
		DrawDebugDirectionalArrow(
			World,
			LineStart,
			LineEnd,
			5.0f,
			ForwardStrength > 0.0f ? FColor::Green : FColor::Red,
			false,
			-1.0f,
			0,
			2.0f);
	}

	// 2. 转向命令：水平面圆弧 + 末端方向箭头
	const float TurnStrength = HighLevelCommand.Y;
	if (!FMath::IsNearlyZero(TurnStrength))
	{
		const FVector Center = ActorLocation + ActorUp * 30.0f;
		const float Radius = 40.0f;
		const float SweepDeg = FMath::Abs(TurnStrength) * 180.0f;
		const float StartDeg = TurnStrength > 0.0f ? 0.0f : 180.0f;
		const float EndDeg = TurnStrength > 0.0f ? SweepDeg : (180.0f - SweepDeg);
		const int32 NumSegments = FMath::Clamp((int32)(FMath::Abs(EndDeg - StartDeg) / 8.0f) + 1, 4, 32);

		const FColor TurnColor = TurnStrength > 0.0f ? FColor::Cyan : FColor::Orange;
		FVector PrevPoint = FVector::ZeroVector;
		for (int32 i = 0; i <= NumSegments; ++i)
		{
			const float Alpha = (float)i / (float)NumSegments;
			const float AngleDeg = FMath::Lerp(StartDeg, EndDeg, Alpha);
			const float AngleRad = FMath::DegreesToRadians(AngleDeg);
			const FVector Point = Center + Radius * (Forward2D * FMath::Cos(AngleRad) + Right2D * FMath::Sin(AngleRad));
			if (i > 0)
			{
				DrawDebugLine(World, PrevPoint, Point, TurnColor, false, -1.0f, 0, 1.5f);
			}
			PrevPoint = Point;
		}

		const float ArrowAngleDeg = TurnStrength > 0.0f ? 150.0f : 30.0f;
		const FVector ArrowDir = Forward2D.RotateAngleAxis(ArrowAngleDeg, ActorUp).GetSafeNormal();
		const FVector ArrowEnd = Center + ArrowDir * Radius;
		DrawDebugDirectionalArrow(
			World,
			Center,
			ArrowEnd,
			3.0f,
			TurnColor,
			false,
			-1.0f,
			0,
			1.5f);
	}

	const FString CommandText = FString::Printf(
		TEXT("Cmd: Fwd %.2f | Turn %.2f"),
		HighLevelCommand.X,
		HighLevelCommand.Y);
	DrawDebugString(
		World,
		ActorLocation + ActorUp * 50.0f,
		CommandText,
		nullptr,
		FColor::White,
		0.0f,
		true);
}
#endif


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
	// 躯干高度(1) + 局部线速度(3) + 局部角速度(3) + 重力投影(1) + 足部触地(2) = 10
	// 每个驱动关节：归一化 Twist / Swing1 / Swing2 角度与角速度，共 6
	return 10 + DriveConstraints.Num() * 6;
}

int32 UOrangeRobotEnvComponent::GetObservationDim() const
{
	return GetLowLevelObservationDim() + (bEnableHighLevelCommand ? 2 : 0);
}

void UOrangeRobotEnvComponent::SetHighLevelCommand(FVector2D InHighLevelCommand)
{
	HighLevelCommand.X = SanitizeFiniteScalar(InHighLevelCommand.X, -1.0f, 1.0f);
	HighLevelCommand.Y = SanitizeFiniteScalar(InHighLevelCommand.Y, -1.0f, 1.0f);
}

void UOrangeRobotEnvComponent::ClearHighLevelCommand()
{
	HighLevelCommand = FVector2D::ZeroVector;
}

void UOrangeRobotEnvComponent::SampleEpisodeHighLevelCommand()
{
	if (!bEnableHighLevelCommand)
	{
		ClearHighLevelCommand();
		return;
	}

	if (bSampleHighLevelCommandOnReset)
	{
		SetHighLevelCommand(FVector2D(
			FMath::FRandRange(-1.0f, 1.0f),
			FMath::FRandRange(-1.0f, 1.0f)));
	}
	else
	{
		ClearHighLevelCommand();
	}

	
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

float UOrangeRobotEnvComponent::GetFootHorizontalSpeed(const UPrimitiveComponent* FootComponent) const
{
	if (!FootComponent)
	{
		return 0.0f;
	}

	const FVector FootVelocity = FootComponent->GetComponentVelocity();
	return FVector(FootVelocity.X, FootVelocity.Y, 0.0f).Size();
}

float UOrangeRobotEnvComponent::GetFootDistanceFromTrunk(const UPrimitiveComponent* FootComponent) const
{
	if (!FootComponent || !TiltCheckComponent)
	{
		return 0.0f;
	}

	return TiltCheckComponent->GetComponentLocation().Z - FootComponent->GetComponentLocation().Z;
}

FVector UOrangeRobotEnvComponent::GetSupportCenter(bool bLeftStable, bool bRightStable) const
{
	const FVector LeftLocation = FootL ? FootL->GetComponentLocation() : FVector::ZeroVector;
	const FVector RightLocation = FootR ? FootR->GetComponentLocation() : FVector::ZeroVector;

	if (bLeftStable && bRightStable)
	{
		return (LeftLocation + RightLocation) * 0.5f;
	}

	if (bLeftStable)
	{
		return LeftLocation;
	}

	if (bRightStable)
	{
		return RightLocation;
	}

	if (FootL && FootR)
	{
		return (LeftLocation + RightLocation) * 0.5f;
	}

	return RobotActor ? RobotActor->GetActorLocation() : FVector::ZeroVector;
}

float UOrangeRobotEnvComponent::GetTrunkSupportOffsetNormalized(bool bLeftStable, bool bRightStable) const
{
	const USceneComponent* TrunkComponent = GetTiltReferenceComponent();
	if (!TrunkComponent)
	{
		return 0.0f;
	}

	const FVector TrunkLocation = TrunkComponent->GetComponentLocation();
	const FVector SupportCenter = GetSupportCenter(bLeftStable, bRightStable);
	const FVector HorizontalOffset = FVector(
		TrunkLocation.X - SupportCenter.X,
		TrunkLocation.Y - SupportCenter.Y,
		0.0f);

	const float NormalizeDistance = FMath::Max(TrunkSupportOffsetNormalizeDistance, 1.0f);
	return FMath::Clamp(HorizontalOffset.Size() / NormalizeDistance, 0.0f, 4.0f);
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

	ClearHighLevelCommand();

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

	// 新增：缓存站立时躯干到双脚的初始垂直距离（正值 = 躯干Z - 脚Z）
	if (TiltCheckComponent)
	{
		const float TrunkZ = TiltCheckComponent->GetComponentLocation().Z;
		InitialLeftFootDistance = FootL ? (TrunkZ - FootL->GetComponentLocation().Z) : 0.0f;
		InitialRightFootDistance = FootR ? (TrunkZ - FootR->GetComponentLocation().Z) : 0.0f;
	}
	else
	{
		InitialLeftFootDistance = 0.0f;
		InitialRightFootDistance = 0.0f;
	}

	UE_LOG(LogTemp, Warning, TEXT("CaptureInitialTransform: Initial Foot Distances - Left: %.2f cm, Right: %.2f cm"),
		InitialLeftFootDistance, InitialRightFootDistance);

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
	const int32 NumJoints = DriveConstraints.Num();
	Obs.Reserve(10 + NumJoints * 6);

	if (!RobotActor) return Obs;

	// 1. 躯干高度（归一化）
	const float TrunkHeight = GetBodyHeight();
	Obs.Add(FMath::Clamp(TrunkHeight / FMath::Max(TrunkHeightNormalization, 1.0f), 0.0f, 2.0f));

	// 2. 躯干局部线速度（前向、侧向、垂向）
	const FVector WorldVelocity = SanitizeFiniteVector(RobotActor->GetVelocity(), -5000.0f, 5000.0f);
	const FVector Forward = RobotActor->GetActorForwardVector();
	const FVector Right = RobotActor->GetActorRightVector();
	const FVector Up = RobotActor->GetActorUpVector();
	const float LocalVelX = FVector::DotProduct(WorldVelocity, Forward);
	const float LocalVelY = FVector::DotProduct(WorldVelocity, Right);
	const float LocalVelZ = FVector::DotProduct(WorldVelocity, Up);
	Obs.Add(SanitizeFiniteScalar(LocalVelX / 200.0f, -5.0f, 5.0f));
	Obs.Add(SanitizeFiniteScalar(LocalVelY / 200.0f, -5.0f, 5.0f));
	Obs.Add(SanitizeFiniteScalar(LocalVelZ / 200.0f, -5.0f, 5.0f));

	// 3. 躯干局部角速度（roll / pitch / yaw 方向）
	FVector LocalAngVel = FVector::ZeroVector;
	if (BodyLinks.Num() > 0 && BodyLinks[0])
	{
		const FVector WorldAngVel = SanitizeFiniteVector(BodyLinks[0]->GetPhysicsAngularVelocityInDegrees(), -540.0f, 540.0f);
		LocalAngVel.X = FVector::DotProduct(WorldAngVel, Forward);
		LocalAngVel.Y = FVector::DotProduct(WorldAngVel, Right);
		LocalAngVel.Z = FVector::DotProduct(WorldAngVel, Up);
	}
	Obs.Add(SanitizeFiniteScalar(LocalAngVel.X / 180.0f, -3.0f, 3.0f));
	Obs.Add(SanitizeFiniteScalar(LocalAngVel.Y / 180.0f, -3.0f, 3.0f));
	Obs.Add(SanitizeFiniteScalar(LocalAngVel.Z / 180.0f, -3.0f, 3.0f));

	// 4. 重力方向投影
	const USceneComponent* TiltComp = GetTiltReferenceComponent();
	const FVector TiltUp = TiltComp ? TiltComp->GetUpVector() : FVector::UpVector;
	Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(TiltUp, FVector::UpVector), -1.0f, 1.0f));

	// 5. 足部触地布尔标志
	Obs.Add(IsFootTouchingGround(FootL) ? 1.0f : 0.0f);
	Obs.Add(IsFootTouchingGround(FootR) ? 1.0f : 0.0f);

	// 6. 每个驱动关节：归一化角度与角速度
	for (int32 i = 0; i < NumJoints; ++i)
	{
		float TwistAngle = 0.0f;
		float Swing1Angle = 0.0f;
		float Swing2Angle = 0.0f;
		if (DriveConstraints[i])
		{
			TwistAngle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentTwist(), -180.0f, 180.0f) / 180.0f;
			Swing1Angle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentSwing1(), -180.0f, 180.0f) / 180.0f;
			Swing2Angle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentSwing2(), -180.0f, 180.0f) / 180.0f;
		}
		Obs.Add(TwistAngle);
		Obs.Add(Swing1Angle);
		Obs.Add(Swing2Angle);

		FVector AngularVelocity = FVector::ZeroVector;
		if (BodyLinks.IsValidIndex(i) && BodyLinks[i])
		{
			AngularVelocity = SanitizeFiniteVector(BodyLinks[i]->GetPhysicsAngularVelocityInDegrees(), -360.0f, 360.0f)
				/ FMath::Max(JointVelocityScale, 1.0f);
			AngularVelocity.X = SanitizeFiniteScalar(AngularVelocity.X, -3.0f, 3.0f);
			AngularVelocity.Y = SanitizeFiniteScalar(AngularVelocity.Y, -3.0f, 3.0f);
			AngularVelocity.Z = SanitizeFiniteScalar(AngularVelocity.Z, -3.0f, 3.0f);
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

	if (bEnableHighLevelCommand)
	{
		Obs.Add(SanitizeFiniteScalar(HighLevelCommand.X, -1.0f, 1.0f));
		Obs.Add(SanitizeFiniteScalar(HighLevelCommand.Y, -1.0f, 1.0f));
	}

	return Obs;
}

// ---------------------------------------------------------------------------
// Step：计算奖励
// ---------------------------------------------------------------------------

float UOrangeRobotEnvComponent::ComputeReward()
{
	if (!RobotActor) return 0.0f;

	float Reward = 0.0f;

	const float UprightDot = GetUprightDot();
	const bool bLeftStable = IsFootStableSupport(FootL);
	const bool bRightStable = IsFootStableSupport(FootR);
	const bool bHasStableSupport = bLeftStable || bRightStable;
	const bool bSingleStableSupport = bLeftStable != bRightStable;
	const float BodyHeight = GetBodyHeight();
	const float BodyHeightFactor = BodyHeightRewardMax > KINDA_SMALL_NUMBER
		? FMath::Clamp(BodyHeight / BodyHeightRewardMax, 0.0f, 1.0f)
		: 0.0f;
	const float SupportFactor = bHasStableSupport ? 1.0f : 0.0f;
	const float UprightFactor = FMath::Max(0.0f, UprightDot) * SupportFactor * BodyHeightFactor;

	const FVector Velocity = RobotActor->GetVelocity();
	const FVector LocalPlanarVelocity = GetLocalPlanarVelocity(RobotActor, Velocity);
	Reward += AliveReward;
	Reward += UprightFactor * UprightRewardScale;
	Reward -= FMath::Abs(LocalPlanarVelocity.Y) * LateralVelocityPenaltyScale;

	const float ActualForwardSpeed = LocalPlanarVelocity.X;
	float ActualTurnSpeedDegPerSec = 0.0f;
	if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(RobotActor->GetRootComponent()))
	{
		ActualTurnSpeedDegPerSec = SanitizeFiniteAngleDegrees(RootPrimitive->GetPhysicsAngularVelocityInDegrees().Z);
	}
	const float DesiredForwardSpeed = HighLevelCommand.X * MaxForwardSpeed;
	const float DesiredTurnSpeedDegPerSec = HighLevelCommand.Y * MaxTurnSpeedDegPerSec;
	float CommandReward = 0.0f;
	if (bEnableCommandReward)
	{
		const float ForwardMatch = FMath::Clamp(
			1.0f - FMath::Abs(ActualForwardSpeed - DesiredForwardSpeed) / FMath::Max(MaxForwardSpeed, 1.0f),
			0.0f,
			1.0f);
		const float TurnMatch = FMath::Clamp(
			1.0f - FMath::Abs(ActualTurnSpeedDegPerSec - DesiredTurnSpeedDegPerSec) / FMath::Max(MaxTurnSpeedDegPerSec, 1.0f),
			0.0f,
			1.0f);
		CommandReward =
			(ForwardMatch * ForwardCommandRewardWeight + TurnMatch * TurnCommandRewardWeight)
			* CommandMatchBaseReward
			* (1.0f + 0.5f * UprightFactor);
		Reward += CommandReward;
	}

	if (bEnableDynamicBalanceReward)
	{
		const bool bLeftTouching = IsFootTouchingGround(FootL);
		const bool bRightTouching = IsFootTouchingGround(FootR);
		const float LeftFootHorizontalSpeed = GetFootHorizontalSpeed(FootL);
		const float RightFootHorizontalSpeed = GetFootHorizontalSpeed(FootR);
		const bool bDoubleSupport = bLeftStable && bRightStable;

		if (bDoubleSupport)
		{
			Reward += DoubleSupportRewardScale;
		}

		const float ForwardSpeed = FMath::Abs(LocalPlanarVelocity.X);
		const float NormalizedForwardSpeed = ForwardSpeedRewardMax > KINDA_SMALL_NUMBER
			? FMath::Clamp(ForwardSpeed / ForwardSpeedRewardMax, 0.0f, 1.0f)
			: 0.0f;
		Reward += NormalizedForwardSpeed * ForwardVelocityUnconditionalRewardScale;

		if (bSingleStableSupport)
		{
			// 确定哪只脚是支撑脚，哪只是摆动脚
			const UPrimitiveComponent* SwingFoot = bLeftStable ? FootR : FootL;
			const UPrimitiveComponent* SupportFoot = bLeftStable ? FootL : FootR;

			// 获取初始站立时该摆动脚的躯干-脚距离
			const float InitialDistance = (SwingFoot == FootL) ? InitialLeftFootDistance : InitialRightFootDistance;
			// 当前躯干-脚距离（正值，脚在躯干下方）
			const float CurrentDistance = GetFootDistanceFromTrunk(SwingFoot);

			// 实际抬起高度 = 初始距离 - 当前距离（抬起后距离减小，抬起量为正）
			const float ActualLift = FMath::Max(0.0f, InitialDistance - CurrentDistance);

			// 计算抬升不足的量（要求 SwingFootMinHeight，不足部分为正值）
			const float LiftDeficit = FMath::Max(0.0f, SwingFootMinHeight - ActualLift);

			// 平方惩罚，鼓励梯度优化
			const float HeightPenalty = LiftDeficit * LiftDeficit * SwingFootHeightPenaltyScale;
			Reward -= HeightPenalty;

			// 前进 + 直立 + 摆动脚合规时的额外奖励
			const bool bIsMovingForward = ForwardSpeed > 10.0f;      // 前进速度阈值
			const bool bUprightEnough = UprightDot > 0.95f;          // 约18度以内
			const bool bSwingFootHighEnough = (ActualLift >= SwingFootMinHeight);

			if (bIsMovingForward && bUprightEnough && bSwingFootHighEnough)
			{
				Reward += SingleSupportBonusReward;
			}

			// 保留躯干偏移惩罚，防止重心过度偏离支撑脚
			Reward -= GetTrunkSupportOffsetNormalized(bLeftStable, bRightStable) * TrunkSupportOffsetPenaltyScale;
		}
		else
		{
			const bool bBothFeetUnstable = !bLeftStable && !bRightStable;
			if (bBothFeetUnstable)
			{
				Reward -= UnstableSupportPenaltyScale;
			}

			const bool bBothSliding = bLeftTouching && !bLeftStable && bRightTouching && !bRightStable;
			if (bBothSliding)
			{
				Reward -= (LeftFootHorizontalSpeed + RightFootHorizontalSpeed) * DualFootShufflePenaltyScale;
			}
		}
	}

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
		TEXT("Step End Debug | Reward=%f | UprightDot=%f | UprightFactor=%f | LeftStable=%s | RightStable=%s | BodyHeight=%f | CmdForward=%f | CmdTurn=%f | ActualForward=%f | DesiredForward=%f | ActualTurnDeg=%f | DesiredTurnDeg=%f | CommandReward=%f | LocalLateralVel=%f | DynamicBalance=%s | Terminated=%s | Truncated=%s | CurrentStep=%d | MaxSteps=%d"),
		Reward,
		UprightDot,
		UprightFactor,
		bLeftStable ? TEXT("true") : TEXT("false"),
		bRightStable ? TEXT("true") : TEXT("false"),
		BodyHeight,
		HighLevelCommand.X,
		HighLevelCommand.Y,
		ActualForwardSpeed,
		DesiredForwardSpeed,
		ActualTurnSpeedDegPerSec,
		DesiredTurnSpeedDegPerSec,
		CommandReward,
		LocalPlanarVelocity.Y,
		bEnableDynamicBalanceReward ? TEXT("true") : TEXT("false"),
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
	SampleEpisodeHighLevelCommand();

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

	if (bApplyRandomJointOffsetsOnReset)
	{
		for (UPhysicsConstraintComponent* Constraint : DriveConstraints)
		{
			if (!Constraint)
			{
				continue;
			}

			const FVector RandomAngularVelocityDeg(
				FMath::FRandRange(-5.0f, 5.0f),
				FMath::FRandRange(-3.0f, 3.0f),
				FMath::FRandRange(-3.0f, 3.0f));
			Constraint->SetAngularVelocityTarget(ClampAngularVelocityTarget(RandomAngularVelocityDeg));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ResetEnv completed."));
}

EAgentStatus UOrangeRobotEnvComponent::GetStatus_Implementation()
{
	return AgentStatus;
}

void UOrangeRobotEnvComponent::SetStatus_Implementation(EAgentStatus NewStatus)
{
	AgentStatus = NewStatus;
}

void UOrangeRobotEnvComponent::Define_Implementation(FInteractionDefinition& OutInteractionDefinition)
{
	const int32 ObsDim = GetObservationDim();
	const int32 ActionDim = GetActionDim();

	TArray<float> ObsLow;
	TArray<float> ObsHigh;
	ObsLow.Init(-5.0f, ObsDim);
	ObsHigh.Init(5.0f, ObsDim);

	TArray<float> ActionLow;
	TArray<float> ActionHigh;
	ActionLow.Init(-1.0f, ActionDim);
	ActionHigh.Init(1.0f, ActionDim);

	OutInteractionDefinition = FInteractionDefinition(
		TInstancedStruct<FSpace>::Make<FBoxSpace>(ObsLow, ObsHigh),
		TInstancedStruct<FSpace>::Make<FBoxSpace>(ActionLow, ActionHigh));
}

void UOrangeRobotEnvComponent::Act_Implementation(const FInstancedStruct& InAction)
{
	if (AgentStatus != EAgentStatus::Running)
	{
		return;
	}

	const FBoxPoint* BoxAction = InAction.GetPtr<FBoxPoint>();
	if (!BoxAction)
	{
		return;
	}

	ApplyAction(BoxAction->Values);
}

void UOrangeRobotEnvComponent::Observe_Implementation(FInstancedStruct& OutObservations)
{
	if (AgentStatus != EAgentStatus::Running)
	{
		OutObservations.InitializeAs<FBoxPoint>(TArray<float>());
		return;
	}

	OutObservations.InitializeAs<FBoxPoint>(CollectObservations());
}