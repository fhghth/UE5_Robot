// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Agent/AgentInterface.h"
#include "XNavigationCubeEnvComponent.generated.h"

UCLASS(Blueprintable, BlueprintType, meta = (BlueprintSpawnableComponent))
class ORANGEROBOT_API UXNavigationCubeEnvComponent : public UActorComponent, public IAgent
{
	GENERATED_BODY()

public:
	UXNavigationCubeEnvComponent();

	static constexpr int32 NumRays = 8;

protected:
	virtual void BeginPlay() override;

	TArray<FVector> RayDirections;
	FCollisionShape GetPerceptionShape() const;
	float SweepClearance(const FVector& Start, const FVector& WorldDirection, float TraceDistance) const;
	float GetDirectionalClearance(const FVector& WorldDirection, float TraceDistance) const;
	float GetTargetDirectionClearance() const;
	float GetClearanceAtAngleOffset(float AngleOffsetDegrees) const;
	float ComputeRayOpeningReward(const TArray<float>& CurrentRays, const FVector& MoveDirectionLocal);

public:
	/** 参与导航训练的立方体组件，可直接引用 BP_TargetCube 中的 Start */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Setup")
	UPrimitiveComponent* CubeComponent = nullptr;

	/** 导航目标组件，可直接引用 BP_TargetCube 中的 End */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Setup")
	USceneComponent* TargetComponent = nullptr;

	/** 立方体初始 Transform，Reset 时恢复 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Setup")
	FTransform InitialCubeTransform;

	/** 每步前进距离缩放，Action[0]=1 时沿当前前向移动的世界距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	float MoveStepScale = 20.0f;

	/** 每步转向角缩放，Action[1]=1 时绕 Z 轴旋转的角度（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	float TurnStepDegrees = 15.0f;

	/** 判定抵达目标的距离阈值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	float ReachTargetDistance = 50.0f;

	/** 距离观测归一化上限 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	float MaxObserveDistance = 2000.0f;

	/** 感知用半宽，用于体积 Sweep，为立方体留出安全余量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Perception", meta = (ClampMin = "0.0"))
	float PerceptionHalfExtent = 30.0f;

	/** 是否使用盒体扫描；关闭后退化为球体扫描 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Perception")
	bool bUseBoxSweep = true;

	/** 目标方向两侧辅助观测射线的偏转角度，帮助学习绕过拐角 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float TargetSideClearanceAngleDegrees = 30.0f;

	/** 是否绘制射线调试可视化 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Debug")
	bool bDebugDrawRays = false;

	/** 射线调试线显示时长，0 表示仅显示一帧 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Debug", meta = (ClampMin = "0.0"))
	float DebugRayDuration = 0.0f;

	/** 最大步数，供蓝图判断 Truncated */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	int32 MaxSteps = 500;

