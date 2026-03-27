// Fill out your copyright notice in the Description page of Project Settings.


#include "OrangeRobotEnvComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h" 
#include "Components/StaticMeshComponent.h"          

UOrangeRobotEnvComponent::UOrangeRobotEnvComponent()
{
}

UOrangeRobotEnvComponent::~UOrangeRobotEnvComponent()
{
}

//设置物理约束和网格组件
void UOrangeRobotEnvComponent::SetConstraintsAndLinks(
    const TArray<UPhysicsConstraintComponent*>& InConstraints,
    const TArray<UStaticMeshComponent*>& InLinks)
{
    CachedConstraints = InConstraints;
    CachedLinks = InLinks;

    //// 缓存初始变换（可选）
    //InitialLinkTransforms.Empty();
    //for (UStaticMeshComponent* Link : CachedLinks)
    //{
    //    if (Link)
    //        InitialLinkTransforms.Add(Link->GetComponentTransform());
    //}
}


//应用动作
void UOrangeRobotEnvComponent::ApplyAction(const TArray<float>& Action)
{
    if (!RobotActor) return;

    // 确保动作数量与关节数量匹配
    int32 NumJoints = CachedConstraints.Num();
    for (int32 i = 0; i < FMath::Min(Action.Num(), NumJoints); ++i)
    {
        UPhysicsConstraintComponent* Constraint = CachedConstraints[i];
        if (Constraint)
        {
            // 示例：将动作值作为角速度目标（弧度/秒）
            // 具体驱动模式需参考你在机器人蓝图中设置的 Angular Drive Mode
            Constraint->SetAngularVelocityTarget(FVector(Action[i], 0.0f, 0.0f));
        }
    }
}


//收集观测数据
TArray<float> UOrangeRobotEnvComponent::CollectObservations()
{
    TArray<float> Obs;

    if (!RobotActor) return Obs;

    // 1. 机器人身体位置 (X, Y, Z)
    FVector Location = RobotActor->GetActorLocation();
    Obs.Add(Location.X);
    Obs.Add(Location.Y);
    Obs.Add(Location.Z);

    // 2. 机器人身体旋转 (Pitch, Roll, Yaw)
    FRotator Rotation = RobotActor->GetActorRotation();
    Obs.Add(Rotation.Pitch);
    Obs.Add(Rotation.Roll);
    Obs.Add(Rotation.Yaw);

    // 3. 每个关节的当前角度（需根据约束类型获取）
    for (UPhysicsConstraintComponent* Constraint : CachedConstraints)
    {
        if (Constraint)
        {
            // 示例：获取 Twist 角度（需根据约束轴调整）
            float Angle = Constraint->GetCurrentTwist();
            Obs.Add(Angle);
        }
    }

    // 4. 每个关节的角速度
    for (UPhysicsConstraintComponent* Constraint : CachedConstraints)
    {
        if (Constraint)
        {
            //FVector AngularVel = Constraint->GetAngularVelocityTarget(); // 实际需获取当前角速度
            //Obs.Add(AngularVel.X); // 根据你的控制轴调整
        }
    }

    return Obs;
}


//计算奖励
float UOrangeRobotEnvComponent::ComputeReward()
{
    float Reward = 0.0f;

    if (!RobotActor) return Reward;

    // 向前速度奖励（假设 X 轴正向）
    FVector Velocity = RobotActor->GetVelocity();
    Reward += Velocity.X * 0.1f;

    // 存活奖励（每步固定小奖励）
    Reward += 0.01f;

    // 摔倒惩罚：如果身体倾斜过大
    FRotator Rotation = RobotActor->GetActorRotation();
    float Tilt = FMath::Abs(Rotation.Pitch) + FMath::Abs(Rotation.Roll);
    if (Tilt > 45.0f)
    {
        Reward -= 10.0f;
    }

    return Reward;
}


//重置状态
void UOrangeRobotEnvComponent::ResetEnv()
{
    if (!RobotActor) return;

    // 1. 重置机器人位置和旋转（需机器人蓝图提供接口，或直接设置）
    RobotActor->SetActorLocation(FVector(0.0f, 0.0f, 100.0f));
    RobotActor->SetActorRotation(FRotator::ZeroRotator);

    // 2. 重置所有物理约束的状态（如速度、目标）
    for (UPhysicsConstraintComponent* Constraint : CachedConstraints)
    {
        if (Constraint)
        {
            Constraint->SetAngularVelocityTarget(FVector::ZeroVector);
            // 可能还需要重置约束驱动目标
        }
    }

    // 3. 重置所有关节网格体的物理状态（可选，如果启用了物理）
    for (UStaticMeshComponent* Link : CachedLinks)
    {
        if (Link && Link->IsSimulatingPhysics())
        {
            Link->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Link->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        }
    }
}