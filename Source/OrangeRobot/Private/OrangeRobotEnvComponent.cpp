// Fill out your copyright notice in the Description page of Project Settings.

#include "OrangeRobotEnvComponent.h"

// 关节角速度目标缩放系数（将归一化动作 [-1,1] 映射到物理单位 °/s）
// 可根据实际调试结果在蓝图中替换为 UPROPERTY，此处作为内部常量
static constexpr float JointVelocityScale = 180.0f;

UOrangeRobotEnvComponent::UOrangeRobotEnvComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ---------------------------------------------------------------------------
// 空间维度查询
// ---------------------------------------------------------------------------

int32 UOrangeRobotEnvComponent::GetObservationDim() const
{
	// 根状态：位置(3) + 旋转(3) + 线速度(3) + 角速度(3) = 12
	// 每个驱动关节：Twist角度(1) + 子连杆角速度沿主轴(1) = 2
	return 12 + DriveConstraints.Num() * 2;
}

int32 UOrangeRobotEnvComponent::GetActionDim() const
{
	return DriveConstraints.Num();
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
}

// ---------------------------------------------------------------------------
// Step：施加动作
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::ApplyAction(const TArray<float>& Action)
{
	//打印动作日志
	UE_LOG(LogTemp, Warning, TEXT("ApplyAction received, Action.Num() = %d"), Action.Num());
	for (int32 i = 0; i < Action.Num(); ++i)
	{
		UE_LOG(LogTemp, Warning, TEXT("Action[%d] = %f"), i, Action[i]);
	}


	if (!RobotActor) return;

	const int32 NumJoints = DriveConstraints.Num();
	for (int32 i = 0; i < FMath::Min(Action.Num(), NumJoints); ++i)
	{
		UPhysicsConstraintComponent* Constraint = DriveConstraints[i];
		if (!Constraint) continue;

		// 将归一化动作缩放为角速度目标（°/s），驱动轴为 X（Twist）
		// 注意：蓝图中需确保对应约束已开启 Angular Velocity Drive（Twist Drive）
		const float VelTarget = FMath::Clamp(Action[i], -1.0f, 1.0f) * JointVelocityScale;
		Constraint->SetAngularVelocityTarget(FVector(VelTarget, 0.0f, 0.0f));
	}

	// 缓存本帧动作用于平滑惩罚
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

	// 5. 每个驱动关节：Twist 角度 + 子连杆角速度主轴分量
	const int32 NumJoints = DriveConstraints.Num();
	for (int32 i = 0; i < NumJoints; ++i)
	{
		// 5a. Twist 角度（度）
		float TwistAngle = 0.0f;
		if (DriveConstraints[i])
		{
			TwistAngle = DriveConstraints[i]->GetCurrentTwist();
		}
		Obs.Add(TwistAngle);

		// 5b. 子连杆角速度主轴（X）分量（rad/s）
		float AngVelX = 0.0f;
		if (BodyLinks.IsValidIndex(i) && BodyLinks[i])
		{
			AngVelX = BodyLinks[i]->GetPhysicsAngularVelocityInRadians().X;
		}
		Obs.Add(AngVelX);
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
	if (LastAction.Num() == DriveConstraints.Num())
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
