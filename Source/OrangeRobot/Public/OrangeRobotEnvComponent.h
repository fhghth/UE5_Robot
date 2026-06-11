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

USTRUCT(BlueprintType)
struct FOrangeRobotRewardComponents
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float Alive = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float Height = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float LateralPenalty = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float SupportStability = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float GaitQuality = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float CommandTracking = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float ActionSmooth = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float EnergyPenalty = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float StepAlternation = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float FootImpactPenalty = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float SymmetryPenalty = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float StepFrequencyReward = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float CostOfTransportPenalty = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float FallTerminal = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float Total = 0.0f;
    
    //双足支撑稳定奖励
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float StableDoubleSupport = 0.0f;
    
    //躯干稳定惩罚
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float TrunkStabilityPenalty = 0.0f;

    //膝关节伸展惩罚
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float KneeExtensionPenalty = 0.0f;

    //倾倒预警
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    float TiltWarning = 0.0f;
};

UENUM(BlueprintType, meta=(Bitflags))
enum class ERewardGroupCore : uint8
{
    None            = 0 UMETA(Hidden),
    Alive           = 1 << 0,
    Height          = 1 << 1,
    LateralPenalty  = 1 << 2,
    TrunkStability  = 1 << 3,
    CommandTracking = 1 << 4,
    StableDoubleSupport = 1 << 5,   
};

UENUM(BlueprintType, meta=(Bitflags))
enum class ERewardGroupGait : uint8
{
    None             = 0 UMETA(Hidden),
    SupportStability = 1 << 0,
    GaitQuality      = 1 << 1,
    StepAlternation  = 1 << 2,
    StepFrequency    = 1 << 3,
    FootImpact       = 1 << 4,
    Symmetry         = 1 << 5,
};

UENUM(BlueprintType, meta=(Bitflags))
enum class ERewardGroupReg : uint8
{
    None            = 0 UMETA(Hidden),
    Energy          = 1 << 0,
    ActionSmooth    = 1 << 1,
    CostOfTransport = 1 << 2,
    FallTerminal    = 1 << 3,
};

template <typename TEnum>
FORCEINLINE bool HasFlag(int32 Mask, TEnum Flag)
{
    return (Mask & static_cast<int32>(Flag)) != 0;
}

#define APPLY_REWARD_CORE(Flag, CodeBlock) \
    do { if (HasFlag(RewardMaskCore, Flag)) { CodeBlock } } while (0)

#define APPLY_REWARD_GAIT(Flag, CodeBlock) \
    do { if (HasFlag(RewardMaskGait, Flag)) { CodeBlock } } while (0)

#define APPLY_REWARD_REG(Flag, CodeBlock) \
    do { if (HasFlag(RewardMaskReg, Flag)) { CodeBlock } } while (0)

/** 自定义机器人轴枚举（支持正负方向） */
UENUM(BlueprintType)
enum class ERobotAxisDirection : uint8
{
    PlusX  UMETA(DisplayName = "+X"),
    PlusY  UMETA(DisplayName = "+Y"),
    PlusZ  UMETA(DisplayName = "+Z"),
    MinusX UMETA(DisplayName = "-X"),
    MinusY UMETA(DisplayName = "-Y"),
    MinusZ UMETA(DisplayName = "-Z")
};

UCLASS(Blueprintable, BlueprintType, meta = (BlueprintSpawnableComponent))
class ORANGEROBOT_API UOrangeRobotEnvComponent : public UActorComponent, public IAgent
{
    GENERATED_BODY()

public:
    UOrangeRobotEnvComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ========================================================================
    // 机器人组件
    // ========================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    USceneComponent* HeadComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    USceneComponent* TiltCheckComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    UStaticMeshComponent* FootR = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    UStaticMeshComponent* FootL = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<UPhysicsConstraintComponent*> DriveConstraints;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<UStaticMeshComponent*> BodyLinks;

    // ========================================================================
    // 机器人本体轴配置（因导入模型可能前方为Y，右侧为-X等）
    // ========================================================================
    /** 机器人局部坐标系中，哪个方向代表“前方” */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup|Axes")
    ERobotAxisDirection RobotForwardAxis = ERobotAxisDirection::PlusY;

    /** 机器人局部坐标系中，哪个方向代表“右侧” */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup|Axes")
    ERobotAxisDirection RobotRightAxis = ERobotAxisDirection::MinusX;

