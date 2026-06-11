// Fill out your copyright notice in the Description page of Project Settings.

#include "OrangeRobotEnvComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformFilemanager.h"
#include "UObject/FieldIterator.h"
#include "JsonObjectConverter.h"  
<<<<<<< HEAD
#include "EnvConfigLoader.h"
=======
>>>>>>> worktree-training-stability-fixes

namespace
{
    const TCHAR* AngularDriveModeToString(EAngularDriveMode::Type DriveMode)
    {
        switch (DriveMode)
        {
        case EAngularDriveMode::SLERP:          return TEXT("SLERP");
        case EAngularDriveMode::TwistAndSwing:  return TEXT("TwistAndSwing");
        default:                                return TEXT("Unknown");
        }
    }

    const TCHAR* AngularMotionToString(EAngularConstraintMotion Motion)
    {
        switch (Motion)
        {
        case ACM_Free:    return TEXT("Free");
        case ACM_Limited: return TEXT("Limited");
        case ACM_Locked:  return TEXT("Locked");
        default:          return TEXT("Unknown");
        }
    }

    void GetAngularMotions(UPhysicsConstraintComponent* Constraint,
                           EAngularConstraintMotion& TwistMotion,
                           EAngularConstraintMotion& Swing1Motion,
                           EAngularConstraintMotion& Swing2Motion)
    {
        TwistMotion = ACM_Locked;
        Swing1Motion = ACM_Locked;
        Swing2Motion = ACM_Locked;

        if (Constraint)
        {
            FConstraintInstance& Instance = Constraint->ConstraintInstance;
            TwistMotion  = Instance.GetAngularTwistMotion();
            Swing1Motion = Instance.GetAngularSwing1Motion();
            Swing2Motion = Instance.GetAngularSwing2Motion();
        }
    }
}

// ---------------------------------------------------------------------------
// 静态 / 辅助函数
// ---------------------------------------------------------------------------

float UOrangeRobotEnvComponent::ShapeNormalizedAction(float Value, float Exponent)
{
    const float ClampedValue = FMath::Clamp(Value, -1.0f, 1.0f);
    const float SafeExponent = FMath::Max(Exponent, 1.0f);
    return FMath::Sign(ClampedValue) * FMath::Pow(FMath::Abs(ClampedValue), SafeExponent);
}

float UOrangeRobotEnvComponent::SanitizeFiniteScalar(float Value, float MinValue, float MaxValue)
{
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, MinValue, MaxValue) : 0.0f;
}

FVector UOrangeRobotEnvComponent::SanitizeFiniteVector(const FVector& Value, float MinValue, float MaxValue)
{
    return FVector(SanitizeFiniteScalar(Value.X, MinValue, MaxValue),
                   SanitizeFiniteScalar(Value.Y, MinValue, MaxValue),
                   SanitizeFiniteScalar(Value.Z, MinValue, MaxValue));
}

float UOrangeRobotEnvComponent::SanitizeFiniteAngleDegrees(float Value)
{
    return FMath::IsFinite(Value) ? FMath::UnwindDegrees(Value) : 0.0f;
}

FVector UOrangeRobotEnvComponent::ClampAngularVelocityTarget(const FVector& TargetVel) const
{
    return FVector(
        SanitizeFiniteScalar(TargetVel.X, -TwistVelocityLimit, TwistVelocityLimit),
        SanitizeFiniteScalar(TargetVel.Y, -SwingVelocityLimit, SwingVelocityLimit),
        SanitizeFiniteScalar(TargetVel.Z, -SwingVelocityLimit, SwingVelocityLimit));
}

UPrimitiveComponent* UOrangeRobotEnvComponent::GetTrunkPrimitive() const
{
    if (TiltCheckComponent)
    {
        if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(TiltCheckComponent))
        {
            return Prim;
        }
    }
    if (BodyLinks.Num() > 0 && BodyLinks[0])
    {
        return BodyLinks[0];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 自定义轴辅助函数
// ---------------------------------------------------------------------------

FVector UOrangeRobotEnvComponent::GetAxisVector(const FQuat& Rotation, ERobotAxisDirection Axis) const
{
    switch (Axis)
    {
    case ERobotAxisDirection::PlusX:  return Rotation.GetAxisX();
    case ERobotAxisDirection::PlusY:  return Rotation.GetAxisY();
    case ERobotAxisDirection::PlusZ:  return Rotation.GetAxisZ();
    case ERobotAxisDirection::MinusX: return -Rotation.GetAxisX();
    case ERobotAxisDirection::MinusY: return -Rotation.GetAxisY();
    case ERobotAxisDirection::MinusZ: return -Rotation.GetAxisZ();
    default:                           return Rotation.GetAxisX();
    }
}

FVector UOrangeRobotEnvComponent::GetRobotForwardVector(const UPrimitiveComponent* Comp) const
{
    if (!Comp) return FVector::ForwardVector;
    return GetAxisVector(Comp->GetComponentQuat(), RobotForwardAxis).GetSafeNormal();
}

FVector UOrangeRobotEnvComponent::GetRobotRightVector(const UPrimitiveComponent* Comp) const
{
    if (!Comp) return FVector::RightVector;
    return GetAxisVector(Comp->GetComponentQuat(), RobotRightAxis).GetSafeNormal();
}

FVector UOrangeRobotEnvComponent::GetLocalRobotPlanarVelocity(const UPrimitiveComponent* Component, const FVector& WorldVelocity) const
{
    if (!Component) return FVector::ZeroVector;
    const FVector Forward = GetRobotForwardVector(Component).GetSafeNormal2D();
    const FVector Right   = GetRobotRightVector(Component).GetSafeNormal2D();
    const FVector PlanarVelocity(WorldVelocity.X, WorldVelocity.Y, 0.0f);
    return FVector(
        FVector::DotProduct(PlanarVelocity, Forward),   // 前进速度
        FVector::DotProduct(PlanarVelocity, Right),     // 右侧速度
        0.0f);
}

// ---------------------------------------------------------------------------
// 构造函数 & 调试绘制
// ---------------------------------------------------------------------------

UOrangeRobotEnvComponent::UOrangeRobotEnvComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

<<<<<<< HEAD
void UOrangeRobotEnvComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!bEnvConfigLoadedFromCommandLine)
    {
        bEnvConfigLoadedFromCommandLine = UEnvConfigLoader::LoadConfigFromCommandLine(this);
        if (bEnvConfigLoadedFromCommandLine)
        {
            UE_LOG(LogTemp, Log, TEXT("OrangeRobotEnvComponent: runtime env config applied from command line"));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("OrangeRobotEnvComponent: using blueprint/default config values"));
        }
    }
}

