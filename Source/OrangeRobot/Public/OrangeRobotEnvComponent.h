// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/ConstraintInstanceBlueprintLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Agent/AgentInterface.h"
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

UCLASS(Blueprintable, BlueprintType, meta = (BlueprintSpawnableComponent))
class ORANGEROBOT_API UOrangeRobotEnvComponent : public UActorComponent, public IAgent
{
    GENERATED_BODY()

public:
    UOrangeRobotEnvComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ========================================================================
    // 机器人组件
    // ========================================================================

    /** 头部组件，用于头部触地检测（checkFallen 优先使用） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    USceneComponent* HeadComponent = nullptr;

    /** 机器人静态网格躯干，用于判断身体是否直立及倾斜检测 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    USceneComponent* TiltCheckComponent = nullptr;

    /** 右脚 StaticMeshComponent（用于步态奖励与触地检测） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    UStaticMeshComponent* FootR = nullptr;

    /** 左脚 StaticMeshComponent（用于步态奖励与触地检测） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    UStaticMeshComponent* FootL = nullptr;

    /** 参与训练的物理约束组件列表（按推荐顺序排列） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<UPhysicsConstraintComponent*> DriveConstraints;

    /** 与 DriveConstraints 一一对应的子连杆 StaticMeshComponent（用于读取关节角速度） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<UStaticMeshComponent*> BodyLinks;

    // ========================================================================
    // 初始状态
    // ========================================================================

    /** 各 BodyLink 的初始世界 Transform（Reset 时恢复） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<FTransform> InitialBodyLinkTransforms;

private:
    /** 左脚初始垂直距离（躯干Z - 左脚Z），用于摆动脚高度惩罚与观测归一化 */
    float InitialLeftFootDistance = 0.0f;

    /** 右脚初始垂直距离（躯干Z - 右脚Z） */
    float InitialRightFootDistance = 0.0f;

public:
    // ========================================================================
    // 高层命令
    // ========================================================================

    /** 是否将高层命令 [CmdForward, CmdTurn] 追加到观测末尾 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    bool bEnableHighLevelCommand = false;

    /** 当前高层命令：X = CmdForward, Y = CmdTurn */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Hierarchical")
    FVector2D HighLevelCommand = FVector2D::ZeroVector;

    /** 是否在每个 episode 重置时随机采样高层命令 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    bool bSampleHighLevelCommandOnReset = true;

    /** 高层前进命令映射到的最大局部前向速度（cm/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float MaxForwardSpeed = 200.0f;

    /** 高层转向命令映射到的最大偏航角速度（deg/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float MaxTurnSpeedDegPerSec = 90.0f;

    /** 仅设置高层命令，不触发推理；供蓝图/C++ 调度器调用 */
    UFUNCTION(BlueprintCallable, Category = "Schola|Hierarchical")
    void SetHighLevelCommand(FVector2D InHighLevelCommand);

    /** 清零高层命令，通常在 Reset 时调用 */
    UFUNCTION(BlueprintCallable, Category = "Schola|Hierarchical")
    void ClearHighLevelCommand();

    // ========================================================================
    // 观测配置
    // ========================================================================