    // ========================================================================
    // 初始状态
    // ========================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Setup")
    TArray<FTransform> InitialBodyLinkTransforms;

private:
    float InitialLeftFootDistance = 0.0f;
    float InitialRightFootDistance = 0.0f;
    bool bEnvConfigLoadedFromCommandLine = false;

public:
    // ========================================================================
    // 高层命令
    // ========================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    bool bEnableHighLevelCommand = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Hierarchical")
    FVector2D HighLevelCommand = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float MaxForwardSpeed = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float MaxTurnSpeedDegPerSec = 90.0f;

    UFUNCTION(BlueprintCallable, Category = "Schola|Hierarchical")
    void SetHighLevelCommand(FVector2D InHighLevelCommand);

    UFUNCTION(BlueprintCallable, Category = "Schola|Hierarchical")
    void ClearHighLevelCommand();

    // ========================================================================
    // 内建课程学习 (C++ 侧)
    // ========================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Curriculum")
    bool bEnableCurriculum = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Curriculum")
    int32 GlobalTrainingStep = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Curriculum")
    int32 CurrentCurriculumStage = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Curriculum")
    TArray<int32> CurriculumStageBoundaries;

    // ========================================================================
    // 观测配置
    // ========================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkHeightNormalization = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float DesiredStepPeriod = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SimulationFrequencyHz = 30.0f;

    // ========================================================================
    // 奖励配置
    // ========================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    bool bEnableCommandReward = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float CommandRewardScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float CommandMatchBaseReward = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float ForwardCommandRewardWeight = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float TurnCommandRewardWeight = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float ForwardCommandMatchSigmaMin = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Hierarchical")
    float TurnCommandMatchSigmaMin = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float AliveReward = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TiltQualityGate = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float StableDoubleSupportRewardScale = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float LateralVelocityPenaltyScale = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float DualFootShufflePenaltyScale = 0.0012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DynamicBalanceRewardWeight = 1.0f;

    //只在运输成本惩罚（Cost of Transport）中发挥作用,
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ForwardSpeedRewardMax = 150.0f;
    
    /** 站立指令时速度跟踪的标准差（cm/s 和 deg/s），值越小对漂移越敏感 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float StandCommandSigma = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SwingFootMinHeight = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SwingFootHeightPenaltyScale = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SingleSupportBonusReward = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkSupportOffsetPenaltyScale = 0.003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkSupportOffsetNormalizeDistance = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionMagnitudePenaltyScale = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float EnergySigma = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float StepAlternationRewardScale = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SameLegDominancePenaltyScale = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float HeightRewardScale = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float HeightDropPenaltyScale = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float KneeExtensionPenaltyScale = 0.001f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TiltWarningScale = 1.0f;

    /*躯干稳定参数*/
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkTiltPenaltyScale = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkAngVelXYPenaltyScale = 0.0005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkVerticalVelocityPenaltyScale = 0.0001f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TrunkVerticalVelocityDeadzone = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootImpactPenaltyScale = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootImpactVelocityThreshold = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float GaitSymmetryPenaltyScale = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    TArray<int32> LeftLegJointIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    TArray<int32> RightLegJointIndices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float StepFrequencyRewardScale = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float MinStepFrequencyHz = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float MaxStepFrequencyHz = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float CostOfTransportScale = 0.003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Reward",
              meta = (Bitmask, BitmaskEnum = "/Script/OrangeRobot.ERewardGroupCore"))
    int32 RewardMaskCore = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Reward",
              meta = (Bitmask, BitmaskEnum = "/Script/OrangeRobot.ERewardGroupGait"))
    int32 RewardMaskGait = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Reward",
              meta = (Bitmask, BitmaskEnum = "/Script/OrangeRobot.ERewardGroupReg"))
    int32 RewardMaskReg = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Reward")
    bool bLogRewardBreakdown = true;

    // ========================================================================
    // 动作 / 驱动配置
    // ========================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionDeadzone = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionResponseExponent = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float JointVelocityScale = 90.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float TwistVelocityLimit = 45.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float SwingVelocityLimit = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float ActionSmoothPenaltyScale = 0.008f;

    // ========================================================================
    // 训练超参数
    // ========================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    int32 MaxSteps = 2000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FallTiltThreshold = 45.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float HeadGroundHeightThreshold = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    int32 FallPenaltyHorizon = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float RewardClampMin = -2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float RewardClampMax = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float BodyHeightThreshold = 45.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float BodyHeightRewardMax = 90.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootSupportTraceDistance = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    float FootStableSpeedThreshold = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot|Training")
    bool bApplyRandomJointOffsetsOnReset = false;

    // ========================================================================
    // 动态参数设置
    // ========================================================================
    UFUNCTION(BlueprintCallable, Category = "Schola|Hierarchical")
    void SetCommandRewardScale(float Scale);

    UFUNCTION(BlueprintCallable, Category = "Robot|Training")
    void SetDynamicBalanceRewardWeight(float Weight);

    UFUNCTION(BlueprintCallable, Category = "Robot|Training")
    void SetGaitSymmetryPenaltyScale(float Scale);

    UFUNCTION(BlueprintCallable, Category = "Robot|Training")
    void SetStepFrequencyRewardScale(float Scale);

    UFUNCTION(BlueprintCallable, Category = "Robot|Training")
    void SetCostOfTransportScale(float Scale);

    // ========================================================================
    // 状态缓存 & 调试
    // ========================================================================
    UPROPERTY(BlueprintReadOnly, Category = "Robot|Training")
    int32 CurrentStep = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Debug")
    TArray<int32> JointActionAxes;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Debug")
    TArray<FOrangeRobotConstraintAxisCache> JointAxisCaches;
    
    //一键导出参数
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Robot|Debug")
    void ExportAllConfigToJSON();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Robot|Reward")
    FOrangeRobotRewardComponents LastRewardComponents;