//一键导出参数
void UOrangeRobotEnvComponent::ExportAllConfigToJSON()
{
    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();

    // 遍历自身及父类的所有属性（UE 反射）
    for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        FString PropName = Prop->GetName();
        const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(this);

        TSharedPtr<FJsonValue> JsonValue;

        // 常见基础类型
        if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
        }
        else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueNumber>(FloatProp->GetPropertyValue(ValuePtr));
        }
        else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueNumber>(DoubleProp->GetPropertyValue(ValuePtr));
        }
        else if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueNumber>(IntProp->GetPropertyValue(ValuePtr));
        }
        else if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
        }
        // 对象/组件引用（输出路径名）
        else if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
        {
            UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
            JsonValue = MakeShared<FJsonValueString>(Obj ? Obj->GetPathName() : TEXT("None"));
        }
        // 常见结构体
        else if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
        {
            if (StructProp->Struct == TBaseStructure<FVector>::Get())
            {
                const FVector* Vec = Prop->ContainerPtrToValuePtr<FVector>(this);
                JsonValue = MakeShared<FJsonValueString>(Vec->ToString());
            }
            else if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
            {
                const FVector2D* Vec2 = Prop->ContainerPtrToValuePtr<FVector2D>(this);
                JsonValue = MakeShared<FJsonValueString>(Vec2->ToString());
            }
            else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
            {
                const FRotator* Rot = Prop->ContainerPtrToValuePtr<FRotator>(this);
                JsonValue = MakeShared<FJsonValueString>(Rot->ToString());
            }
            else if (StructProp->Struct == TBaseStructure<FTransform>::Get())
            {
                const FTransform* Trans = Prop->ContainerPtrToValuePtr<FTransform>(this);
                JsonValue = MakeShared<FJsonValueString>(Trans->ToString());
            }
            else
            {
                // 其他结构体，尝试导出为 JSON 对象
                TSharedRef<FJsonObject> SubObj = MakeShared<FJsonObject>();
                if (FJsonObjectConverter::UStructToJsonObject(StructProp->Struct, ValuePtr, SubObj))
                {
                    JsonValue = MakeShared<FJsonValueObject>(SubObj);
                }
                else
                {
                    JsonValue = MakeShared<FJsonValueString>(TEXT("<ComplexStruct>"));
                }
            }
        }
        // 数组（简单序列化为字符串，或可进一步展开）
        else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
        {
            FScriptArrayHelper Helper(ArrayProp, ValuePtr);
            TArray<FString> Elements;
            for (int32 i = 0; i < Helper.Num(); ++i)
            {
                // 对于 float/int 数组可转数值，这里简化处理
                Elements.Add(TEXT("<element>"));
            }
            JsonValue = MakeShared<FJsonValueString>(
                FString::Printf(TEXT("[Array of %d elements]"), Helper.Num()));
        }
        else
        {
            JsonValue = MakeShared<FJsonValueString>(TEXT("<Unsupported type>"));
        }

        if (JsonValue.IsValid())
        {
            RootObject->SetField(PropName, JsonValue);
        }
    }

    // 序列化 JSON
    FString JsonString;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
    FJsonSerializer::Serialize(RootObject, Writer);

    // 保存到项目目录/Logs
    FString LogDir = FPaths::ProjectDir() / TEXT("Logs");
    IFileManager::Get().MakeDirectory(*LogDir, true);  // 确保目录存在

    FDateTime Now = FDateTime::Now();
    FString FileName = FString::Printf(TEXT("ConfigExport_%s.json"),
        *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
    FString FullPath = LogDir / FileName;

    bool bSaved = FFileHelper::SaveStringToFile(JsonString, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (bSaved)
    {
        UE_LOG(LogTemp, Warning, TEXT("Config exported successfully -> %s"), *FullPath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save config to %s"), *FullPath);
    }

    // 同时输出到 Output Log
    UE_LOG(LogTemp, Warning, TEXT("=========== Full Config JSON ===========\n%s\n========================================"), *JsonString);
}

void UOrangeRobotEnvComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if WITH_EDITOR
    if (bEnableHighLevelCommand) DrawHighLevelCommandDebug();
    DrawRobotCoordinateAxes();
#endif
}

=======
//一键导出参数
void UOrangeRobotEnvComponent::ExportAllConfigToJSON()
{
    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();

    // 遍历自身及父类的所有属性（UE 反射）
    for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        FString PropName = Prop->GetName();
        const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(this);

        TSharedPtr<FJsonValue> JsonValue;

        // 常见基础类型
        if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
        }
        else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueNumber>(FloatProp->GetPropertyValue(ValuePtr));
        }
        else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueNumber>(DoubleProp->GetPropertyValue(ValuePtr));
        }
        else if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueNumber>(IntProp->GetPropertyValue(ValuePtr));
        }
        else if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
        {
            JsonValue = MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
        }
        // 对象/组件引用（输出路径名）
        else if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
        {
            UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
            JsonValue = MakeShared<FJsonValueString>(Obj ? Obj->GetPathName() : TEXT("None"));
        }
        // 常见结构体
        else if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
        {
            if (StructProp->Struct == TBaseStructure<FVector>::Get())
            {
                const FVector* Vec = Prop->ContainerPtrToValuePtr<FVector>(this);
                JsonValue = MakeShared<FJsonValueString>(Vec->ToString());
            }
            else if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
            {
                const FVector2D* Vec2 = Prop->ContainerPtrToValuePtr<FVector2D>(this);
                JsonValue = MakeShared<FJsonValueString>(Vec2->ToString());
            }
            else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
            {
                const FRotator* Rot = Prop->ContainerPtrToValuePtr<FRotator>(this);
                JsonValue = MakeShared<FJsonValueString>(Rot->ToString());
            }
            else if (StructProp->Struct == TBaseStructure<FTransform>::Get())
            {
                const FTransform* Trans = Prop->ContainerPtrToValuePtr<FTransform>(this);
                JsonValue = MakeShared<FJsonValueString>(Trans->ToString());
            }
            else
            {
                // 其他结构体，尝试导出为 JSON 对象
                TSharedRef<FJsonObject> SubObj = MakeShared<FJsonObject>();
                if (FJsonObjectConverter::UStructToJsonObject(StructProp->Struct, ValuePtr, SubObj))
                {
                    JsonValue = MakeShared<FJsonValueObject>(SubObj);
                }
                else
                {
                    JsonValue = MakeShared<FJsonValueString>(TEXT("<ComplexStruct>"));
                }
            }
        }
        // 数组（简单序列化为字符串，或可进一步展开）
        else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
        {
            FScriptArrayHelper Helper(ArrayProp, ValuePtr);
            TArray<FString> Elements;
            for (int32 i = 0; i < Helper.Num(); ++i)
            {
                // 对于 float/int 数组可转数值，这里简化处理
                Elements.Add(TEXT("<element>"));
            }
            JsonValue = MakeShared<FJsonValueString>(
                FString::Printf(TEXT("[Array of %d elements]"), Helper.Num()));
        }
        else
        {
            JsonValue = MakeShared<FJsonValueString>(TEXT("<Unsupported type>"));
        }

        if (JsonValue.IsValid())
        {
            RootObject->SetField(PropName, JsonValue);
        }
    }

    // 序列化 JSON
    FString JsonString;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
    FJsonSerializer::Serialize(RootObject, Writer);

    // 保存到项目目录/Logs
    FString LogDir = FPaths::ProjectDir() / TEXT("Logs");
    IFileManager::Get().MakeDirectory(*LogDir, true);  // 确保目录存在

    FDateTime Now = FDateTime::Now();
    FString FileName = FString::Printf(TEXT("ConfigExport_%s.json"),
        *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
    FString FullPath = LogDir / FileName;

    bool bSaved = FFileHelper::SaveStringToFile(JsonString, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (bSaved)
    {
        UE_LOG(LogTemp, Warning, TEXT("Config exported successfully -> %s"), *FullPath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save config to %s"), *FullPath);
    }

    // 同时输出到 Output Log
    UE_LOG(LogTemp, Warning, TEXT("=========== Full Config JSON ===========\n%s\n========================================"), *JsonString);
}

void UOrangeRobotEnvComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if WITH_EDITOR
    if (bEnableHighLevelCommand) DrawHighLevelCommandDebug();
    DrawRobotCoordinateAxes();
#endif
}

>>>>>>> worktree-training-stability-fixes
#if WITH_EDITOR
//命令调试
void UOrangeRobotEnvComponent::DrawHighLevelCommandDebug() const
{
    // 调试代码
}
//机器人轴方向
void UOrangeRobotEnvComponent::DrawRobotCoordinateAxes() const
{
    UPrimitiveComponent* TrunkComp = GetTrunkPrimitive();
    if (!TrunkComp || !GetWorld()) return;

    const FVector Origin = TrunkComp->GetComponentLocation();
    const float AxisLength = 50.0f; // cm

    const FVector Fwd = GetRobotForwardVector(TrunkComp);
    const FVector Rgt = GetRobotRightVector(TrunkComp);
    const FVector Up  = TrunkComp->GetUpVector();

    DrawDebugLine(GetWorld(), Origin, Origin + Fwd * AxisLength, FColor::Red, false, -1.f, 0, 2.f);
    DrawDebugLine(GetWorld(), Origin, Origin + Rgt * AxisLength, FColor::Green, false, -1.f, 0, 2.f);
    DrawDebugLine(GetWorld(), Origin, Origin + Up  * AxisLength, FColor::Blue, false, -1.f, 0, 2.f);
}
#endif

// ---------------------------------------------------------------------------
// 约束轴缓存
// ---------------------------------------------------------------------------

FOrangeRobotConstraintAxisCache UOrangeRobotEnvComponent::BuildConstraintAxisCache(UPhysicsConstraintComponent* Constraint) const
{
    FOrangeRobotConstraintAxisCache AxisCache;
    if (!Constraint) return AxisCache;

    FConstraintInstanceAccessor Accessor(Constraint);
    TEnumAsByte<EAngularDriveMode::Type> DriveMode = EAngularDriveMode::TwistAndSwing;
    bool bTwistVel = false, bSwingVel = false, bSlerpVel = false;

    UConstraintInstanceBlueprintLibrary::GetAngularDriveMode(Accessor, DriveMode);
    UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveTwistAndSwing(Accessor, bTwistVel, bSwingVel);
    UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveSLERP(Accessor, bSlerpVel);

    EAngularConstraintMotion TwistMotion, Swing1Motion, Swing2Motion;
    GetAngularMotions(Constraint, TwistMotion, Swing1Motion, Swing2Motion);

    if (DriveMode == EAngularDriveMode::TwistAndSwing)
    {
        AxisCache.bUseTwist  = bTwistVel && TwistMotion != ACM_Locked;
        AxisCache.bUseSwing1 = bSwingVel && Swing1Motion != ACM_Locked;
        AxisCache.bUseSwing2 = bSwingVel && Swing2Motion != ACM_Locked;
    }
    else if (DriveMode == EAngularDriveMode::SLERP)
    {
        const bool bAnyFree = TwistMotion != ACM_Locked || Swing1Motion != ACM_Locked || Swing2Motion != ACM_Locked;
        if (bSlerpVel && bAnyFree)
        {
            AxisCache.bUseTwist  = TwistMotion  != ACM_Locked;
            AxisCache.bUseSwing1 = Swing1Motion != ACM_Locked;
            AxisCache.bUseSwing2 = Swing2Motion != ACM_Locked;
        }
    }

    return AxisCache;
}

void UOrangeRobotEnvComponent::CacheJointActionAxes()
{
    JointActionAxes.Empty();
    JointAxisCaches.Empty();
    JointActionAxes.Reserve(DriveConstraints.Num());
    JointAxisCaches.Reserve(DriveConstraints.Num());

    for (int32 i = 0; i < DriveConstraints.Num(); ++i)
    {
        UPhysicsConstraintComponent* Con = DriveConstraints[i];
        const FOrangeRobotConstraintAxisCache Cache = BuildConstraintAxisCache(Con);
        JointAxisCaches.Add(Cache);
        JointActionAxes.Add(Cache.GetAxisCount());
    }
}

// ---------------------------------------------------------------------------
// 空间维度查询
// ---------------------------------------------------------------------------

int32 UOrangeRobotEnvComponent::GetLowLevelObservationDim() const
{
    int32 JointDim = 0;
    for (const FOrangeRobotConstraintAxisCache& AxisCache : JointAxisCaches)
    {
        JointDim += AxisCache.GetAxisCount() * 2;
    }
    return 8 + 6 + JointDim + 2; // +2 for gait phase sin/cos
}

int32 UOrangeRobotEnvComponent::GetObservationDim() const
{
    return GetLowLevelObservationDim() + (bEnableCommandReward ? 2 : 0);
}

void UOrangeRobotEnvComponent::SetHighLevelCommand(FVector2D InHighLevelCommand)
{
    if (!bEnableHighLevelCommand && !bEnableCommandReward) return;
    HighLevelCommand.X = SanitizeFiniteScalar(InHighLevelCommand.X, -1.0f, 1.0f);
    HighLevelCommand.Y = SanitizeFiniteScalar(InHighLevelCommand.Y, -1.0f, 1.0f);
}

void UOrangeRobotEnvComponent::ClearHighLevelCommand()
{
    HighLevelCommand = FVector2D::ZeroVector;
}

void UOrangeRobotEnvComponent::SetCommandRewardScale(float Scale)
{
    CommandRewardScale = FMath::Clamp(Scale, 0.0f, 1.0f);
}

void UOrangeRobotEnvComponent::SetDynamicBalanceRewardWeight(float Weight)
{
    DynamicBalanceRewardWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
}

void UOrangeRobotEnvComponent::SetGaitSymmetryPenaltyScale(float Scale)
{
    GaitSymmetryPenaltyScale = FMath::Max(Scale, 0.0f);
}

void UOrangeRobotEnvComponent::SetStepFrequencyRewardScale(float Scale)
{
    StepFrequencyRewardScale = FMath::Max(Scale, 0.0f);
}

void UOrangeRobotEnvComponent::SetCostOfTransportScale(float Scale)
{
    CostOfTransportScale = FMath::Max(Scale, 0.0f);
}

float UOrangeRobotEnvComponent::GetJointAngle(int32 JointIndex, int32 AxisSlot) const
{
    if (!DriveConstraints.IsValidIndex(JointIndex) || !DriveConstraints[JointIndex])
        return 0.0f;

    UPhysicsConstraintComponent* Con = DriveConstraints[JointIndex];
    switch (AxisSlot)
    {
    case 0: return SanitizeFiniteScalar(Con->GetCurrentTwist(),  -180.0f, 180.0f) / 180.0f;
    case 1: return SanitizeFiniteScalar(Con->GetCurrentSwing1(), -180.0f, 180.0f) / 180.0f;
    case 2: return SanitizeFiniteScalar(Con->GetCurrentSwing2(), -180.0f, 180.0f) / 180.0f;
    default: return 0.0f;
    }
}

void UOrangeRobotEnvComponent::SampleEpisodeHighLevelCommand()
{
    if (bEnableCommandReward)
    {
        SetHighLevelCommand(FVector2D(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f)));
    }
    else
    {
        ClearHighLevelCommand();
    }
}

