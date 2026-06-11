#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Vector2D.h"
#include "NNEModelData.h"

#include "Agent/AgentInterface.h"
#include "Policies/NNEPolicy.h"
#include "Steppers/SimpleStepper.h"
#include "Steppers/PipelinedStepper.h"

#include "BridgeComponent.generated.h"

class AActor;
class UOrangeRobotEnvComponent;
class UXNavigationCubeEnvComponent;

/**
* 机器人策略桥接 Pawn：
* - 聚合导航/低层观测
* - 加载 ONNX 模型（高层 + 低层）
* - 实现分层推理调度（高层低频，低层高频）
* - 命令平滑与执行
*/
UCLASS(Blueprintable)
class ORANGEROBOT_API ABridgeComponent : public AActor, public IAgent
{
	GENERATED_BODY()

public:
	ABridgeComponent();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot")
	AActor* RobotActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	AActor* TargetActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ONNX")
	UNNEModelData* HighLevelModelData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ONNX")
	UNNEModelData* LowLevelModelData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	float HighInferenceInterval = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing")
	float CommandSmoothAlpha = 0.3f;

	UFUNCTION(BlueprintCallable, Category = "Bridge|Navigation")
	void SetTargetActor(AActor* NewTargetActor);

private:
	UPROPERTY(Transient)
	UNNEModelData* RuntimeHighLevelModelData = nullptr;

	UPROPERTY(Transient)
	UNNEModelData* RuntimeLowLevelModelData = nullptr;

	UPROPERTY(Transient)
	UOrangeRobotEnvComponent* EnvComp = nullptr;

	UPROPERTY(Transient)
	UXNavigationCubeEnvComponent* NavigationComp = nullptr;

	UPROPERTY(Transient)
	UNNEPolicy* HighPolicy = nullptr;

	UPROPERTY(Transient)
	UNNEPolicy* LowPolicy = nullptr;

	UPROPERTY(Transient)
	USimpleStepper* NavigationStepper = nullptr;

	UPROPERTY(Transient)
	UPipelinedStepper* ControlStepper = nullptr;

	UPROPERTY(Transient)
	TScriptInterface<IAgent> NavigationAgent;

	UPROPERTY(Transient)
	TScriptInterface<IAgent> ControlAgent;

	float TimeSinceLastHigh = 0.0f;
	FVector2D SmoothedCommand = FVector2D::ZeroVector;

	void ApplyDeployConfigIfPresent();
	UNNEModelData* LoadModelDataFromDisk(const FString& ModelPath, UNNEModelData*& RuntimeModelStorage, const TCHAR* LogLabel);
	void InitPolicies();
	void InitSteppers();
	void StepHighLevelInference();
	void StepLowLevelInference();
	void ApplySmoothCommand(const FVector2D& NewCommand);
};
