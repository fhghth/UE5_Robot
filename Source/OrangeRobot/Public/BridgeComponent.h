#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Math/Vector2D.h"

#include "Agent/AgentInterface.h"
#include "Policies/NNEPolicy.h"
#include "Steppers/SimpleStepper.h"	
#include "Steppers/PipelinedStepper.h"
//#include "NNEModelData.h"

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
class ORANGEROBOT_API ABridgeComponent : public AActor,public IAgent
{
	GENERATED_BODY()

public:
	ABridgeComponent();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 在蓝图中手动绑定机器人 Actor（例如 ABP_OrangeRobot 实例） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Robot")
	AActor* RobotActor = nullptr;

	/** 导航目标点，可在蓝图中直接指定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	AActor* TargetActor = nullptr;

	/** 高层导航 ONNX 模型资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ONNX")
	UNNEModelData* HighLevelModelData = nullptr;

	/** 低层步态 ONNX 模型资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ONNX")
	UNNEModelData* LowLevelModelData = nullptr;
	
	//// 暴露给蓝图的ONNX模型资产
	//UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schola|Models")
	//UObject* HighLevelModelData;

	//UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Schola|Models")
	//UObject* LowLevelModelData;
	

	/** 高层推理间隔（秒），默认 0.2 秒 = 5Hz */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	float HighInferenceInterval = 0.2f;

	/** 命令平滑系数（0~1），越高响应越快，越低越平滑 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoothing")
	float CommandSmoothAlpha = 0.3f;

	/** 在运行时更新导航目标 */
	UFUNCTION(BlueprintCallable, Category = "Bridge|Navigation")
	void SetTargetActor(AActor* NewTargetActor);

private:
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

	void InitPolicies();
	void InitSteppers();
	void StepHighLevelInference();
	void StepLowLevelInference();
	void ApplySmoothCommand(const FVector2D& NewCommand);
};