// ---------------------------------------------------------------------------
// 内建课程学习
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::UpdateCurriculumWeightsAndCommand()
{
    if (!bEnableCurriculum) return;

    int32 Stage = 0;
    for (int32 Bound : CurriculumStageBoundaries)
    {
        if (GlobalTrainingStep < Bound) break;
        ++Stage;
    }
    CurrentCurriculumStage = FMath::Clamp(Stage, 0, 4);

    auto LerpWeight = [&](float StartVal, float EndVal, int32 StartStep, int32 EndStep) -> float
    {
        if (GlobalTrainingStep <= StartStep) return StartVal;
        if (GlobalTrainingStep >= EndStep) return EndVal;
        const float Alpha = float(GlobalTrainingStep - StartStep) / float(EndStep - StartStep);
        return FMath::Lerp(StartVal, EndVal, Alpha);
    };

    auto GetBound = [&](int32 Index, int32 Default) -> int32
    {
        return CurriculumStageBoundaries.IsValidIndex(Index) ? CurriculumStageBoundaries[Index] : Default;
    };

    const int32 B0 = GetBound(0, 150000);
    const int32 B1 = GetBound(1, 300000);
    const int32 B2 = GetBound(2, 600000);
    const int32 B3 = GetBound(3, 1000000);
    
    float DynamicW = 1.0f;
    float CmdScale = 1.0f;
    float SymScale = 0.02f;
    float FreqScale = 0.1f;
    float CoTScale = 0.003f;
    FVector2D Cmd = FVector2D::ZeroVector;

    auto SampleCmd = [&](float FwdMin, float FwdMax, float TurnMin, float TurnMax)
    {
        Cmd.X = FMath::FRandRange(FwdMin, FwdMax);
        Cmd.Y = FMath::FRandRange(TurnMin, TurnMax);
    };

    if (CurrentCurriculumStage == 0)
    {
        // 阶段0：纯站立
        DynamicW = 0.0f;         // 关闭步态奖励
        CmdScale = 1.0f;         // 命令跟踪奖励全开
        SymScale = 0.0f;
        FreqScale = 0.0f;
        CoTScale = 0.0f;
        Cmd = FVector2D::ZeroVector; // 命令固定为零
    }
    else if (CurrentCurriculumStage == 1)
    {
        // 阶段1：极慢速前进，无转向
        DynamicW = LerpWeight(0.0f, 0.3f, B0, B1);
        CmdScale = 1.0f;
        SymScale = 0.0f;
        FreqScale = 0.0f;
        CoTScale = 0.0f;
        SampleCmd(0.05f, 0.20f, 0.0f, 0.0f);
    }
    else if (CurrentCurriculumStage == 2)
    {
        DynamicW = LerpWeight(0.3f, 0.6f, B1, B2);
        CmdScale = 1.0f;         // 始终保持开启
        SymScale = LerpWeight(0.0f, 0.02f, B1, B2);
        FreqScale = LerpWeight(0.0f, 0.1f, B1, B2);
        CoTScale = 0.0f;
        SampleCmd(0.20f, 0.40f, -0.05f, 0.05f);
    }
    else if (CurrentCurriculumStage == 3)
    {
        // StandW = 0.0f;
        DynamicW = LerpWeight(0.6f, 1.0f, B2, B3);
        CmdScale = 1.0f;
        SymScale = 0.02f;
        FreqScale = 0.1f;
        CoTScale = LerpWeight(0.0f, 0.003f, B2, B3);
        SampleCmd(-1.0f, 1.0f, -0.5f, 0.5f);
    }
    else
    {
        // StandW = 0.0f;
        DynamicW = 1.0f;
        CmdScale = 1.0f;
        SymScale = 0.02f;
        FreqScale = 0.1f;
        CoTScale = 0.003f;
        SampleCmd(-1.0f, 1.0f, -1.0f, 1.0f);
    }
    
    SetDynamicBalanceRewardWeight(DynamicW);
    SetCommandRewardScale(CmdScale);
    SetGaitSymmetryPenaltyScale(SymScale);
    SetStepFrequencyRewardScale(FreqScale);
    SetCostOfTransportScale(CoTScale);
    SetHighLevelCommand(Cmd);

    UE_LOG(LogTemp, Verbose, TEXT("Curriculum: GlobalStep=%d, Stage=%d, Dynamic=%.2f, Cmd=(%.2f, %.2f)"),
        GlobalTrainingStep, CurrentCurriculumStage, DynamicW, Cmd.X, Cmd.Y);
}

// ---------------------------------------------------------------------------
// 状态辅助
// ---------------------------------------------------------------------------

USceneComponent* UOrangeRobotEnvComponent::GetTiltReferenceComponent() const
{
    return TiltCheckComponent ? TiltCheckComponent : (BodyLinks.Num() > 0 ? BodyLinks[0] : nullptr);
}

float UOrangeRobotEnvComponent::GetUprightDot() const
{
    const USceneComponent* TiltComp = GetTiltReferenceComponent();
    const FVector Up = TiltComp ? TiltComp->GetUpVector() : FVector::UpVector;
    return FVector::DotProduct(Up, FVector::UpVector);
}

float UOrangeRobotEnvComponent::GetBodyHeight() const
{
    const USceneComponent* BodyComp = GetTiltReferenceComponent();
    if (BodyComp) return BodyComp->GetComponentLocation().Z;
    if (UPrimitiveComponent* TrunkComp = GetTrunkPrimitive())
        return TrunkComp->GetComponentLocation().Z;
    return 0.0f;
}


bool UOrangeRobotEnvComponent::IsComponentTouchingGround(const UPrimitiveComponent* Component) const
{
    if (!Component) return false;
    UWorld* World = GetWorld();
    if (!World) return false;

    const FVector Start = Component->GetComponentLocation();
    const FVector End = Start - FVector(0, 0, FMath::Max(FootSupportTraceDistance, 1.0f));
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FootSupportTrace), false, GetOwner());
    QueryParams.AddIgnoredComponent(Component);
    FHitResult Hit;
    return World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams);
}

bool UOrangeRobotEnvComponent::IsFootTouchingGround(const UPrimitiveComponent* FootComponent) const
{
    if (!FootComponent) return false;
    UWorld* World = GetWorld();
    if (!World) return false;

    const FVector Start = FootComponent->GetComponentLocation();
    const FVector End = Start - FVector(0,0, FMath::Max(FootSupportTraceDistance, 1.0f));
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FootSupportTrace), false, GetOwner());
    Params.AddIgnoredComponent(FootComponent);
    FHitResult Hit;
    return World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);
}

bool UOrangeRobotEnvComponent::IsFootStableSupport(const UPrimitiveComponent* FootComponent) const
{
    if (!FootComponent) return false;
    if (!IsFootTouchingGround(FootComponent)) return false;
    return FootComponent->GetComponentVelocity().Size() <= FootStableSpeedThreshold;
}

bool UOrangeRobotEnvComponent::HasStableFootSupport() const
{
    return IsFootStableSupport(FootL) || IsFootStableSupport(FootR);
}

float UOrangeRobotEnvComponent::GetFootHorizontalSpeed(const UPrimitiveComponent* FootComponent) const
{
    if (!FootComponent) return 0.0f;
    const FVector Vel = FootComponent->GetComponentVelocity();
    return FVector(Vel.X, Vel.Y, 0.0f).Size();
}

float UOrangeRobotEnvComponent::GetFootDistanceFromTrunk(const UPrimitiveComponent* FootComponent) const
{
    if (!FootComponent || !TiltCheckComponent) return 0.0f;
    return TiltCheckComponent->GetComponentLocation().Z - FootComponent->GetComponentLocation().Z;
}