private:
    TArray<float> LastAction;
    TArray<float> PreviousAction;
    EAgentStatus AgentStatus = EAgentStatus::Running;

    enum class ESupportSide : uint8 { None, Left, Right };
    ESupportSide LastSingleSupportSide = ESupportSide::None;
    int32 SameLegConsecutiveSteps = 0;
    float GaitPhase = 0.0f;

    bool bLTouchPrev = false;
    bool bRTouchPrev = false;
    int32 LastStepTransitionStep = -1;

public:
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetLowLevelObservationDim() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetObservationDim() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
    int32 GetActionDim() const;

    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void CaptureInitialTransform();

    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    TArray<float> CollectObservations() const;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Robot|LowLevel")
    TArray<float> CollectLowLevelObservations() const;

    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    void ApplyAction(const TArray<float>& Action);

    UFUNCTION(BlueprintCallable, Category = "Schola|Step")
    float ComputeReward();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Step")
    bool CheckFallen() const;

    UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
    void ResetEnv();

    UFUNCTION(BlueprintCallable, Category = "Robot|Debug")
    void LogDriveConstraintStates() const;

    virtual EAgentStatus GetStatus_Implementation() override;
    virtual void SetStatus_Implementation(EAgentStatus NewStatus) override;
    virtual void Define_Implementation(FInteractionDefinition& OutInteractionDefinition) override;
    virtual void Act_Implementation(const FInstancedStruct& InAction) override;
    virtual void Observe_Implementation(FInstancedStruct& OutObservations) override;

private:
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

    bool IsComponentTouchingGround(const UPrimitiveComponent* Component) const;
    bool IsFootTouchingGround(const UPrimitiveComponent* FootComponent) const;
    bool IsFootStableSupport(const UPrimitiveComponent* FootComponent) const;
    bool HasStableFootSupport() const;

    float GetFootHorizontalSpeed(const UPrimitiveComponent* FootComponent) const;
    float GetFootDistanceFromTrunk(const UPrimitiveComponent* FootComponent) const;

    FVector GetSupportCenter(bool bLeftStable, bool bRightStable) const;
    float GetTrunkSupportOffsetNormalized(bool bLeftStable, bool bRightStable) const;

    void SampleEpisodeHighLevelCommand();
    void UpdateCurriculumWeightsAndCommand();

    float GetJointAngle(int32 JointIndex, int32 AxisSlot) const;

    // --- 自定义轴辅助函数 ---
    /** 根据枚举将四元数转换为对应的方向向量 */
    FVector GetAxisVector(const FQuat& Rotation, ERobotAxisDirection Axis) const;
    /** 从组件获取机器人前方的单位向量（世界方向） */
    FVector GetRobotForwardVector(const UPrimitiveComponent* Comp) const;
    /** 从组件获取机器人右方的单位向量（世界方向） */
    FVector GetRobotRightVector(const UPrimitiveComponent* Comp) const;
    /** 计算局部平面速度：X=前进，Y=右侧，Z=0 */
    FVector GetLocalRobotPlanarVelocity(const UPrimitiveComponent* Component, const FVector& WorldVelocity) const;

#if WITH_EDITOR
    void DrawHighLevelCommandDebug() const;
    /** 可视化绘制机器人本体坐标轴（编辑器Tick中调用） */
    void DrawRobotCoordinateAxes() const;
#endif
};