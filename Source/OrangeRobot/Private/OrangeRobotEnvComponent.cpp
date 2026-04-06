// Fill out your copyright notice in the Description page of Project Settings.

#include "OrangeRobotEnvComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"

// 关节角速度目标缩放系数（将归一化动作 [-1,1] 映射到物理单位 °/s）
// 可根据实际调试结果在蓝图中替换为 UPROPERTY，此处作为内部常量
static constexpr float JointVelocityScale = 180.0f;

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

int32 UOrangeRobotEnvComponent::GetObservationDim() const
{
	// 根状态：位置(3) + 旋转(3) + 线速度(3) + 角速度(3) = 12
	// 每个驱动关节：Twist / Swing1 / Swing2 各自的角度与角速度，共 6
	return 12 + DriveConstraints.Num() * 6;
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
	UE_LOG(LogTemp, Warning, TEXT("ApplyAction received, Action.Num() = %d, ExpectedActionDim = %d"), Action.Num(), GetActionDim());
	for (int32 i = 0; i < Action.Num(); ++i)
	{
		UE_LOG(LogTemp, Warning, TEXT("Action[%d] = %f"), i, Action[i]);
	}

	if (!RobotActor) return;

	if (JointActionAxes.Num() != DriveConstraints.Num() || JointAxisCaches.Num() != DriveConstraints.Num())
	{
		CacheJointActionAxes();
	}

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

		if (AxisCache.bUseTwist)
		{
			if (Action.IsValidIndex(ActionIndex))
			{
				TargetVel.X = FMath::Clamp(Action[ActionIndex], -1.0f, 1.0f) * JointVelocityScale;
			}
			++ActionIndex;
		}

		if (AxisCache.bUseSwing1)
		{
			if (Action.IsValidIndex(ActionIndex))
			{
				TargetVel.Y = FMath::Clamp(Action[ActionIndex], -1.0f, 1.0f) * JointVelocityScale;
			}
			++ActionIndex;
		}

		if (AxisCache.bUseSwing2)
		{
			if (Action.IsValidIndex(ActionIndex))
			{
				TargetVel.Z = FMath::Clamp(Action[ActionIndex], -1.0f, 1.0f) * JointVelocityScale;
			}
			++ActionIndex;
		}

		Constraint->SetAngularVelocityTarget(TargetVel);

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("ApplyAction -> Joint[%d] %s | TargetVel=(%f, %f, %f) | EnabledAxes[X=%s,Y=%s,Z=%s]"),
			JointIndex,
			*Constraint->GetName(),
			TargetVel.X,
			TargetVel.Y,
			TargetVel.Z,
			AxisCache.bUseTwist ? TEXT("true") : TEXT("false"),
			AxisCache.bUseSwing1 ? TEXT("true") : TEXT("false"),
			AxisCache.bUseSwing2 ? TEXT("true") : TEXT("false"));
	}

	LastAction = Action;
	CurrentStep++;
}

// ---------------------------------------------------------------------------
// Step：收集观测
// ---------------------------------------------------------------------------

TArray<float> UOrangeRobotEnvComponent::CollectObservations() const
{
	TArray<float> Obs;
	Obs.Reserve(GetObservationDim());

	if (!RobotActor) return Obs;

	// 1. 根 Actor 位置（世界空间，cm）
	const FVector Location = RobotActor->GetActorLocation();
	Obs.Add(Location.X);
	Obs.Add(Location.Y);
	Obs.Add(Location.Z);

	// 2. 根 Actor 旋转（度）
	const FRotator Rotation = RobotActor->GetActorRotation();
	Obs.Add(Rotation.Pitch);
	Obs.Add(Rotation.Roll);
	Obs.Add(Rotation.Yaw);

	// 3. 根 Actor 线速度（cm/s）
	const FVector LinVel = RobotActor->GetVelocity();
	Obs.Add(LinVel.X);
	Obs.Add(LinVel.Y);
	Obs.Add(LinVel.Z);

	// 4. 根 Actor 角速度（rad/s，通过躯干 StaticMesh 读取）
	//    若躯干不在 BodyLinks 中，则填 0 保持维度一致
	FVector RootAngVel = FVector::ZeroVector;
	if (BodyLinks.Num() > 0 && BodyLinks[0])
	{
		RootAngVel = BodyLinks[0]->GetPhysicsAngularVelocityInRadians();
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
			TwistAngle = DriveConstraints[i]->GetCurrentTwist();
			Swing1Angle = DriveConstraints[i]->GetCurrentSwing1();
			Swing2Angle = DriveConstraints[i]->GetCurrentSwing2();
		}
		Obs.Add(TwistAngle);
		Obs.Add(Swing1Angle);
		Obs.Add(Swing2Angle);

		FVector AngularVelocity = FVector::ZeroVector;
		if (BodyLinks.IsValidIndex(i) && BodyLinks[i])
		{
			AngularVelocity = BodyLinks[i]->GetPhysicsAngularVelocityInRadians();
		}
		Obs.Add(AngularVelocity.X);
		Obs.Add(AngularVelocity.Y);
		Obs.Add(AngularVelocity.Z);
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

	// 1. 向前行走奖励（世界 X 轴正方向线速度）
	const float ForwardVel = RobotActor->GetVelocity().X;
	Reward += ForwardVel * ForwardRewardScale;

	// 2. 存活奖励
	Reward += AliveReward;

	// 3. 摔倒惩罚（若已摔倒则扣分，终止由 CheckFallen 负责）
	if (CheckFallen())
	{
		Reward -= FallPenalty;
	}

	// 4. 动作平滑惩罚（抑制抖振）
	if (LastAction.Num() == GetActionDim())
	{
		float SmoothPenalty = 0.0f;
		for (const float A : LastAction)
		{
			SmoothPenalty += A * A;
		}
		Reward -= SmoothPenalty * ActionSmoothPenaltyScale;
	}

	return Reward;
}

// ---------------------------------------------------------------------------
// Step：终止条件
// ---------------------------------------------------------------------------

bool UOrangeRobotEnvComponent::CheckFallen() const
{
	if (!RobotActor) return false;

	const FRotator Rot = RobotActor->GetActorRotation();
	const float Tilt = FMath::Abs(Rot.Pitch) + FMath::Abs(Rot.Roll);
	return Tilt > FallTiltThreshold;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::ResetEnv()
{
	CurrentStep = 0;
	LastAction.Empty();

	CacheJointActionAxes();
	LogDriveConstraintStates();

	if (!RobotActor) return;

	// 1. 恢复根 Actor 的位置与旋转
	RobotActor->SetActorTransform(InitialRobotTransform, false, nullptr, ETeleportType::ResetPhysics);

	// 2. 清零所有关节角速度目标
	for (UPhysicsConstraintComponent* Constraint : DriveConstraints)
	{
		if (Constraint)
		{
			Constraint->SetAngularVelocityTarget(FVector::ZeroVector);
		}
	}

	// 3. 清零所有连杆的线速度与角速度
	for (UStaticMeshComponent* Link : BodyLinks)
	{
		if (Link && Link->IsSimulatingPhysics())
		{
			Link->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Link->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		}
	}
}