    /** 躯干高度观测归一化基准值（cm） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkHeightNormalization = 100.0f;

    // ========================================================================
    // 奖励配置
    // ========================================================================

    /** 是否启用命令匹配奖励（可用于课程控制） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    bool bEnableCommandReward = false;

    /** 命令匹配基础奖励值（每步最大） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float CommandMatchBaseReward = 0.5f;

    /** 前进命令匹配在总命令奖励中的权重 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float ForwardCommandRewardWeight = 0.7f;

    /** 转向命令匹配在总命令奖励中的权重 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float TurnCommandRewardWeight = 0.3f;

    /** 每步存活奖励（鼓励保持站立） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float AliveReward = 0.02f;

    /** 站立直立奖励系数，鼓励身体保持竖直 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float UprightRewardScale = 0.2f;

    /** 横向漂移惩罚系数，抑制左右乱晃 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float LateralVelocityPenaltyScale = 0.02f;

    /** 双脚均不稳定时的惩罚系数，抑制腾空或无支撑状态 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float UnstableSupportPenaltyScale = 0.12f;

    /** 双脚同时高速摆动惩罚系数，抑制双脚高频乱抖 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float DualFootShufflePenaltyScale = 0.0012f;

    /** 第一阶段动态平衡过渡开关：启用后优先鼓励稳定支撑与小步纠偏 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    bool bEnableDynamicBalanceReward = true;

    /** 双脚同时稳定支撑时的奖励值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float DoubleSupportRewardScale = 0.15f;

    /** 无条件前进速度奖励系数（鼓励产生向前的速度） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ForwardVelocityUnconditionalRewardScale = 0.03f;

    /** 前进速度奖励的归一化上限（cm/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ForwardSpeedRewardMax = 150.0f;

    /** 单脚支撑时，摆动脚必须至少抬起的高度（cm） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SwingFootMinHeight = 8.0f;

    /** 摆动脚高度过低的惩罚系数（超出阈值的平方惩罚） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SwingFootHeightPenaltyScale = 0.01f;

    /** 单脚支撑额外奖励（前进中+直立+摆动合规时激活） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SingleSupportBonusReward = 0.1f;

    /** 单脚支撑时，躯干水平投影偏离支撑中心的惩罚系数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkSupportOffsetPenaltyScale = 0.003f;

    /** 躯干水平投影偏移归一化距离（cm） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkSupportOffsetNormalizeDistance = 25.0f;
    
    /** 是否启用站立专项奖励（静态站立时惩罚移动/倾斜，奖励双脚平踩） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    bool bEnableStandReward = false;

    /** 站立时对躯干线速度的惩罚系数（cm/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float StandVelocityPenaltyScale = 0.1f;

    /** 站立时对躯干角速度的惩罚系数（deg/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float StandAngularVelocityPenaltyScale = 0.05f;

    /** 站立时奖励双脚同时触地且稳定的权重 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float StandStableFootReward = 0.2f;

    // ========================================================================
    // 动作 / 驱动配置
    // ========================================================================

    /** 动作死区，小幅动作直接视为 0，减轻抖振 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionDeadzone = 0.05f;

    /** 归一化动作的非线性软映射指数；大于 1 时会压低中高幅动作，减轻爆冲 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionResponseExponent = 2.0f;

    /** 关节角速度目标缩放系数（将归一化动作 [-1,1] 映射到 °/s） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float JointVelocityScale = 90.0f;

    /** Twist 角速度目标硬上限（度/秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TwistVelocityLimit = 45.0f;

    /** Swing1 / Swing2 角速度目标硬上限（度/秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SwingVelocityLimit = 35.0f;

    /** 动作平滑惩罚系数（抑制关节抖振，对相邻帧动作差值施加惩罚） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionSmoothPenaltyScale = 0.005f;

    // ========================================================================
    // 训练超参数
    // ========================================================================

    /** 最大剧集步数，超过后 bTruncated = true */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    int32 MaxSteps = 2000;

    /** 躯干倾斜超过此角度（度）视为摔倒并终止剧集（未配置 HeadComponent 时使用） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FallTiltThreshold = 45.0f;

    /** 头部世界 Z 坐标低于此阈值时视为摔倒 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float HeadGroundHeightThreshold = 30.0f;

    /** 摔倒惩罚值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FallPenalty = 10.0f;

    /** 身体世界 Z 坐标低于此阈值时，认为机体已塌陷 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float BodyHeightThreshold = 45.0f;

    /** 身体高度奖励归一化上限（大于该高度后按满额计算） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float BodyHeightRewardMax = 90.0f;

    /** 脚部向下探测距离，用于判断是否接近地面支撑 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootSupportTraceDistance = 12.0f;

    /** 脚部线速度低于此阈值（cm/s）时，可视为稳定支撑脚 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootStableSpeedThreshold = 35.0f;

    /** Reset 时是否对关节施加小幅随机角速度扰动，增强策略鲁棒性 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    bool bApplyRandomJointOffsetsOnReset = false;

    // ========================================================================
    // 状态缓存 & 调试
    // ========================================================================

    /** 当前剧集已执行步数，ResetEnv() 时自动清零 */
    UPROPERTY(BlueprintReadOnly, Category = "Robot|Training")
    int32 CurrentStep = 0;