FVector UOrangeRobotEnvComponent::GetSupportCenter(bool bLeftStable, bool bRightStable) const
{
    const FVector L = FootL ? FootL->GetComponentLocation() : FVector::ZeroVector;
    const FVector R = FootR ? FootR->GetComponentLocation() : FVector::ZeroVector;
    if (bLeftStable && bRightStable) return (L + R) * 0.5f;
    if (bLeftStable)  return L;
    if (bRightStable) return R;
    if (UPrimitiveComponent* Trunk = GetTrunkPrimitive()) return Trunk->GetComponentLocation();
    return FVector::ZeroVector;
}

float UOrangeRobotEnvComponent::GetTrunkSupportOffsetNormalized(bool bLeftStable, bool bRightStable) const
{
    const USceneComponent* Trunk = GetTiltReferenceComponent();
    if (!Trunk) return 0.0f;
    const FVector T = Trunk->GetComponentLocation();
    const FVector S = GetSupportCenter(bLeftStable, bRightStable);
    const FVector Offset(T.X - S.X, T.Y - S.Y, 0.0f);
    return FMath::Clamp(Offset.Size() / FMath::Max(TrunkSupportOffsetNormalizeDistance, 1.0f), 0.0f, 4.0f);
}

int32 UOrangeRobotEnvComponent::GetActionDim() const
{
    int32 Total = 0;
    for (const int32 Axes : JointActionAxes) Total += Axes;
    return Total;
}

// ---------------------------------------------------------------------------
// 初始状态记录
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::CaptureInitialTransform()
{
    ClearHighLevelCommand();

    InitialBodyLinkTransforms.Empty();
    for (UStaticMeshComponent* Link : BodyLinks)
    {
        InitialBodyLinkTransforms.Add(Link ? Link->GetComponentTransform() : FTransform::Identity);
    }

    if (TiltCheckComponent)
    {
        const float TrunkZ = TiltCheckComponent->GetComponentLocation().Z;
        InitialLeftFootDistance  = FootL ? (TrunkZ - FootL->GetComponentLocation().Z) : 0.0f;
        InitialRightFootDistance = FootR ? (TrunkZ - FootR->GetComponentLocation().Z) : 0.0f;
    }
    else
    {
        InitialLeftFootDistance = 0.0f;
        InitialRightFootDistance = 0.0f;
    }

    CacheJointActionAxes();
}

void UOrangeRobotEnvComponent::LogDriveConstraintStates() const {} // 省略实现

// ---------------------------------------------------------------------------
// ApplyAction
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::ApplyAction(const TArray<float>& Action)
{
    if (!GetOwner()) return;

    if (JointActionAxes.Num() != DriveConstraints.Num() || JointAxisCaches.Num() != DriveConstraints.Num())
        CacheJointActionAxes();

    const int32 ExpectedDim = GetActionDim();
    if (Action.Num() != ExpectedDim)
    {
        UE_LOG(LogTemp, Warning, TEXT("ApplyAction length mismatch: got %d, expected %d"), Action.Num(), ExpectedDim);
    }

    TArray<float> CurrentAction;
    CurrentAction.Init(0.0f, ExpectedDim);

<<<<<<< HEAD
    // L2-norm clamp: scale down all actions proportionally when the policy
    // tries to drive all joints at max speed simultaneously. Prevents Chaos
    // solver from spending excessive sub-iterations per physics step.
    TArray<float> ClampedRawAction = Action;
    {
        float ActionNormSq = 0.0f;
        for (float V : Action) ActionNormSq += V * V;
        const float ActionNorm = FMath::Sqrt(ActionNormSq);
        const float MaxActionNorm = 2.0f;  // ~0.63 per dim for 10-DoF
        if (ActionNorm > MaxActionNorm)
        {
            const float Scale = MaxActionNorm / ActionNorm;
            for (float& V : ClampedRawAction) V *= Scale;
        }
=======
    int32 ActionIndex = 0;
    for (int32 JointIdx = 0; JointIdx < DriveConstraints.Num(); ++JointIdx)
    {
        UPhysicsConstraintComponent* Con = DriveConstraints[JointIdx];
        if (!Con || !JointAxisCaches.IsValidIndex(JointIdx)) continue;

        const FOrangeRobotConstraintAxisCache& Cache = JointAxisCaches[JointIdx];
        FVector TargetVel = FVector::ZeroVector;

        auto ConsumeAction = [&](double& OutVel, float Limit)
        {
            if (!CurrentAction.IsValidIndex(ActionIndex)) { ++ActionIndex; return; }
            float Val = Action.IsValidIndex(ActionIndex) ? FMath::Clamp(Action[ActionIndex], -1.0f, 1.0f) : 0.0f;
            if (FMath::Abs(Val) < ActionDeadzone) Val = 0.0f;
            else Val = ShapeNormalizedAction(Val, ActionResponseExponent);
            CurrentAction[ActionIndex] = Val;
            OutVel = SanitizeFiniteScalar(Val * JointVelocityScale, -Limit, Limit);
            ++ActionIndex;
        };

        if (Cache.bUseTwist)  ConsumeAction(TargetVel.X, TwistVelocityLimit);
        if (Cache.bUseSwing1) ConsumeAction(TargetVel.Y, SwingVelocityLimit);
        if (Cache.bUseSwing2) ConsumeAction(TargetVel.Z, SwingVelocityLimit);

        Con->SetAngularVelocityTarget(ClampAngularVelocityTarget(TargetVel));
    }

    PreviousAction = LastAction;
    LastAction = MoveTemp(CurrentAction);
    CurrentStep++;

    if (bEnableCurriculum)
    {
        ++GlobalTrainingStep;
        UpdateCurriculumWeightsAndCommand();
>>>>>>> worktree-training-stability-fixes
    }

    int32 ActionIndex = 0;
    for (int32 JointIdx = 0; JointIdx < DriveConstraints.Num(); ++JointIdx)
    {
        UPhysicsConstraintComponent* Con = DriveConstraints[JointIdx];
        if (!Con || !JointAxisCaches.IsValidIndex(JointIdx)) continue;

        const FOrangeRobotConstraintAxisCache& Cache = JointAxisCaches[JointIdx];
        FVector TargetVel = FVector::ZeroVector;

        auto ConsumeAction = [&](double& OutVel, float Limit)
        {
            if (!CurrentAction.IsValidIndex(ActionIndex)) { ++ActionIndex; return; }
            float Val = ClampedRawAction.IsValidIndex(ActionIndex) ? FMath::Clamp(ClampedRawAction[ActionIndex], -1.0f, 1.0f) : 0.0f;
            if (FMath::Abs(Val) < ActionDeadzone) Val = 0.0f;
            else Val = ShapeNormalizedAction(Val, ActionResponseExponent);
            CurrentAction[ActionIndex] = Val;
            OutVel = SanitizeFiniteScalar(Val * JointVelocityScale, -Limit, Limit);
            ++ActionIndex;
        };

        if (Cache.bUseTwist)  ConsumeAction(TargetVel.X, TwistVelocityLimit);
        if (Cache.bUseSwing1) ConsumeAction(TargetVel.Y, SwingVelocityLimit);
        if (Cache.bUseSwing2) ConsumeAction(TargetVel.Z, SwingVelocityLimit);

        Con->SetAngularVelocityTarget(ClampAngularVelocityTarget(TargetVel));
    }

    PreviousAction = LastAction;
    LastAction = MoveTemp(CurrentAction);
    CurrentStep++;

    if (bEnableCurriculum)
    {
        ++GlobalTrainingStep;
        UpdateCurriculumWeightsAndCommand();
    }
}

// ---------------------------------------------------------------------------
// 收集观测
// ---------------------------------------------------------------------------

TArray<float> UOrangeRobotEnvComponent::CollectLowLevelObservations() const
{
    TArray<float> Obs;
    Obs.Reserve(GetLowLevelObservationDim());

    UPrimitiveComponent* TrunkComp = GetTrunkPrimitive();
    const FVector Forward = GetRobotForwardVector(TrunkComp);
    const FVector Right   = GetRobotRightVector(TrunkComp);
    const FVector Up      = TrunkComp ? TrunkComp->GetUpVector() : FVector::UpVector;
    const FVector WorldVel = TrunkComp
        ? SanitizeFiniteVector(TrunkComp->GetComponentVelocity(), -5000.0f, 5000.0f)
        : FVector::ZeroVector;

    const float TrunkHeight = GetBodyHeight();
    Obs.Add(FMath::Clamp(TrunkHeight / FMath::Max(TrunkHeightNormalization, 1.0f), 0.0f, 2.0f));
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(WorldVel, Forward) / 200.0f, -5.0f, 5.0f));
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(WorldVel, Right) / 200.0f, -5.0f, 5.0f));
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(WorldVel, Up) / 200.0f, -5.0f, 5.0f));

    FVector LocalAngVel = FVector::ZeroVector;
    if (TrunkComp)
    {
        const FVector WorldAngVel = SanitizeFiniteVector(TrunkComp->GetPhysicsAngularVelocityInDegrees(), -540.0f, 540.0f);
        LocalAngVel.X = FVector::DotProduct(WorldAngVel, Forward);
        LocalAngVel.Y = FVector::DotProduct(WorldAngVel, Right);
        LocalAngVel.Z = FVector::DotProduct(WorldAngVel, Up);
    }
    Obs.Add(SanitizeFiniteScalar(LocalAngVel.X / 180.0f, -3.0f, 3.0f));
    Obs.Add(SanitizeFiniteScalar(LocalAngVel.Y / 180.0f, -3.0f, 3.0f));
    Obs.Add(SanitizeFiniteScalar(LocalAngVel.Z / 180.0f, -3.0f, 3.0f));

    const USceneComponent* TiltComp = GetTiltReferenceComponent();
    const FVector TiltUp = TiltComp ? TiltComp->GetUpVector() : FVector::UpVector;
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(TiltUp, FVector::UpVector), -1.0f, 1.0f));

    auto AddFootObs = [&](const UPrimitiveComponent* Foot, float InitialDist)
    {
        float Contact = 0.0f, RelHeight = 0.0f, SlideSpeed = 0.0f;
        if (Foot && TiltCheckComponent)
        {
            const float CurrentDist = TiltCheckComponent->GetComponentLocation().Z - Foot->GetComponentLocation().Z;
            const float Lift = InitialDist - CurrentDist;
            Contact = 1.0f - FMath::Clamp(Lift / FMath::Max(InitialDist * 0.5f, 1.0f), 0.0f, 1.0f);
            RelHeight = FMath::Clamp(CurrentDist / FMath::Max(InitialDist, 1.0f), 0.0f, 2.0f);
            SlideSpeed = FMath::Clamp(GetFootHorizontalSpeed(Foot) / FMath::Max(FootStableSpeedThreshold, 1.0f), 0.0f, 3.0f);
        }
        Obs.Add(Contact);
        Obs.Add(RelHeight);
        Obs.Add(SlideSpeed);
    };
    AddFootObs(FootL, InitialLeftFootDistance);
    AddFootObs(FootR, InitialRightFootDistance);

    // 关节
    for (int32 i = 0; i < DriveConstraints.Num(); ++i)
    {
        if (!JointAxisCaches.IsValidIndex(i)) continue;
        const FOrangeRobotConstraintAxisCache& Cache = JointAxisCaches[i];

        float TwistAngle = 0.0f, Swing1Angle = 0.0f, Swing2Angle = 0.0f;
        if (DriveConstraints[i])
        {
            TwistAngle  = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentTwist(),  -180.0f, 180.0f) / 180.0f;
            Swing1Angle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentSwing1(), -180.0f, 180.0f) / 180.0f;
            Swing2Angle = SanitizeFiniteScalar(DriveConstraints[i]->GetCurrentSwing2(), -180.0f, 180.0f) / 180.0f;
        }

        FVector AngVel = FVector::ZeroVector;
        if (BodyLinks.IsValidIndex(i) && BodyLinks[i])
        {
            AngVel = SanitizeFiniteVector(BodyLinks[i]->GetPhysicsAngularVelocityInDegrees(), -360.0f, 360.0f)
                     / FMath::Max(JointVelocityScale, 1.0f);
        }

        if (Cache.bUseTwist)
        {
            Obs.Add(TwistAngle);
            Obs.Add(SanitizeFiniteScalar(AngVel.X, -3.0f, 3.0f));
        }
        if (Cache.bUseSwing1)
        {
            Obs.Add(Swing1Angle);
            Obs.Add(SanitizeFiniteScalar(AngVel.Y, -3.0f, 3.0f));
        }
        if (Cache.bUseSwing2)
        {
            Obs.Add(Swing2Angle);
            Obs.Add(SanitizeFiniteScalar(AngVel.Z, -3.0f, 3.0f));
        }
    }

    // 步态相位
    Obs.Add(FMath::Sin(GaitPhase));
    Obs.Add(FMath::Cos(GaitPhase));

    return Obs;
}

