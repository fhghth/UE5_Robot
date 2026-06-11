#include "BridgeComponent.h"
#include "OrangeRobotEnvComponent.h"
#include "XNavigationCubeEnvComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Logging/LogMacros.h"
#include "Policies/PolicyInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ABridgeComponent::ABridgeComponent()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ABridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	ApplyDeployConfigIfPresent();

	if (!RobotActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[BridgePawn] RobotActor is null!"));
		return;
	}

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
	else
	{
		if (TargetActor)
		{
			NavigationComp->TargetComponent = TargetActor->GetRootComponent();
		}
		if (!NavigationComp->CubeComponent)
		{
			NavigationComp->CubeComponent = EnvComp->BodyLinks.Num() > 0
				? Cast<UPrimitiveComponent>(EnvComp->BodyLinks[0])
				: Cast<UPrimitiveComponent>(RobotActor->GetRootComponent());
			UE_LOG(LogTemp, Log, TEXT("[BridgePawn] Auto-filled Nav CubeComponent=%s"),
				NavigationComp->CubeComponent ? *NavigationComp->CubeComponent->GetName() : TEXT("null"));
		}
	}

	// // --- 部署兼容：无 RPC Connector 时自动从 Actor 组件层级填充关键引用 ---
	// if (EnvComp->DriveConstraints.IsEmpty())
	// {
	// 	TArray<UPhysicsConstraintComponent*> Constraints;
	// 	RobotActor->GetComponents<UPhysicsConstraintComponent>(Constraints);
	// 	EnvComp->DriveConstraints = MoveTemp(Constraints);
	// 	UE_LOG(LogTemp, Log, TEXT("[BridgePawn] Auto-filled DriveConstraints: %d"), EnvComp->DriveConstraints.Num());
	// }
	// if (EnvComp->BodyLinks.IsEmpty())
	// {
	// 	TArray<UStaticMeshComponent*> Meshes;
	// 	RobotActor->GetComponents<UStaticMeshComponent>(Meshes);
	// 	for (UStaticMeshComponent* M : Meshes)
	// 	{
	// 		if (!M) continue;
	// 		if (M->GetName().Contains(TEXT("Foot")))
	// 		{
	// 			if (!EnvComp->FootL)      EnvComp->FootL = M;
	// 			else if (!EnvComp->FootR) EnvComp->FootR = M;
	// 		}
	// 		else
	// 		{
	// 			EnvComp->BodyLinks.Add(M);
	// 		}
	// 	}
	// 	UE_LOG(LogTemp, Log, TEXT("[BridgePawn] Auto-filled BodyLinks=%d FootL=%s FootR=%s"),
	// 		EnvComp->BodyLinks.Num(),
	// 		EnvComp->FootL ? *EnvComp->FootL->GetName() : TEXT("null"),
	// 		EnvComp->FootR ? *EnvComp->FootR->GetName() : TEXT("null"));
	// }
	// if (!EnvComp->TiltCheckComponent)
	// {
	// 	TArray<USceneComponent*> Scenes;
	// 	RobotActor->GetComponents<USceneComponent>(Scenes);
	// 	for (USceneComponent* S : Scenes)
	// 	{
	// 		if (S && S->GetName().Contains(TEXT("Head")) && !EnvComp->HeadComponent)
	// 			EnvComp->HeadComponent = S;
	// 		if (S && S->GetName().Contains(TEXT("Tilt")) && !EnvComp->TiltCheckComponent)
	// 			EnvComp->TiltCheckComponent = S;
	// 	}
	// 	if (!EnvComp->TiltCheckComponent && EnvComp->BodyLinks.Num() > 0)
	// 		EnvComp->TiltCheckComponent = EnvComp->BodyLinks[0];
	// 	UE_LOG(LogTemp, Log, TEXT("[BridgePawn] Auto-filled TiltCheck=%s Head=%s"),
	// 		EnvComp->TiltCheckComponent ? *EnvComp->TiltCheckComponent->GetName() : TEXT("null"),
	// 		EnvComp->HeadComponent ? *EnvComp->HeadComponent->GetName() : TEXT("null"));
	// }
	// // --- 部署兼容结束 ---

	EnvComp->bEnableHighLevelCommand = true;
	EnvComp->bEnableCommandReward = true;
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

void ABridgeComponent::ApplyDeployConfigIfPresent()
{
	FString DeployConfigPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("DeployConfig="), DeployConfigPath))
	{
		return;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *DeployConfigPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[BridgePawn] Failed to load deploy config: %s"), *DeployConfigPath);
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[BridgePawn] Failed to parse deploy config JSON: %s"), *DeployConfigPath);
		return;
	}

	const FString HighModelPath = JsonObject->GetStringField(TEXT("high_level_onnx_path"));
	const FString LowModelPath = JsonObject->GetStringField(TEXT("low_level_onnx_path"));
	const FString TargetActorName = JsonObject->GetStringField(TEXT("target_actor_name"));

	if (!TargetActorName.IsEmpty())
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->GetName() == TargetActorName)
			{
				TargetActor = *It;
				break;
			}
		}
	}

	if (!HighModelPath.IsEmpty())
	{
		HighLevelModelData = LoadModelDataFromDisk(HighModelPath, RuntimeHighLevelModelData, TEXT("High"));
	}

	if (!LowModelPath.IsEmpty())
	{
		LowLevelModelData = LoadModelDataFromDisk(LowModelPath, RuntimeLowLevelModelData, TEXT("Low"));
	}
}

UNNEModelData* ABridgeComponent::LoadModelDataFromDisk(const FString& ModelPath, UNNEModelData*& RuntimeModelStorage, const TCHAR* LogLabel)
{
	TArray64<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ModelPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[BridgePawn] %s-level model load failed: %s"), LogLabel, *ModelPath);
		return nullptr;
	}

	RuntimeModelStorage = NewObject<UNNEModelData>(this);
	if (!RuntimeModelStorage)
	{
		UE_LOG(LogTemp, Error, TEXT("[BridgePawn] %s-level runtime model data allocation failed."), LogLabel);
		return nullptr;
	}

	RuntimeModelStorage->Init(TEXT("onnx"), TConstArrayView64<uint8>(FileData));
	UE_LOG(LogTemp, Log, TEXT("[BridgePawn] %s-level ONNX loaded from disk: %s"), LogLabel, *ModelPath);
	return RuntimeModelStorage;
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
	SmoothedCommand.X = CommandSmoothAlpha * NewCommand.X + (1.0f - CommandSmoothAlpha) * SmoothedCommand.X;
	SmoothedCommand.Y = CommandSmoothAlpha * NewCommand.Y + (1.0f - CommandSmoothAlpha) * SmoothedCommand.Y;

	SmoothedCommand.X = FMath::Clamp(SmoothedCommand.X, -1.0f, 1.0f);
	SmoothedCommand.Y = FMath::Clamp(SmoothedCommand.Y, -1.0f, 1.0f);
}

void ABridgeComponent::SetTargetActor(AActor* NewTargetActor)
{
	TargetActor = NewTargetActor;
	if (NavigationComp)
	{
		NavigationComp->TargetComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	}
	UE_LOG(LogTemp, Log, TEXT("[BridgePawn] Target actor updated to %s"), TargetActor ? *TargetActor->GetName() : TEXT("None"));
}