    /** 当前每个约束缓存出的可控动作轴数量 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Debug")
    TArray<int32> JointActionAxes;

    /** 当前每个约束缓存出的可控轴详情（Twist / Swing1 / Swing2） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Debug")
    TArray<FOrangeRobotConstraintAxisCache> JointAxisCaches;

private:
    /** 上一帧动作缓存，用于动作平滑惩罚计算 */
    TArray<float> LastAction;

    /** 上上一帧动作缓存，用于计算相邻帧动作变化惩罚 */
    TArray<float> PreviousAction;

    EAgentStatus AgentStatus = EAgentStatus::Running;

public:
    // ========================================================================
    // 空间维度查询
    // ========================================================================

    /** 返回低层观测向量维度 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetLowLevelObservationDim() const;

    /** 返回总观测向量维度（包含高层命令） */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetObservationDim() const;

    /** 返回动作向量维度 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetActionDim() const;

    // ========================================================================
    // 初始状态记录
    // ========================================================================

    /** 记录当前 Transform 为初始状态，并缓存各约束的可控动作轴 */
    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void CaptureInitialTransform();

    // ========================================================================
    // 观测收集
    // ========================================================================

    /** 收集当前帧总观测向量 */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    TArray<float> CollectObservations() const;

    /** 收集当前帧低层观测向量，不包含高层命令 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Robot|LowLevel")
    TArray<float> CollectLowLevelObservations() const;

    // ========================================================================
    // 动作应用
    // ========================================================================

    /** 将归一化动作应用到各约束的角速度目标 */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    void ApplyAction(const TArray<float>& Action);

    // ========================================================================
    // 奖励计算
    // ========================================================================

    /** 计算当前步奖励 */
    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    float ComputeReward();

    // ========================================================================
    // 终止条件
    // ========================================================================

    /** 判断是否摔倒（终止条件） */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Step")
    bool CheckFallen() const;

    // ========================================================================
    // 重置逻辑
    // ========================================================================

    /** 重置环境到初始状态，清零所有速度与目标 */
    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void ResetEnv();

    // ========================================================================
    // 调试与工具
    // ========================================================================

    /** 打印所有驱动约束的状态信息到日志 */
    UFUNCTION(BlueprintCallable, Category = "Robot|Debug")
    void LogDriveConstraintStates() const;

    // ========================================================================
    // IAgent 接口实现
    // ========================================================================

    virtual EAgentStatus GetStatus_Implementation() override;
    virtual void SetStatus_Implementation(EAgentStatus NewStatus) override;
    virtual void Define_Implementation(FInteractionDefinition& OutInteractionDefinition) override;
    virtual void Act_Implementation(const FInstancedStruct& InAction) override;
    virtual void Observe_Implementation(FInstancedStruct& OutObservations) override;

private:
    // ========================================================================
    // 内部辅助函数
    // ========================================================================

    UPrimitiveComponent* GetTrunkPrimitive() const;

    void CacheJointActionAxes();
    FOrangeRobotConstraintAxisCache BuildConstraintAxisCache(UPhysicsConstraintComponent* Constraint) const;

    static float ShapeNormalizedAction(float Value, float Exponent);
    static float SanitizeFiniteScalar(float Value, float MinValue, float MaxValue);
    static FVector SanitizeFiniteVector(const FVector& Value, float MinValue, float MaxValue);
    static float SanitizeFiniteAngleDegrees(float Value);

    FVector ClampAngularVelocityTarget(const FVector& TargetVel) const;

    USceneComponent* GetTiltReferenceComponent() const;
    float GetUprightDot() const;
    float GetBodyHeight() const;

    bool IsFootTouchingGround(const UPrimitiveComponent* FootComponent) const;
    bool IsFootStableSupport(const UPrimitiveComponent* FootComponent) const;
    bool HasStableFootSupport() const;

    float GetFootHorizontalSpeed(const UPrimitiveComponent* FootComponent) const;
    float GetFootDistanceFromTrunk(const UPrimitiveComponent* FootComponent) const;

    FVector GetSupportCenter(bool bLeftStable, bool bRightStable) const;
    float GetTrunkSupportOffsetNormalized(bool bLeftStable, bool bRightStable) const;

    void SampleEpisodeHighLevelCommand();

#if WITH_EDITOR
    void DrawHighLevelCommandDebug() const;
#endif
};