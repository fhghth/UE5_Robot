// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/ConstraintInstanceBlueprintLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "OrangeRobotEnvComponent.generated.h"

USTRUCT(BlueprintType)
struct FOrangeRobotConstraintAxisCache
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Robot|Debug")
	bool bUseTwist = false;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Debug")
	bool bUseSwing1 = false;

	UPROPERTY(VisibleAnywhere, Category = "Robot|Debug")
	bool bUseSwing2 = false;

	int32 GetAxisCount() const
	{
		return (bUseTwist ? 1 : 0) + (bUseSwing1 ? 1 : 0) + (bUseSwing2 ? 1 : 0);
	}
};

/**
 * UOrangeRobotEnvComponent
 *
 * 作为蓝图（BP_OrangeRobotEnv）实现 ISingleAgentScholaEnvironment 接口的底层工具组件。
 * 蓝图负责实现 Schola 接口（InitializeEnvironment / Reset / Step），
 * 本组件负责封装物理关节的读写、观测收集、奖励计算和环境重置逻辑。
 *
 * 机器人可驱动约束列表。
 * 当前动作空间不再简单等于约束数量，而是会在运行时根据每个约束的：
 *   - 角驱动模式（TwistAndSwing / SLERP）
 *   - 速度驱动启用状态
 *   - Twist / Swing1 / Swing2 的 Motion 是否被锁定
 * 自动推导每个约束实际可控的动作轴。
 * 推荐顺序仍为：右髋、右膝、右踝、左髋、左膝、左踝、右肩、右腕、左肩、左腕、颈
 *
 * 蓝图配置步骤：
 *   1. DriveConstraints 按顺序填入需要参与训练的物理约束组件
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

    /*机器人静态网格躯干，用于检测摔倒*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    USceneComponent* TiltCheckComponent = nullptr;

    /** 头部组件，用于检测头部是否接近地面；若为空则回退到躯干倾斜检测 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    USceneComponent* HeadComponent = nullptr;
    
    /** 机器人初始 BodyLink Transform（Reset 时恢复用）
     * 调用 CaptureInitialTransform() 后自动填充，也可在蓝图中手动覆盖
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<FTransform> InitialBodyLinkTransforms = TArray<FTransform>() ;

    /**
     * 参与训练的物理约束组件列表。
     * 动作维度会在运行时根据约束驱动模式、速度驱动状态和 Twist/Swing1/Swing2 的 Motion 自动计算。
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

    /** 导航目标 Actor，用于构造 navigation 16 维观测 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    AActor* NavigationTargetActor = nullptr;

    /** 导航观测的参考组件，若为空则回退到 RobotActor 根坐标系 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    USceneComponent* NavigationReferenceComponent = nullptr;

    /** 导航体积感知的 Sweep 半尺寸 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    FVector NavigationPerceptionHalfExtent = FVector(18.0f, 18.0f, 18.0f);

    /** 目标距离归一化使用的最大观测距离 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    float NavigationMaxObserveDistance = 2000.0f;

    /** 侧向开阔度角度，需与 navigation_029 保持一致 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    float TargetSideClearanceAngleDegrees = 30.0f;

    /** 动作前瞻安全余量使用的前瞻距离 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    float NavigationActionLookaheadDistance = 120.0f;

    /** Sweep 检测通道 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    TEnumAsByte<ECollisionChannel> NavigationSweepChannel = ECC_WorldStatic;

    /** 是否在总观测末尾追加 navigation 16 维观测 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Navigation")
    bool bAppendNavigationObservation = false;

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

    /** 躯干倾斜超过此角度（度）视为摔倒并终止剧集；当未配置 HeadComponent 时使用 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FallTiltThreshold = 45.0f;

    /** 头部世界 Z 坐标低于此阈值时视为摔倒 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float HeadGroundHeightThreshold = 30.0f;

    /** 摔倒惩罚值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FallPenalty = 10.0f;

    /** 动作平滑惩罚系数（抑制关节抖振，对相邻帧动作差值施加惩罚） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionSmoothPenaltyScale = 0.005f;

    /** 关节角速度目标缩放系数（将归一化动作 [-1,1] 映射到 °/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float JointVelocityScale = 90.0f;

    /** 动作死区，小幅动作直接视为 0，减轻抖振 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionDeadzone = 0.05f;

    /** 归一化动作的非线性软映射指数；大于 1 时会压低中高幅动作，减轻爆冲 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionResponseExponent = 2.0f;

    /** Twist 角速度目标硬上限（度/秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TwistVelocityLimit = 45.0f;

    /** Swing1 / Swing2 角速度目标硬上限（度/秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SwingVelocityLimit = 35.0f;

    /** 前进奖励使用的速度裁剪上限（cm/s），防止用爆冲刷奖励 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float MaxForwardRewardSpeed = 120.0f;

    /** 站立直立奖励系数，鼓励身体保持竖直 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float UprightRewardScale = 0.2f;

    /** 脚部向下探测距离，用于判断是否接近地面支撑 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootSupportTraceDistance = 12.0f;

    /** 脚部线速度低于此阈值（cm/s）时，可视为稳定支撑脚 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootStableSpeedThreshold = 35.0f;

    /** 身体世界 Z 坐标低于此阈值时，认为机体已塌陷 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float BodyHeightThreshold = 45.0f;

    /** 身体高度奖励归一化上限（大于该高度后按满额计算） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float BodyHeightRewardMax = 90.0f;

    /** 横向漂移惩罚系数，抑制左右乱晃 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float LateralVelocityPenaltyScale = 0.02f;

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
     * 返回低层观测向量维度
     * 构成：根位置(3) + 根旋转(3) + 根线速度(3) + 根角速度(3)
     *      + 每个约束 [Twist角度, Swing1角度, Swing2角度, X角速度, Y角速度, Z角速度]
     * 即：12 + DriveConstraints.Num() * 6
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetLowLevelObservationDim() const;

    /**
     * 返回总观测向量维度
     * = 低层观测维度 + (bAppendNavigationObservation ? 16 : 0)
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetObservationDim() const;

    /**
     * 返回动作向量维度 = 所有约束实际可控轴数量之和
     * 维度来自缓存的 JointActionAxes，按每个约束的 Twist(X) / Swing1(Y) / Swing2(Z) 可控状态累加
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetActionDim() const;

    // -----------------------------------------------------------------------
    // Schola Step 核心逻辑（供蓝图 Step 事件依次调用）
    // -----------------------------------------------------------------------

    /**
     * 将归一化动作 [-1, 1] 应用到每个约束实际可控的角速度目标
     * 动作展开顺序：按 DriveConstraints 顺序遍历，每个约束内部按 Twist(X) -> Swing1(Y) -> Swing2(Z)
     * @param Action 长度必须等于 GetActionDim()
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    void ApplyAction(const TArray<float>& Action);

    /**
     * 收集当前帧总观测向量，长度等于 GetObservationDim()
     * 默认仅包含低层观测；若启用 bAppendNavigationObservation，则在末尾追加 navigation16
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    TArray<float> CollectObservations() const;

    /** 收集当前帧低层观测向量，不包含 navigation16 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Robot|LowLevel")
    TArray<float> CollectLowLevelObservations() const;

    /**
     * 计算当前步奖励
     * 将返回值填入 OutAgentState.Reward
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    float ComputeReward() const;

    /**
     * 判断是否摔倒（终止条件 bTerminated）
     * 优先使用头部高度阈值检测；若未配置 HeadComponent，则回退到躯干倾斜检测
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Step")
    bool CheckFallen() const;

    // -----------------------------------------------------------------------
    // Schola Reset 逻辑（供蓝图 Reset 事件调用）
    // -----------------------------------------------------------------------

    /**
     * 将机器人恢复到 InitialRobotTransform，清零所有连杆线速度和角速度，
     * 清零所有关节角速度目标，并重新缓存各约束的可控动作轴，再将 CurrentStep 重置为 0
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void ResetEnv();

    /**
     * 记录当前 RobotActor 的 Transform 为 InitialRobotTransform，
     * 并同步缓存每个约束的可控动作轴信息。
     * 建议在蓝图 InitializeEnvironment 实现的最后调用一次
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void CaptureInitialTransform();

    /**
     * 打印当前所有 DriveConstraints 的驱动模式、速度驱动状态与角轴约束状态到 UE 日志
     * 便于确认 UPhysicsConstraintComponent 返回信息是否符合预期
     */
    UFUNCTION(BlueprintCallable, Category = "Robot|Debug")
    void LogDriveConstraintStates() const;

    /** 只收集与 navigation_029 对齐的 16 维导航观测 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Robot|Navigation")
    TArray<float> CollectNavigationObservations() const;

    /** 打印当前 navigation16 维观测，便于与立方体环境对比 */
    UFUNCTION(BlueprintCallable, Category = "Robot|Navigation")
    void LogNavigationObservations() const;


    /** 当前每个约束缓存出的可控动作轴数量 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Debug")
    TArray<int32> JointActionAxes;

    /** 当前每个约束缓存出的可控轴详情（Twist / Swing1 / Swing2） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Debug")
    TArray<FOrangeRobotConstraintAxisCache> JointAxisCaches;

private:

    void CacheJointActionAxes();
    FOrangeRobotConstraintAxisCache BuildConstraintAxisCache(UPhysicsConstraintComponent* Constraint) const;
    static float ShapeNormalizedAction(float Value, float Exponent);
    static float SanitizeFiniteScalar(float Value, float MinValue, float MaxValue);
    static FVector SanitizeFiniteVector(const FVector& Value, float MinValue, float MaxValue);
    FVector ClampAngularVelocityTarget(const FVector& TargetVel) const;
    FVector GetNavigationOrigin() const;
    FTransform GetNavigationReferenceTransform() const;
    float GetDirectionalClearance(const FVector& WorldDirection, float TraceDistance) const;
    void AppendNavigationObservations(TArray<float>& Obs) const;
    USceneComponent* GetTiltReferenceComponent() const;
    float GetUprightDot() const;
    float GetBodyHeight() const;
    bool IsFootTouchingGround(const UPrimitiveComponent* FootComponent) const;
    bool IsFootStableSupport(const UPrimitiveComponent* FootComponent) const;
    bool HasStableFootSupport() const;

    /** 上一帧动作缓存，用于动作平滑惩罚计算 */
    TArray<float> LastAction;

    /** 上上一帧动作缓存，用于计算相邻帧动作变化惩罚 */
    TArray<float> PreviousAction;
};
