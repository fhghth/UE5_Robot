#include "BridgeComponent.h"
#include "OrangeRobotEnvComponent.h"
#include "XNavigationCubeEnvComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Logging/LogMacros.h"
#include "Policies/PolicyInterface.h"

ABridgeComponent::ABridgeComponent()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ABridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!RobotActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[BridgePawn] RobotActor is null!"));
		return;
	}

	// 获取机器人组件
	EnvComp = RobotActor->FindComponentByClass<UOrangeRobotEnvComponent>();
	if (!EnvComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[BridgePawn] No OrangeRobotEnvComponent found on RobotActor."));
		return;
	}

	NavigationComp = RobotActor->FindComponentByClass<UXNavigationCubeEnvComponent>();
	if (!NavigationComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BridgePawn] No XNavigationCubeEnvComponent found on RobotActor."));
	}
	else if (TargetActor)
	{
		NavigationComp->TargetComponent = TargetActor->GetRootComponent();
	}

	// 配置环境组件以支持高层命令拼接
	EnvComp->bEnableHighLevelCommand = true;
	EnvComp->ClearHighLevelCommand();
	SmoothedCommand = FVector2D::ZeroVector;
	TimeSinceLastHigh = HighInferenceInterval;

	NavigationAgent.SetObject(NavigationComp);
	NavigationAgent.SetInterface(Cast<IAgent>(NavigationComp));
	ControlAgent.SetObject(EnvComp);
	ControlAgent.SetInterface(Cast<IAgent>(EnvComp));

	InitPolicies();
	InitSteppers();

	UE_LOG(LogTemp, Log, TEXT("[BridgePawn] Initialization complete."));
}

void ABridgeComponent::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!EnvComp || !ControlStepper)
	{
		return;
	}

	TimeSinceLastHigh += DeltaTime;
	if (NavigationStepper && TimeSinceLastHigh >= HighInferenceInterval)
	{
		TimeSinceLastHigh = 0.0f;
		StepHighLevelInference();
	}

	StepLowLevelInference();
}

void ABridgeComponent::InitPolicies()
{
	if (HighLevelModelData && NavigationComp)
	{
		HighPolicy = NewObject<UNNEPolicy>(this);
		if (HighPolicy)
		{
			HighPolicy->ModelData = HighLevelModelData;
			HighPolicy->RuntimeName = TEXT("NNERuntimeORTCpu");

			FInteractionDefinition NavDef;
			IAgent::Execute_Define(NavigationComp, NavDef);
			if (!HighPolicy->Init(NavDef))
			{
				UE_LOG(LogTemp, Error, TEXT("[BridgePawn] High-level policy init failed."));
				HighPolicy = nullptr;
			}
		}
	}

	if (LowLevelModelData && EnvComp)
	{
		LowPolicy = NewObject<UNNEPolicy>(this);
		if (LowPolicy)
		{
			LowPolicy->ModelData = LowLevelModelData;
			LowPolicy->RuntimeName = TEXT("NNERuntimeORTCpu");

			FInteractionDefinition ControlDef;
			IAgent::Execute_Define(EnvComp, ControlDef);
			if (!LowPolicy->Init(ControlDef))
			{
				UE_LOG(LogTemp, Error, TEXT("[BridgePawn] Low-level policy init failed."));
				LowPolicy = nullptr;
			}
		}
	}
}

void ABridgeComponent::InitSteppers()
{
	if (NavigationAgent && HighPolicy)
	{
		NavigationStepper = NewObject<USimpleStepper>(this);
		if (NavigationStepper)
		{
			TArray<TScriptInterface<IAgent>> NavAgents;
			NavAgents.Add(NavigationAgent);
			TScriptInterface<IPolicy> PolicyInterface;
			PolicyInterface.SetObject(HighPolicy);
			PolicyInterface.SetInterface(Cast<IPolicy>(HighPolicy));
			if (!NavigationStepper->Init(NavAgents, PolicyInterface))
			{
				UE_LOG(LogTemp, Error, TEXT("[BridgePawn] NavigationStepper init failed."));
				NavigationStepper = nullptr;
			}
		}
	}

	if (ControlAgent && LowPolicy)
	{
		ControlStepper = NewObject<UPipelinedStepper>(this);
		if (ControlStepper)
		{
			TArray<TScriptInterface<IAgent>> ControlAgents;
			ControlAgents.Add(ControlAgent);
			TScriptInterface<IPolicy> PolicyInterface;
			PolicyInterface.SetObject(LowPolicy);
			PolicyInterface.SetInterface(Cast<IPolicy>(LowPolicy));
			if (!ControlStepper->Init(ControlAgents, PolicyInterface))
			{
				UE_LOG(LogTemp, Error, TEXT("[BridgePawn] ControlStepper init failed."));
				ControlStepper = nullptr;
			}
		}
	}
}

void ABridgeComponent::StepHighLevelInference()
{
	if (!NavigationStepper || !NavigationComp || !EnvComp || !TargetActor)
	{
		return;
	}

	NavigationStepper->Step();

	// 高层 Agent 在 Act 中写入 RawHighLevelCommand，这里统一做平滑后再下发给低层。
	ApplySmoothCommand(NavigationComp->RawHighLevelCommand);
	EnvComp->SetHighLevelCommand(SmoothedCommand);
}

void ABridgeComponent::StepLowLevelInference()
{
	if (!ControlStepper)
	{
		return;
	}

	ControlStepper->Step();
}

void ABridgeComponent::ApplySmoothCommand(const FVector2D& NewCommand)
{
	// 指数移动平均平滑
	SmoothedCommand.X = CommandSmoothAlpha * NewCommand.X + (1.0f - CommandSmoothAlpha) * SmoothedCommand.X;
	SmoothedCommand.Y = CommandSmoothAlpha * NewCommand.Y + (1.0f - CommandSmoothAlpha) * SmoothedCommand.Y;

	// 可选：限制范围
	SmoothedCommand.X = FMath::Clamp(SmoothedCommand.X, -1.0f, 1.0f);
	SmoothedCommand.Y = FMath::Clamp(SmoothedCommand.Y, -1.0f, 1.0f);
}

void ABridgeComponent::SetTargetActor(AActor* NewTargetActor)
{
	TargetActor = NewTargetActor;
	TargetActor = NewTargetActor;
	if (NavigationComp)
	{
		NavigationComp->TargetComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	}
	UE_LOG(LogTemp, Log, TEXT("[BridgePawn] Target actor updated to %s"), TargetActor ? *TargetActor->GetName() : TEXT("None"));
}