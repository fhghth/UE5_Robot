// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Components/StaticMeshComponent.h"
#include "OrangeRobotEnvComponent.generated.h"

/**
 * UOrangeRobotEnvComponent
 *
 * 作为蓝图（BP_OrangeRobotEnv）实现 ISingleAgentScholaEnvironment 接口的底层工具组件。
 * 蓝图负责实现 Schola 接口（InitializeEnvironment / Reset / Step），
 * 本组件负责封装物理关节的读写、观测收集、奖励计算和环境重置逻辑。
 *
 * 机器人有效驱动关节（锁定约束 B-R0、B-R4 已排除，共 13 个）：
 *   腿部（左/右各 3 个）：髋关节(R4-R5)、膝关节(R5-R6)、踝关节(R6-R7)
 *   臂部（左/右各 2 个）：肩关节(R0-R1)、腕关节(R2-R3)
 *   头部（1 个）         ：颈关节(H-B)
 *
 * 蓝图配置步骤：
 *   1. DriveConstraints 按顺序填入 13 个物理约束组件
 *      推荐顺序：右髋、右膝、右踝、左髋、左膝、左踝、右肩、右腕、左肩、左腕、颈
 *   2. BodyLinks 按与 DriveConstraints 相同顺序填入子连杆的 StaticMeshComponent
 *   3. RobotActor 指向机器人根 Actor
 *   4. FootR / FootL 分别指向右脚、左脚的 StaticMeshComponent
 *   5. 在蓝图 BeginPlay 或 InitializeEnvironment 中调用 CaptureInitialTransform()
 */
UCLASS(Blueprintable, BlueprintType, meta = (BlueprintSpawnableComponent))
class ORANGEROBOT_API UOrangeRobotEnvComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UOrangeRobotEnvComponent();

    // -----------------------------------------------------------------------
    // 蓝图可配置属性
    // -----------------------------------------------------------------------

    /** 机器人根 Actor，用于读取整体位置 / 速度 / 姿态 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    AActor* RobotActor = nullptr;

    /**
     * 有效驱动关节的物理约束组件（共 13 个，锁定约束不加入）
     * 推荐顺序：右髋、右膝、右踝、左髋、左膝、左踝、右肩、右腕、左肩、左腕、颈
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<UPhysicsConstraintComponent*> DriveConstraints;

    /**
     * 与 DriveConstraints 一一对应的子连杆 StaticMeshComponent
     * 用于读取各关节实际角速度（GetPhysicsAngularVelocityInRadians）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<UStaticMeshComponent*> BodyLinks;

    /** 右脚 StaticMeshComponent（用于步态奖励） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    UStaticMeshComponent* FootR = nullptr;

    /** 左脚 StaticMeshComponent（用于步态奖励） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    UStaticMeshComponent* FootL = nullptr;

    /**
     * 机器人初始 Transform（Reset 时恢复用）
     * 调用 CaptureInitialTransform() 后自动填充，也可在蓝图中手动覆盖
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    FTransform InitialRobotTransform;

    // -----------------------------------------------------------------------
    // 训练超参数（可在蓝图细节面板中调整）
    // -----------------------------------------------------------------------

    /** 最大剧集步数，超过后 bTruncated = true（由蓝图 Step 事件判断） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    int32 MaxSteps = 2000;

    /** 向前行走奖励系数（沿世界 X 轴正方向速度） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ForwardRewardScale = 0.15f;

    /** 每步存活奖励（鼓励保持站立） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float AliveReward = 0.02f;

    /** 躯干倾斜超过此角度（度）视为摔倒并终止剧集 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FallTiltThreshold = 45.0f;

    /** 摔倒惩罚值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FallPenalty = 10.0f;

    /** 动作平滑惩罚系数（抑制关节抖振，对相邻帧动作差值施加惩罚） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionSmoothPenaltyScale = 0.005f;

    // -----------------------------------------------------------------------
    // 状态读取（只读，供蓝图监控）
    // -----------------------------------------------------------------------

    /** 当前剧集已执行步数，ResetEnv() 时自动清零 */
    UPROPERTY(BlueprintReadOnly, Category = "Robot|Training")
    int32 CurrentStep = 0;

    // -----------------------------------------------------------------------
    // Schola 空间维度查询（供蓝图 InitializeEnvironment 构建 FBoxSpace）
    // -----------------------------------------------------------------------

    /**
     * 返回观测向量维度
     * 构成：根位置(3) + 根旋转(3) + 根线速度(3) + 根角速度(3)
     *       + 每驱动关节 [Twist角度(1) + 角速度(1)] * NumDriveJoints
     * 即：12 + DriveConstraints.Num() * 2
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetObservationDim() const;

    /**
     * 返回动作向量维度 = DriveConstraints.Num()
     * 每元素为归一化角速度目标 [-1, 1]，ApplyAction 内部缩放到物理单位
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetActionDim() const;

    // -----------------------------------------------------------------------
    // Schola Step 核心逻辑（供蓝图 Step 事件依次调用）
    // -----------------------------------------------------------------------

    /**
     * 将归一化动作 [-1, 1] 应用到对应关节的角速度目标
     * @param Action 长度必须等于 GetActionDim()
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    void ApplyAction(const TArray<float>& Action);

    /**
     * 收集当前帧观测向量，长度等于 GetObservationDim()
     * 将返回值封装为 FBoxPoint 后填入 OutAgentState.Observations
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    TArray<float> CollectObservations() const;

    /**
     * 计算当前步奖励
     * 将返回值填入 OutAgentState.Reward
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    float ComputeReward() const;

    /**
     * 判断是否摔倒（终止条件 bTerminated）
     * 躯干 Pitch 或 Roll 超过 FallTiltThreshold 即返回 true
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Step")
    bool CheckFallen() const;

    // -----------------------------------------------------------------------
    // Schola Reset 逻辑（供蓝图 Reset 事件调用）
    // -----------------------------------------------------------------------

    /**
     * 将机器人恢复到 InitialRobotTransform，清零所有连杆线速度和角速度，
     * 清零所有关节角速度目标，并将 CurrentStep 重置为 0
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void ResetEnv();

    /**
     * 记录当前 RobotActor 的 Transform 为 InitialRobotTransform
     * 建议在蓝图 InitializeEnvironment 实现的最后调用一次
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void CaptureInitialTransform();

private:

    /** 上一帧动作缓存，用于动作平滑惩罚计算 */
    TArray<float> LastAction;
};