	/** 距离缩短奖励缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float DistanceRewardScale = 0.12f;

	/** 每执行一步的时间成本，抑制无意义徘徊 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float StepPenalty = 0.02f;

	/** 发生碰撞时的惩罚 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float CollisionPenalty = 1.5f;

	/** 单步位移小于该阈值时视为停滞 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0"))
	float StuckDistanceThreshold = 1.0f;

	/** 停滞惩罚，避免贴墙卡住 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float StuckPenalty = 0.2f;

	/** 连续停滞时每步额外增加的惩罚系数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0"))
	float StuckPenaltyRamp = 0.05f;

	/** 连续停滞达到该阈值后可判定为截断，避免无效回合 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training", meta = (ClampMin = "1"))
	int32 MaxConsecutiveStuckSteps = 25;

	/** 单步距离改善低于该阈值时视为目标距离停滞 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0"))
	float DistanceStuckThreshold = 2.0f;

	/** 连续多少步目标距离停滞后开始施加惩罚 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "1"))
	int32 MaxDistanceStuckSteps = 12;

	/** 目标距离停滞时的基础惩罚 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float DistanceStuckPenalty = 0.15f;

	/** 射线距离增量超过该阈值时视为方向突然打开 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0"))
	float RayOpeningThreshold = 0.18f;

	/** 某方向突然打开时给予的探索奖励 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float RaySuddenOpeningReward = 0.06f;

	/** 目标方向通路变开阔时的奖励缩放，鼓励绕障 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float ClearanceRewardScale = 0.2f;

	/** 当前朝向与目标方向的静态对齐奖励，驱动先转向再前进 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float FacingRewardScale = 0.5f;

	/** 当前步朝向改善奖励，直接奖励有效转向 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float FacingImprovementRewardScale = 1.0f;

	/** 朝向距离门控指数，SAC 下用平滑二次门控避免门限抖动 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "1.0"))
	float HeadingGateExponent = 2.0f;

	/** 绕障机会改善奖励缩放，鼓励发现目标附近的侧向通路 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float BypassRewardScale = 0.35f;

	/** 绕障机会 EMA 平滑系数，降低射线高频波动造成的奖励噪声 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BypassOpportunityEmaAlpha = 0.35f;

	/** 当前动作方向安全余量改善奖励，鼓励先把朝向转到更安全的方向 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float TurnSafetyRewardScale = 0.6f;

	/** 前冲时安全余量低于该阈值会触发盲目前冲惩罚 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnsafeActionSafetyThreshold = 0.3f;

	/** 在朝向差或安全余量过低时仍强行前冲的惩罚系数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float BadForwardPenaltyScale = 0.35f;

	/** 将原地有效转向视为准备动作时的步罚保留比例 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TurningStepPenaltyScale = 0.2f;

	/** 仅当转角超过该阈值时，才认为本步进行了有效原地转向 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward", meta = (ClampMin = "0.0"))
	float EffectiveTurnThresholdDegrees = 5.0f;

	/** 当前实际移动方向与目标方向越对齐奖励越高，背向目标时转为惩罚 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float TargetAlignmentRewardScale = 0.08f;

	/** 到达目标的终点奖励 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Reward")
	float ReachTargetReward = 10.0f;

	/** 当前回合已执行步数 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	int32 CurrentStep = 0;

	/** 上一步到目标的距离，用于计算距离缩短奖励 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousDistance = 0.0f;

	/** 上一步位置，用于判定是否贴墙停滞 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	FVector PreviousCubeLocation = FVector::ZeroVector;

	/** 上一步目标方向上的射线通路余量，用于鼓励绕过障碍 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousTargetDirectionClearance = 0.0f;

	/** 上一步平滑后的绕障机会，用于奖励发现更好的侧向通路 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousBypassOpportunity = 0.0f;

	/** 上一步朝向与目标方向的对齐度 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousFacingAlignment = 0.0f;

	/** 连续停滞步数，用于更强地惩罚贴墙不动 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	int32 ConsecutiveStuckSteps = 0;

	/** 连续多少步没有明显接近目标 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	int32 ConsecutiveDistanceStuckSteps = 0;

	/** 上一步 8 向射线结果，用于检测某方向是否突然打开 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	TArray<float> PreviousRayResults;

	/** 上一步动作在局部平面上的移动方向，用于将探索奖励限制为动作对齐方向 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	FVector PreviousMoveDirectionLocal = FVector::ZeroVector;

	/** 上一步前进动作值，用于奖励时识别盲目前冲 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousForwardAction = 0.0f;

	/** 上一步转向动作值，用于奖励时识别有效原地转向 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousTurnAction = 0.0f;

	/** 当前步是否发生碰撞 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	bool bHasCollided = false;

	/** 当前动作方向的一步前瞻安全余量 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousActionSafety = 1.0f;

	/** 导航模型最新输出的原始高层命令，X=Forward, Y=Turn */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	FVector2D RawHighLevelCommand = FVector2D::ZeroVector;

	/** 观测维度：目标相对自身局部方向(3) + 距离(1) + 射线(8) + 目标对齐开阔度(3) + 动作前瞻安全余量(1) + 当前前向与目标方向点积(1) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
	int32 GetObservationDim() const;

	/** 动作维度：前进、转向共 2 维 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
	int32 GetActionDim() const;

	/** 记录当前 CubeComponent 的世界 Transform 作为 reset 初始位置 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
	void CaptureInitialTransform();

	/** 恢复到初始位置并清零步数 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
	void ResetEnv();

	/** 执行 8 向射线检测并返回归一化距离 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Sensor")
	TArray<float> PerformRaycasts() const;

	/**
	 * 收集观测：目标相对立方体的局部方向(3) + 世界距离(1)
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Step")
	TArray<float> CollectObservations() const;

	/**	
	 * 应用二维动作：Action[0]=Forward，Action[1]=Turn
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Step")
	void ApplyAction(const TArray<float>& Action);

	/** 距离缩短奖励：靠近目标给正奖励，抵达目标给额外奖励 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Step")
	float ComputeReward();

	/** 是否因连续停滞而建议提前截断当前回合 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Step")
	bool CheckShouldTruncateForStuck() const;

	/** 是否抵达目标 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Step")
	bool CheckReachedTarget() const;

	// IAgent
	virtual EAgentStatus GetStatus_Implementation() override;
	virtual void SetStatus_Implementation(EAgentStatus NewStatus) override;
	virtual void Define_Implementation(FInteractionDefinition& OutInteractionDefinition) override;
	virtual void Act_Implementation(const FInstancedStruct& InAction) override;
	virtual void Observe_Implementation(FInstancedStruct& OutObservations) override;

private:
	EAgentStatus AgentStatus = EAgentStatus::Running;
};
