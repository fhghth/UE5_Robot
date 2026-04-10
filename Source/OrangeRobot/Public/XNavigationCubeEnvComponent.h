// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "XNavigationCubeEnvComponent.generated.h"

UCLASS(Blueprintable, BlueprintType, meta = (BlueprintSpawnableComponent))
class ORANGEROBOT_API UXNavigationCubeEnvComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UXNavigationCubeEnvComponent();

protected:
	virtual void BeginPlay() override;

public:
	/** 参与导航训练的立方体组件，可直接引用 BP_TargetCube 中的 Start */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Setup")
	USceneComponent* CubeComponent = nullptr;

	/** 导航目标组件，可直接引用 BP_TargetCube 中的 End */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Setup")
	USceneComponent* TargetComponent = nullptr;

	/** 立方体初始 Transform，Reset 时恢复 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Setup")
	FTransform InitialCubeTransform;

	/** 每步移动距离缩放，Action=1 时沿对应方向移动的世界距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	float MoveStepScale = 20.0f;

	/** 判定抵达目标的距离阈值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	float ReachTargetDistance = 50.0f;

	/** 距离观测归一化上限 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	float MaxObserveDistance = 2000.0f;

	/** 最大步数，供蓝图判断 Truncated */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Training")
	int32 MaxSteps = 500;

	/** 当前回合已执行步数 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	int32 CurrentStep = 0;

	/** 上一步到目标的距离，用于计算距离缩短奖励 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation|Training")
	float PreviousDistance = 0.0f;

	/** 观测维度：目标相对自身局部方向(3) + 距离(1) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
	int32 GetObservationDim() const;

	/** 动作维度：前后、左右共 2 维 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Space")
	int32 GetActionDim() const;

	/** 记录当前 CubeComponent 的世界 Transform 作为 reset 初始位置 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
	void CaptureInitialTransform();

	/** 恢复到初始位置并清零步数 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Reset")
	void ResetEnv();

	/**
	 * 收集观测：目标相对立方体的局部方向(3) + 世界距离(1)
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Step")
	TArray<float> CollectObservations() const;

	/**	
	 * 应用二维动作：Action[0]=Forward，Action[1]=Right
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Step")
	void ApplyAction(const TArray<float>& Action);

	/** 距离缩短奖励：靠近目标给正奖励，抵达目标给额外奖励 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Step")
	float ComputeReward();

	/** 是否抵达目标 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Schola|Step")
	bool CheckReachedTarget() const;
};
