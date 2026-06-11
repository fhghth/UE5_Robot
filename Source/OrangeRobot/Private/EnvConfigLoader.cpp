// EnvConfigLoader.cpp - 通用反射版本

#include "EnvConfigLoader.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UEnvConfigLoader::LoadConfigFromCommandLine(UActorComponent* Target)
{
	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("EnvConfigLoader: Target is null"));
		return false;
	}

	FString ConfigPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("EnvConfig="), ConfigPath))
	{
		UE_LOG(LogTemp, Log, TEXT("EnvConfigLoader: No -EnvConfig parameter found, using default values"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("EnvConfigLoader: Loading config from: %s"), *ConfigPath);

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ConfigPath))
	{
		UE_LOG(LogTemp, Error, TEXT("EnvConfigLoader: Failed to load config file: %s"), *ConfigPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("EnvConfigLoader: Failed to parse JSON from: %s"), *ConfigPath);
		return false;
	}

	int32 AppliedCount = 0;

	for (TFieldIterator<FProperty> PropIt(Target->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		const FString PropName = Prop->GetName();

		if (!JsonObject->HasField(PropName))
		{
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);

		auto TryApplyFloat = [&]() -> bool
		{
			if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				FloatProp->SetFloatingPointPropertyValue(ValuePtr,
					static_cast<float>(JsonObject->GetNumberField(PropName)));
				return true;
			}
			return false;
		};

		auto TryApplyDouble = [&]() -> bool
		{
			if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
			{
				DoubleProp->SetFloatingPointPropertyValue(ValuePtr, JsonObject->GetNumberField(PropName));
				return true;
			}
			return false;
		};

		auto TryApplyInt = [&]() -> bool
		{
			if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
			{
				IntProp->SetPropertyValue(ValuePtr,
					static_cast<int32>(JsonObject->GetIntegerField(PropName)));
				return true;
			}
			if (FInt64Property* Int64Prop = CastField<FInt64Property>(Prop))
			{
				Int64Prop->SetPropertyValue(ValuePtr, JsonObject->GetIntegerField(PropName));
				return true;
			}
			return false;
		};

		auto TryApplyBool = [&]() -> bool
		{
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				BoolProp->SetPropertyValue(ValuePtr, JsonObject->GetBoolField(PropName));
				return true;
			}
			return false;
		};

		auto TryApplyArrayInt = [&]() -> bool
		{
			FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
			if (!ArrayProp)
			{
				return false;
			}
			if (!JsonObject->HasTypedField<EJson::Array>(PropName))
			{
				return true; // 字段存在但不是数组，不算错误
			}

			FIntProperty* InnerIntProp = CastField<FIntProperty>(ArrayProp->Inner);
			if (!InnerIntProp)
			{
				return true; // 非 int 数组，跳过
			}

			FScriptArrayHelper Helper(ArrayProp, ValuePtr);
			const TArray<TSharedPtr<FJsonValue>>& JsonArray = JsonObject->GetArrayField(PropName);
			Helper.Resize(JsonArray.Num());
			for (int32 i = 0; i < JsonArray.Num(); ++i)
			{
				InnerIntProp->SetPropertyValue(Helper.GetRawPtr(i),
					static_cast<int32>(JsonArray[i]->AsNumber()));
			}
			return true;
		};

		bool bApplied = false;
		bApplied = TryApplyFloat() || TryApplyDouble() || TryApplyInt() || TryApplyBool() || TryApplyArrayInt();

		if (bApplied)
		{
			AppliedCount++;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("EnvConfigLoader: Applied %d parameters to %s from config file"),
		AppliedCount, *Target->GetClass()->GetName());
	return true;
}
