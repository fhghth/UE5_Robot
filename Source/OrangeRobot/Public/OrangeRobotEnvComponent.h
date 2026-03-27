// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OrangeRobotEnvComponent.generated.h"

UCLASS(Blueprintable, BlueprintType, meta = (BlueprintSpawnableComponent))
class ORANGEROBOT_API UOrangeRobotEnvComponent : public UActorComponent
{
	GENERATED_BODY()


public:

	UOrangeRobotEnvComponent();
	~UOrangeRobotEnvComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot")
	AActor* RobotActor;

    //获取组件信息
    UFUNCTION(BlueprintCallable, Category = "Robot")
    void SetConstraintsAndLinks(
        const TArray<UPhysicsConstraintComponent*>& InConstraints,
        const TArray<UStaticMeshComponent*>& InLinks
    );

    // 应用动作（由训练器的 Step 调用）
    UFUNCTION(BlueprintCallable, Category = "Schola")
    void ApplyAction(const TArray<float>& Action);

    // 收集观测值（由训练器的 CollectObservations 调用）
    UFUNCTION(BlueprintCallable, Category = "Schola")
    TArray<float> CollectObservations();

    // 计算奖励（由训练器的 ComputeReward 调用）
    UFUNCTION(BlueprintCallable, Category = "Schola")
    float ComputeReward();

    // 重置环境（由训练器的 Reset 调用）
    UFUNCTION(BlueprintCallable, Category = "Schola")
    void ResetEnv();


private:
    TArray<UPhysicsConstraintComponent*> CachedConstraints;
    TArray<UStaticMeshComponent*> CachedLinks;

};


