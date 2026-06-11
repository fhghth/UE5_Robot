// EnvConfigLoader.h - 从命令行参数加载环境配置（通用反射版本）

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EnvConfigLoader.generated.h"

/**
 * 用于从命令行参数 -EnvConfig=<path> 加载 JSON 配置并反射写入目标组件。
 * 支持任意 UActorComponent 子类，JSON key 与 UPROPERTY 名称按需匹配。
 */
UCLASS()
class ORANGEROBOT_API UEnvConfigLoader : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 从命令行参数 -EnvConfig=<path> 加载配置文件并通过反射应用到目标组件。
	 * @param Target 要配置的组件（UActorComponent 及其子类均可）
	 * @return 是否成功加载并应用配置
	 */
	UFUNCTION(BlueprintCallable, Category = "Env|Config")
	static bool LoadConfigFromCommandLine(UActorComponent* Target);
};