TArray<float> UOrangeRobotEnvComponent::CollectObservations() const
{
    TArray<float> Obs = CollectLowLevelObservations();
    if (bEnableCommandReward)
    {
        Obs.Add(SanitizeFiniteScalar(HighLevelCommand.X, -1.0f, 1.0f));
        Obs.Add(SanitizeFiniteScalar(HighLevelCommand.Y, -1.0f, 1.0f));
    }
    return Obs;
}
// ---------------------------------------------------------------------------
// ComputeReward
// ---------------------------------------------------------------------------

float UOrangeRobotEnvComponent::ComputeReward()
{
    // =========================================================
    // 0) 调试打印：观察权重与指令（每100帧一次防刷屏）
    // =========================================================
    static int Counter = 0;
    if (++Counter % 100 == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Current Weights -  Dynamic: %f, Cmd: (%f, %f)"),
        DynamicBalanceRewardWeight, HighLevelCommand.X, HighLevelCommand.Y);
    }
    // //临时验证
    // RewardMaskCore = static_cast<int32>(ERewardGroupCore::Alive); // 1
    // RewardMaskGait = 0;
    // RewardMaskReg  = 0;
    
    // =========================================================
    // 1) 状态/观测量提取（reward 的“输入”）
    // =========================================================
    UPrimitiveComponent* TrunkComp = GetTrunkPrimitive();

    const FVector Velocity      = TrunkComp ? TrunkComp->GetComponentVelocity() : FVector::ZeroVector;
    const FVector LocalPlanarVel = GetLocalRobotPlanarVelocity(TrunkComp, Velocity);
    const float ForwardSpeed     = LocalPlanarVel.X;
    const float LateralSpeed     = FMath::Abs(LocalPlanarVel.Y);

    float TrunkAngVelZ = 0.0f;
    if (TrunkComp)
        TrunkAngVelZ = SanitizeFiniteAngleDegrees(TrunkComp->GetPhysicsAngularVelocityInDegrees().Z);

    const float UprightDot   = GetUprightDot();    // 直立程度（越接近1越直立）
    const float BodyHeight   = GetBodyHeight();    // 躯干高度

    // 支撑/步态判定
    const bool bLStable      = IsFootStableSupport(FootL);
    const bool bRStable      = IsFootStableSupport(FootR);
    const bool bHasSupport   = bLStable || bRStable;
    const bool bDoubleSupport= bLStable && bRStable;
    const bool bSingleSupport= bLStable != bRStable;

    // 触地/滑动信息（用于打滑、冲击等惩罚）
    const bool  bLTouch      = IsFootTouchingGround(FootL);
    const bool  bRTouch      = IsFootTouchingGround(FootR);
    const float LSlide       = GetFootHorizontalSpeed(FootL);
    const float RSlide       = GetFootHorizontalSpeed(FootR);

    // 高层指令：前进 & 转向（归一化到[-1,1]）
    const float CmdFwd  = SanitizeFiniteScalar(HighLevelCommand.X, -1.0f, 1.0f);
    const float CmdTurn = SanitizeFiniteScalar(HighLevelCommand.Y, -1.0f, 1.0f);

    // 指令模式：站立 or 行走（站立：几乎无前进&转向）
    const bool bCommandedStand =
        bEnableCommandReward && FMath::Abs(CmdFwd) < 0.05f && FMath::Abs(CmdTurn) < 0.05f;

    const bool bFallen = CheckFallen();

    // =========================================================
    // 2) 初始化奖励分量（便于日志与消融）
    // =========================================================
    FOrangeRobotRewardComponents Components;

    // =========================================================
    // 3) Reward blocks (gated by CORE/GAIT/REG masks)
    // =========================================================

    // --------------------------
    // 3.1 Alive（CORE / 正奖励）
    // --------------------------
    APPLY_REWARD_CORE(ERewardGroupCore::Alive, {
<<<<<<< HEAD
    if (UprightDot < TiltQualityGate)
    {
        // 倾斜超过门控阈值：当帧不给存活奖励
        Components.Alive = 0.0f;
    }
    else
    {
        float StandStability = 0.0f;

        if (bHasSupport && !bFallen)
        {
            // 1) 直立质量：越直立越接近 1.0
            const float TiltQuality = FMath::Clamp(UprightDot, 0.0f, 1.0f);

            // 2) 垂直颠簸惩罚：上下抖动越厉害越接近 0
            float VerticalSpeedCm = 0.0f;
            if (TrunkComp)
            {
                VerticalSpeedCm = FMath::Abs(
                    FVector::DotProduct(TrunkComp->GetComponentVelocity(), FVector::UpVector));
            }
            const float VelQuality = FMath::Clamp(
                1.0f - (VerticalSpeedCm / 20.0f), 0.0f, 1.0f);

            // 3) 综合稳定性（两项同时好才给高奖励）
            StandStability = TiltQuality * VelQuality;
        }

        // // 范围：[AliveReward, 2 * AliveReward]
        Components.Alive = AliveReward * (1.0f + StandStability);
    }
=======
    float StandStability = 0.0f;
    
    if (bHasSupport && !bFallen)
    {
        // 1) 直立质量：越直立越接近 1.0
        const float TiltQuality = FMath::Clamp(UprightDot, 0.0f, 1.0f);
        
        // 2) 垂直颠簸惩罚：上下抖动越厉害越接近 0
        float VerticalSpeedCm = 0.0f;
        if (TrunkComp)
        {
            VerticalSpeedCm = FMath::Abs(
                FVector::DotProduct(TrunkComp->GetComponentVelocity(), FVector::UpVector));
        }
        const float VelQuality = FMath::Clamp(
            1.0f - (VerticalSpeedCm / 20.0f), 0.0f, 1.0f);
        
        // 3) 综合稳定性（两项同时好才给高奖励）
        StandStability = TiltQuality * VelQuality;
    }
        
        // 范围：[0.5 * AliveReward, 1.5 * AliveReward]
    // Components.Alive = AliveReward * FMath::Lerp(0.5f, 1.5f, StandStability);
    
    // // 基线奖励 + 稳定性加成
    // // 范围：[AliveReward, 2 * AliveReward]
    Components.Alive = AliveReward * (1.0f + StandStability);
        
>>>>>>> worktree-training-stability-fixes
});

    // // --------------------------
    // // 3.2 Upright（CORE / 负奖励：不直立）
    // // --------------------------
    // APPLY_REWARD_CORE(ERewardGroupCore::Upright, {
    //     const float UprightError = FMath::Clamp(1.0f - UprightDot, 0.0f, 2.0f);
    //     Components.Upright = -UprightError * UprightRewardScale;
    // });

    // --------------------------
    // 3.3 Height（CORE / 混合）
    // --------------------------
    APPLY_REWARD_CORE(ERewardGroupCore::Height, {
        const float HeightRatio = FMath::Clamp(BodyHeight / FMath::Max(BodyHeightRewardMax, 1.0f), 0.0f, 1.5f);

        if (HeightRatio >= 0.9f)
        {
            Components.Height = HeightRewardScale;
        }
        else if (HeightRatio >= 0.85f)
        {
            Components.Height = FMath::Lerp(
                -FMath::Square(1.0f - 0.85f) * HeightDropPenaltyScale,
                HeightRewardScale,
                (HeightRatio - 0.85f) / 0.10f
            );
        }
        else
        {
            Components.Height = -FMath::Square(1.0f - HeightRatio) * HeightDropPenaltyScale;
        }
    });

    // --------------------------
    // 3.4 Lateral velocity（CORE / 负奖励）
    // --------------------------
    APPLY_REWARD_CORE(ERewardGroupCore::LateralPenalty, {
        Components.LateralPenalty = -LateralSpeed * LateralVelocityPenaltyScale;
    });
    
    // --------------------------
    // 3.5 Stable Double Support（CORE / 正奖励，独立于 DynamicBalanceRewardWeight）
    // --------------------------
    APPLY_REWARD_CORE(ERewardGroupCore::StableDoubleSupport, {
        if (bDoubleSupport)  // bDoubleSupport 已在函数开头计算：bLStable && bRStable
        {
            Components.StableDoubleSupport = StableDoubleSupportRewardScale;
        }
    });
    
    
    // --------------------------
    // 3.6 Command tracking（CORE / 正奖励：跟踪高层指令）
    // --------------------------
    // 使用指数核，静止与非静止指令统一处理
    if (bEnableCommandReward && CommandRewardScale > 0.0f)
    {
        // 将命令从归一化转为实际目标速度
        const float TargetFwd  = CmdFwd  * MaxForwardSpeed;
        const float TargetTurn = CmdTurn * MaxTurnSpeedDegPerSec;

        // 动态标准差：命令越大，容许匹配误差越大（保持灵敏度）
        float FwdSigma  = FMath::Max(ForwardCommandMatchSigmaMin,
                                           MaxForwardSpeed * FMath::Lerp(0.18f, 0.50f, FMath::Abs(CmdFwd)));
        float TurnSigma = FMath::Max(TurnCommandMatchSigmaMin,
                                           MaxTurnSpeedDegPerSec * FMath::Lerp(0.20f, 0.50f, FMath::Abs(CmdTurn)));

        //站立指令时使用极小 sigma，强制精确跟踪零速度
        if (bCommandedStand)
        {
            FwdSigma  = FMath::Min(FwdSigma,  StandCommandSigma);
            TurnSigma = FMath::Min(TurnSigma, StandCommandSigma);
        }
        
        // 误差平方
        const float FwdErrorSq  = FMath::Square(ForwardSpeed - TargetFwd);
        const float TurnErrorSq = FMath::Square(TrunkAngVelZ - TargetTurn);

        // 指数核奖励
        const float FwdMatch  = FMath::Exp(-FwdErrorSq  / (2.0f * FwdSigma * FwdSigma));
        const float TurnMatch = FMath::Exp(-TurnErrorSq / (2.0f * TurnSigma * TurnSigma));

        // 加权组合
        const float WeightedMatch = FwdMatch * ForwardCommandRewardWeight +
                                    TurnMatch * TurnCommandRewardWeight;

        APPLY_REWARD_CORE(ERewardGroupCore::CommandTracking, {
            Components.CommandTracking = WeightedMatch * CommandMatchBaseReward * CommandRewardScale;
        });
    }

    // --------------------------
    // 3.7 Dynamic balance & gait（GAIT / 混合）
    // --------------------------
    const float ClampedDynamicBalanceWeight = FMath::Clamp(DynamicBalanceRewardWeight, 0.0f, 1.0f);
    if (ClampedDynamicBalanceWeight > 0.0f)
    {
        float SupportReward = 0.0f;
        float GaitReward    = 0.0f;

<<<<<<< HEAD
        // --- 膝关节伸展惩罚：右膝[5] 左膝[11]，Swing1=轴1，弯曲越深惩罚越重
        {
            const int32 RightKneeIdx = 5;
            const int32 LeftKneeIdx  = 11;
            const int32 KneeAxis = 1;  // Swing1

            float RightKneeAngle = FMath::Max(0.0f,
                GetJointAngle(RightKneeIdx, KneeAxis));
            float LeftKneeAngle = FMath::Max(0.0f,
                GetJointAngle(LeftKneeIdx, KneeAxis));

            const float RightPenalty = FMath::Square(RightKneeAngle) * KneeExtensionPenaltyScale;
            const float LeftPenalty  = FMath::Square(LeftKneeAngle)  * KneeExtensionPenaltyScale;

            Components.KneeExtensionPenalty = -(RightPenalty + LeftPenalty) * ClampedDynamicBalanceWeight;
        }

        // --- 步态质量负项：双脚拖地/乱蹭（平方惩罚）
        GaitReward -= (LSlide * LSlide + RSlide * RSlide) * DualFootShufflePenaltyScale * 0.1f;

        // --- 步态交替 + 步频
        if (bSingleSupport)
        {
=======
        // --- 步态质量负项：双脚拖地/乱蹭（平方惩罚）
        GaitReward -= (LSlide * LSlide + RSlide * RSlide) * DualFootShufflePenaltyScale * 0.1f;

        // --- 步态交替 + 步频
        if (bSingleSupport)
        {
>>>>>>> worktree-training-stability-fixes
            const ESupportSide CurrentSingleSide = bLStable ? ESupportSide::Left : ESupportSide::Right;

            if (LastSingleSupportSide != ESupportSide::None && LastSingleSupportSide != CurrentSingleSide)
            {
                // 正：交替成功
                APPLY_REWARD_GAIT(ERewardGroupGait::StepAlternation, {
                    Components.StepAlternation = StepAlternationRewardScale;
                });

                SameLegConsecutiveSteps = 0;

                if (LastStepTransitionStep >= 0)
                {
                    const int32 StepDuration = FMath::Max(CurrentStep - LastStepTransitionStep, 1);
                    const float StepFrequency = SimulationFrequencyHz / static_cast<float>(StepDuration);

                    APPLY_REWARD_GAIT(ERewardGroupGait::StepFrequency, {
                        if (StepFrequency >= MinStepFrequencyHz && StepFrequency <= MaxStepFrequencyHz)
                        {
                            Components.StepFrequencyReward = StepFrequencyRewardScale;
                        }
                        else
                        {
                            const float FrequencyError = StepFrequency < MinStepFrequencyHz
                                ? (MinStepFrequencyHz - StepFrequency)
                                : (StepFrequency - MaxStepFrequencyHz);

                            Components.StepFrequencyReward =
                                -FMath::Min(FrequencyError * StepFrequencyRewardScale, StepFrequencyRewardScale);
                        }

                        Components.StepFrequencyReward *= ClampedDynamicBalanceWeight;
                    });
                }

                LastStepTransitionStep = CurrentStep;
            }
            else if (LastSingleSupportSide == CurrentSingleSide)
            {
                SameLegConsecutiveSteps++;

                APPLY_REWARD_GAIT(ERewardGroupGait::StepAlternation, {
                    Components.StepAlternation -= FMath::Min(
                        SameLegConsecutiveSteps * 0.001f,
                        SameLegDominancePenaltyScale
                    );
                });
            }

            LastSingleSupportSide = CurrentSingleSide;
        }

        // --- 单支撑细化：抬脚/早触地/支撑偏移
        if (bSingleSupport)
        {
            const UPrimitiveComponent* SwingFoot = bLStable ? FootR : FootL;
            const bool bSwingFootTouching = (SwingFoot == FootL) ? bLTouch : bRTouch;

            const float SwingInitDist = (SwingFoot == FootL) ? InitialLeftFootDistance : InitialRightFootDistance;
            const float SafeSwingInitDist = FMath::Max(SwingInitDist, 1.0f);

            const float CurDist     = GetFootDistanceFromTrunk(SwingFoot);
            const float ActualLift  = FMath::Max(0.0f, SwingInitDist - CurDist);
            const float LiftDeficit = FMath::Max(0.0f, SwingFootMinHeight - ActualLift);

            GaitReward -= LiftDeficit * LiftDeficit * SwingFootHeightPenaltyScale;

            if (bSwingFootTouching)
            {
                const float SwingFootContact =
                    1.0f - FMath::Clamp((SwingInitDist - CurDist) / (SafeSwingInitDist * 0.5f), 0.0f, 1.0f);
                if (SwingFootContact > 0.5f)
                {
                    GaitReward -= SwingFootContact * 0.05f;
                }
            }

            const float FwdBonus = FMath::Clamp(ForwardSpeed / 50.0f, 0.1f, 1.0f);
            if (!bCommandedStand && UprightDot > 0.95f && ActualLift >= SwingFootMinHeight)
            {
                GaitReward += SingleSupportBonusReward * FwdBonus;
            }

            SupportReward -= GetTrunkSupportOffsetNormalized(bLStable, bRStable) * TrunkSupportOffsetPenaltyScale;
        }

        // 汇总写入点：分别 gate（GAIT）
        APPLY_REWARD_GAIT(ERewardGroupGait::SupportStability, {
            Components.SupportStability = SupportReward * ClampedDynamicBalanceWeight;
        });

        APPLY_REWARD_GAIT(ERewardGroupGait::GaitQuality, {
            Components.GaitQuality = GaitReward * ClampedDynamicBalanceWeight;
        });

        APPLY_REWARD_GAIT(ERewardGroupGait::StepAlternation, {
            Components.StepAlternation *= ClampedDynamicBalanceWeight;
        });
    }

    // --------------------------
    // 3.8 Energy penalty（REG / 负）
    // --------------------------
    if (LastAction.Num() == GetActionDim())
    {
        float ActionMagSq = 0.0f;
        for (float Val : LastAction) ActionMagSq += Val * Val;

        const float ActionMagSqPerDim = ActionMagSq / FMath::Max(GetActionDim(), 1);
        const float SafeSigma = FMath::Max(EnergySigma, 0.01f);

        APPLY_REWARD_REG(ERewardGroupReg::Energy, {
            Components.EnergyPenalty =
                -ActionMagnitudePenaltyScale *
                (1.0f - FMath::Exp(-ActionMagSqPerDim / (2.0f * FMath::Square(SafeSigma))));
        });
    }

    // --------------------------
    // 3.9 Action smooth（REG / 负）
    // --------------------------
    if (LastAction.Num() == GetActionDim() && PreviousAction.Num() == LastAction.Num())
    {
        float SmoothPen = 0.0f;
        for (int32 i = 0; i < LastAction.Num(); ++i)
        {
            const float Delta = LastAction[i] - PreviousAction[i];
            SmoothPen += Delta * Delta;
        }

        APPLY_REWARD_REG(ERewardGroupReg::ActionSmooth, {
            Components.ActionSmooth = -SmoothPen * ActionSmoothPenaltyScale;
        });
    }

    // --------------------------
    // 3.10 Trunk pitch stability（CORE / 负）
    // --------------------------
    {
        // --- 倾斜惩罚（平方型）---
        const float UprightError = FMath::Clamp(1.0f - UprightDot, 0.0f, 2.0f);
        // 倾斜惩罚上限
        const float TiltPenaltyMax = 1.0f;
        const float TiltPenalty = FMath::Min(FMath::Square(UprightError) * TrunkTiltPenaltyScale, TiltPenaltyMax);

        // --- 角速度惩罚（L2）---
        float AngVelXYPenalty = 0.0f;
        const float AngVelPenaltyMax = 0.5f;
        if (TrunkComp)
        {
            const FVector WorldAngVel = SanitizeFiniteVector(
                TrunkComp->GetPhysicsAngularVelocityInDegrees(), -540.0f, 540.0f);
            const FVector Forward = GetRobotForwardVector(TrunkComp);
            const FVector Right   = GetRobotRightVector(TrunkComp);
            const float LocalAngVelX = FVector::DotProduct(WorldAngVel, Forward);
            const float LocalAngVelY = FVector::DotProduct(WorldAngVel, Right);
            // AngVelXYPenalty = (FMath::Square(LocalAngVelX) + FMath::Square(LocalAngVelY))
            //                  * TrunkAngVelXYPenaltyScale;
            
            AngVelXYPenalty = FMath::Min( (FMath::Square(LocalAngVelX) + FMath::Square(LocalAngVelY))
                             * TrunkAngVelXYPenaltyScale, AngVelPenaltyMax);
        }

        // --- 垂直速度惩罚（死区 + 平方，单位 cm/s）---
        float VerticalVelPenalty = 0.0f;
        // 垂直速度惩罚上限
        const float VertVelPenaltyMax = 0.1f;
        if (TrunkComp)
        {
            const float VerticalSpeedCm = FMath::Abs(
                FVector::DotProduct(TrunkComp->GetComponentVelocity(), FVector::UpVector));
            const float ExcessCm = FMath::Max(0.0f, VerticalSpeedCm - TrunkVerticalVelocityDeadzone);
            VerticalVelPenalty = FMath::Min(FMath::Square(ExcessCm) * TrunkVerticalVelocityPenaltyScale,VertVelPenaltyMax);
        }

<<<<<<< HEAD
        // --- 倾倒预警：倾斜角度超过倒地阈值50%时开始预警 ---
        float TiltWarningVal = 0.0f;
        {
            const float TiltAngleRad = FMath::Acos(FMath::Clamp(UprightDot, -1.0f, 1.0f));
            const float TiltAngleDeg = FMath::RadiansToDegrees(TiltAngleRad);
            const float WarningThresholdDeg = FallTiltThreshold * 0.5f;

            if (TiltAngleDeg > WarningThresholdDeg)
            {
                const float Excess = (TiltAngleDeg - WarningThresholdDeg)
                                   / FMath::Max(FallTiltThreshold - WarningThresholdDeg, 1.0f);
                TiltWarningVal = -TiltWarningScale * FMath::Square(Excess);
            }
        }

        APPLY_REWARD_CORE(ERewardGroupCore::TrunkStability, {
            Components.TrunkStabilityPenalty = -(TiltPenalty + AngVelXYPenalty + VerticalVelPenalty);
            Components.TiltWarning = TiltWarningVal;
=======
        APPLY_REWARD_CORE(ERewardGroupCore::TrunkStability, {
            Components.TrunkStabilityPenalty = -(TiltPenalty + AngVelXYPenalty + VerticalVelPenalty);
>>>>>>> worktree-training-stability-fixes
        });
    }

    // --------------------------
    // 3.11 Foot impact（GAIT / 负）
    // --------------------------
    {
        float ImpactPenalty = 0.0f;

        auto CheckImpact = [&](const UStaticMeshComponent* Foot, bool bTouchNow, bool bTouchWas)
        {
            if (bTouchNow && !bTouchWas && Foot)
            {
                const float DownVel = -Foot->GetComponentVelocity().Z;
                if (DownVel > FootImpactVelocityThreshold)
                {
                    const float Excess = DownVel - FootImpactVelocityThreshold;
                    ImpactPenalty += Excess * Excess;
                }
            }
        };

        CheckImpact(FootL, bLTouch, bLTouchPrev);
        CheckImpact(FootR, bRTouch, bRTouchPrev);

        APPLY_REWARD_GAIT(ERewardGroupGait::FootImpact, {
            Components.FootImpactPenalty = -ImpactPenalty * FootImpactPenaltyScale * ClampedDynamicBalanceWeight;
        });

        bLTouchPrev = bLTouch;
        bRTouchPrev = bRTouch;
    }

    // --------------------------
    // 3.12 Symmetry（GAIT / 负）
    // --------------------------
    if (ClampedDynamicBalanceWeight > 0.0f && GaitSymmetryPenaltyScale > 0.0f)
    {
        const int32 PairCount = FMath::Min(LeftLegJointIndices.Num(), RightLegJointIndices.Num());
        if (PairCount > 0)
        {
            float SymDiff = 0.0f;

            for (int32 PairIdx = 0; PairIdx < PairCount; ++PairIdx)
            {
                const int32 LIdx = LeftLegJointIndices[PairIdx];
                const int32 RIdx = RightLegJointIndices[PairIdx];
                if (!JointAxisCaches.IsValidIndex(LIdx) || !JointAxisCaches.IsValidIndex(RIdx))
                {
                    continue;
                }

                const FOrangeRobotConstraintAxisCache& LC = JointAxisCaches[LIdx];
                const FOrangeRobotConstraintAxisCache& RC = JointAxisCaches[RIdx];

                if (LC.bUseTwist && RC.bUseTwist)
                {
                    const float D = GetJointAngle(LIdx, 0) - GetJointAngle(RIdx, 0);
                    SymDiff += D * D;
                }
                if (LC.bUseSwing1 && RC.bUseSwing1)
                {
                    const float D = GetJointAngle(LIdx, 1) - GetJointAngle(RIdx, 1);
                    SymDiff += D * D;
                }
                if (LC.bUseSwing2 && RC.bUseSwing2)
                {
                    const float D = GetJointAngle(LIdx, 2) - GetJointAngle(RIdx, 2);
                    SymDiff += D * D;
                }
            }

            APPLY_REWARD_GAIT(ERewardGroupGait::Symmetry, {
                Components.SymmetryPenalty = -SymDiff * GaitSymmetryPenaltyScale * ClampedDynamicBalanceWeight;
            });
        }
    }

    // --------------------------
    // 3.13 Cost of Transport（REG / 负）
    // --------------------------
    if (ClampedDynamicBalanceWeight > 0.5f && !bCommandedStand && FMath::Abs(ForwardSpeed) > 10.0f && CostOfTransportScale > 0.0f)
    {
        if (LastAction.Num() == GetActionDim())
        {
            float ActionMagSqCoT = 0.0f;
            for (float Val : LastAction) ActionMagSqCoT += Val * Val;

            const float ActionMagSqPerDimCoT = ActionMagSqCoT / FMath::Max(GetActionDim(), 1);
            const float SpeedNorm = FMath::Abs(ForwardSpeed) / FMath::Max(ForwardSpeedRewardMax, 1.0f);

            const float CoT = ActionMagSqPerDimCoT / FMath::Max(SpeedNorm, 0.1f);

            APPLY_REWARD_REG(ERewardGroupReg::CostOfTransport, {
                Components.CostOfTransportPenalty = -FMath::Min(CoT * CostOfTransportScale, 0.05f);
            });
        }
    }

    // --------------------------
    // 3.14 Fall terminal（REG / 负）
    // --------------------------
    if (bFallen)
    {
        const int32 RemainingSteps = FMath::Max(0, MaxSteps - CurrentStep);
        const int32 PenaltySteps   = FMath::Min(RemainingSteps, FMath::Max(FallPenaltyHorizon, 1));

<<<<<<< HEAD
        // Curriculum-dependent fall penalty scaling
=======
        // Stage-dependent scaling: full penalty only after stage 1 (standing established)
        // Stage 0: 20% of full penalty — don't punish falls during standing learning
        // Stage 1+: linearly ramp to full penalty by stage 2
>>>>>>> worktree-training-stability-fixes
        const float FallScale = (CurrentCurriculumStage == 0) ? 0.2f
                              : (CurrentCurriculumStage == 1) ? 0.5f
                              : 1.0f;

        APPLY_REWARD_REG(ERewardGroupReg::FallTerminal, {
            Components.FallTerminal = -FMath::Abs(AliveReward) * PenaltySteps * FallScale;
        });
    }

    // =========================================================
    // 4) Total 汇总（建议：这里保持“纯加和”，便于消融对比）
    // =========================================================
    Components.Total =
          Components.Alive
        // + Components.Upright
        + Components.Height
        + Components.LateralPenalty
        // + Components.Stand
        + Components.StableDoubleSupport
        + Components.SupportStability
        + Components.GaitQuality
<<<<<<< HEAD
        + Components.KneeExtensionPenalty
=======
>>>>>>> worktree-training-stability-fixes
        + Components.CommandTracking
        + Components.ActionSmooth
        + Components.EnergyPenalty
        + Components.StepAlternation
        + Components.TrunkStabilityPenalty
<<<<<<< HEAD
        + Components.TiltWarning
=======
>>>>>>> worktree-training-stability-fixes
        + Components.FootImpactPenalty
        + Components.SymmetryPenalty
        + Components.StepFrequencyReward
        + Components.CostOfTransportPenalty
        + Components.FallTerminal;

    LastRewardComponents = Components;
    
    if (bLogRewardBreakdown && CurrentStep > 0 && (CurrentStep % 10) == 0)
    {
        UE_LOG(LogTemp, Log,
<<<<<<< HEAD
    TEXT("RewardBreakdown Step=%d Core=0x%08X Gait=0x%08X Reg=0x%08X Total=%.4f | Alive=%.4f Height=%.4f Lat=%.4f StableDS=%.4f Support=%.4f Gait=%.4f Knee=%.4f Cmd=%.4f Smooth=%.4f Energy=%.4f StepAlt=%.4f TrunkSP=%.4f TiltWarn=%.4f Impact=%.4f Sym=%.4f StepFreq=%.4f CoT=%.4f Fall=%.4f"),
    CurrentStep, RewardMaskCore, RewardMaskGait, RewardMaskReg, Components.Total,
    Components.Alive, Components.Height, Components.LateralPenalty,
    Components.StableDoubleSupport,
    Components.SupportStability, Components.GaitQuality, Components.KneeExtensionPenalty,
    Components.CommandTracking, Components.ActionSmooth,
    Components.EnergyPenalty, Components.StepAlternation, Components.TrunkStabilityPenalty,
    Components.TiltWarning,
=======
    TEXT("RewardBreakdown Step=%d Core=0x%08X Gait=0x%08X Reg=0x%08X Total=%.4f | Alive=%.4f Height=%.4f Lat=%.4f StableDS=%.4f Support=%.4f Gait=%.4f Cmd=%.4f Smooth=%.4f Energy=%.4f StepAlt=%.4f TrunkSP=%.4f Impact=%.4f Sym=%.4f StepFreq=%.4f CoT=%.4f Fall=%.4f"),
    CurrentStep, RewardMaskCore, RewardMaskGait, RewardMaskReg, Components.Total,
    Components.Alive, Components.Height, Components.LateralPenalty,
    Components.StableDoubleSupport,
    Components.SupportStability, Components.GaitQuality, Components.CommandTracking, Components.ActionSmooth,
    Components.EnergyPenalty, Components.StepAlternation, Components.TrunkStabilityPenalty,
>>>>>>> worktree-training-stability-fixes
    Components.FootImpactPenalty, Components.SymmetryPenalty, Components.StepFrequencyReward,
    Components.CostOfTransportPenalty, Components.FallTerminal);
    }
    
    static int SpeedLogCounter = 0;
    if (++SpeedLogCounter % 10 == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("SpeedDebug Fwd=%.1f Lat=%.1f AngZ=%.1f | LFootSpeed=%.1f RFootSpeed=%.1f | CmdStand=%d"),
            ForwardSpeed, LateralSpeed, TrunkAngVelZ,
            GetFootHorizontalSpeed(FootL), GetFootHorizontalSpeed(FootR),
            bCommandedStand ? 1 : 0);
    }

    // =========================================================
    // 5) 步态相位估计（用于可视化/调试，不直接入reward）
    // =========================================================
    const float StepsPerCycle = FMath::Max(DesiredStepPeriod * SimulationFrequencyHz, 1.0f);
    GaitPhase = FMath::Fmod(CurrentStep * 2.0f * PI / StepsPerCycle, 2.0f * PI);

<<<<<<< HEAD
    return FMath::Clamp(Components.Total, RewardClampMin, RewardClampMax);
=======
    return Components.Total;
>>>>>>> worktree-training-stability-fixes
}

// ---------------------------------------------------------------------------
// 终止条件 & 重置
// ---------------------------------------------------------------------------

bool UOrangeRobotEnvComponent::CheckFallen() const
{
    // 1. 头部触地检测（优先）
    if (HeadComponent)
    {
        const double HeadHeight = HeadComponent->GetComponentLocation().Z;
        if (HeadHeight < HeadGroundHeightThreshold)
            return true;
    }

    // 2. 躯干高度检测
    const double BodyHeight = GetBodyHeight();
    if (BodyHeight > KINDA_SMALL_NUMBER && BodyHeight < BodyHeightThreshold)
    {
        UE_LOG(LogTemp, Verbose, TEXT("CheckFallen: Body height %.2f < threshold %.2f -> FALLEN"), 
            BodyHeight, BodyHeightThreshold);
        return true;
    }

    // 3. 躯干倾斜检测
    const float UprightDot = GetUprightDot();
    const float UprightDotThreshold = FMath::Cos(FMath::DegreesToRadians(FallTiltThreshold));
    if (UprightDot < UprightDotThreshold)
        return true;

    // 4. 身体其他部位（除脚外）接触地面检测
    for (UStaticMeshComponent* BodyLink : BodyLinks)
    {
        if (!BodyLink) continue;
        if (BodyLink == FootL || BodyLink == FootR) continue;
        if (IsComponentTouchingGround(BodyLink))
        {
            UE_LOG(LogTemp, Verbose, TEXT("CheckFallen (BodyPart %s): Touching ground -> FALLEN"), *BodyLink->GetName());
            return true;
        }
    }

    return false;
}
void UOrangeRobotEnvComponent::ResetEnv()
{
    CurrentStep = 0;
    LastAction.Empty();
    PreviousAction.Empty();
    LastSingleSupportSide = ESupportSide::None;
    SameLegConsecutiveSteps = 0;
    GaitPhase = 0.0f;
    bLTouchPrev = false;
    bRTouchPrev = false;
    LastStepTransitionStep = -1;
    SampleEpisodeHighLevelCommand();
    UpdateCurriculumWeightsAndCommand();

    if (JointActionAxes.Num() != DriveConstraints.Num() || JointAxisCaches.Num() != DriveConstraints.Num())
        CacheJointActionAxes();

    for (int32 i = 0; i < BodyLinks.Num(); ++i)
    {
        UStaticMeshComponent* Link = BodyLinks[i];
        if (Link && InitialBodyLinkTransforms.IsValidIndex(i))
            Link->SetWorldTransform(InitialBodyLinkTransforms[i], false, nullptr, ETeleportType::ResetPhysics);
    }

    for (UStaticMeshComponent* Link : BodyLinks)
    {
        if (Link && Link->IsSimulatingPhysics())
        {
            Link->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Link->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            Link->WakeAllRigidBodies();
        }
    }

    for (UPhysicsConstraintComponent* Con : DriveConstraints)
    {
        if (Con) Con->SetAngularVelocityTarget(FVector::ZeroVector);
    }

    if (bApplyRandomJointOffsetsOnReset)
    {
        for (UPhysicsConstraintComponent* Con : DriveConstraints)
        {
            if (!Con) continue;
            const FVector RandVel(FMath::FRandRange(-5.0f,5.0f), FMath::FRandRange(-3.0f,3.0f), FMath::FRandRange(-3.0f,3.0f));
            Con->SetAngularVelocityTarget(ClampAngularVelocityTarget(RandVel));
        }
    }
}

// ---------------------------------------------------------------------------
// IAgent 接口
// ---------------------------------------------------------------------------

EAgentStatus UOrangeRobotEnvComponent::GetStatus_Implementation() { return AgentStatus; }
void UOrangeRobotEnvComponent::SetStatus_Implementation(EAgentStatus NewStatus) { AgentStatus = NewStatus; }

void UOrangeRobotEnvComponent::Define_Implementation(FInteractionDefinition& OutInteractionDefinition)
{
<<<<<<< HEAD
    if (JointAxisCaches.IsEmpty() || JointActionAxes.IsEmpty())
    {
        CacheJointActionAxes();
    }
    const int32 ObsDim = GetObservationDim();
    const int32 ActDim = GetActionDim();

    UE_LOG(LogTemp, Warning, TEXT("[Define] ObsDim=%d, ActDim=%d, JointAxisCount=%d, DriveConstraints=%d, bEnableCmd=%d"),
        ObsDim, ActDim, JointActionAxes.Num(), DriveConstraints.Num(), bEnableCommandReward ? 1 : 0);
=======
    const int32 ObsDim = GetObservationDim();
    const int32 ActDim = GetActionDim();
>>>>>>> worktree-training-stability-fixes
    TArray<float> ObsLow, ObsHigh, ActLow, ActHigh;
    ObsLow.Init(-5.0f, ObsDim);  ObsHigh.Init(5.0f, ObsDim);
    ActLow.Init(-1.0f, ActDim);  ActHigh.Init(1.0f, ActDim);
    OutInteractionDefinition = FInteractionDefinition(
        TInstancedStruct<FSpace>::Make<FBoxSpace>(ObsLow, ObsHigh),
        TInstancedStruct<FSpace>::Make<FBoxSpace>(ActLow, ActHigh));
}

void UOrangeRobotEnvComponent::Act_Implementation(const FInstancedStruct& InAction)
{
    if (AgentStatus != EAgentStatus::Running) return;
    const FBoxPoint* BoxAction = InAction.GetPtr<FBoxPoint>();
    if (BoxAction) ApplyAction(BoxAction->Values);
}

void UOrangeRobotEnvComponent::Observe_Implementation(FInstancedStruct& OutObservations)
{
    if (AgentStatus != EAgentStatus::Running)
    {
        OutObservations.InitializeAs<FBoxPoint>(TArray<float>());
        return;
    }
    OutObservations.InitializeAs<FBoxPoint>(CollectObservations());
}