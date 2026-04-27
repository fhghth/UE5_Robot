// Fill out your copyright notice in the Description page of Project Settings.

#include "OrangeRobotEnvComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"

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

    /** 根据躯干组件计算局部平面速度 */
    FVector GetLocalPlanarVelocityFromComponent(const UPrimitiveComponent* Component, const FVector& WorldVelocity)
    {
        if (!Component) return FVector::ZeroVector;
        const FVector Forward = Component->GetForwardVector().GetSafeNormal2D();
        const FVector Right   = Component->GetRightVector().GetSafeNormal2D();
        const FVector PlanarVelocity(WorldVelocity.X, WorldVelocity.Y, 0.0f);
        return FVector(
            FVector::DotProduct(PlanarVelocity, Forward),
            FVector::DotProduct(PlanarVelocity, Right),
            0.0f);
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
// 构造函数 & 调试绘制
// ---------------------------------------------------------------------------

UOrangeRobotEnvComponent::UOrangeRobotEnvComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UOrangeRobotEnvComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if WITH_EDITOR
    if (bEnableHighLevelCommand) DrawHighLevelCommandDebug();
#endif
}

#if WITH_EDITOR
void UOrangeRobotEnvComponent::DrawHighLevelCommandDebug() const
{
    if (!bEnableHighLevelCommand) return;
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_DedicatedServer) return;

    UPrimitiveComponent* TrunkComp = GetTrunkPrimitive();
    if (!TrunkComp) return;

    const FVector Loc = TrunkComp->GetComponentLocation();
    const FVector Fwd = TrunkComp->GetForwardVector();
    const FVector Right = TrunkComp->GetRightVector();
    const FVector Up = TrunkComp->GetUpVector();

    const FVector Fwd2D = Fwd.GetSafeNormal2D();
    const FVector Right2D = Right.GetSafeNormal2D();

    // 前进命令箭头
    const float FwdStrength = HighLevelCommand.X;
    if (!FMath::IsNearlyZero(FwdStrength))
    {
        const FVector Dir = (FwdStrength > 0.0f) ? Fwd2D : -Fwd2D;
        const float Len = FMath::Abs(FwdStrength) * 50.0f;
        DrawDebugDirectionalArrow(World, Loc + Up * 20.0f, Loc + Up * 20.0f + Dir * Len,
            5.0f, FwdStrength > 0.0f ? FColor::Green : FColor::Red, false, -1.0f, 0, 2.0f);
    }

    // 转向命令圆弧
    const float TurnStrength = HighLevelCommand.Y;
    if (!FMath::IsNearlyZero(TurnStrength))
    {
        const FVector Center = Loc + Up * 30.0f;
        const float Radius = 40.0f;
        const float Sweep = FMath::Abs(TurnStrength) * 180.0f;
        const float Start = TurnStrength > 0.0f ? 0.0f : 180.0f;
        const float End = TurnStrength > 0.0f ? Sweep : 180.0f - Sweep;
        const int32 Segs = FMath::Clamp(FMath::CeilToInt(FMath::Abs(End - Start) / 8.0f) + 1, 4, 32);
        const FColor Color = TurnStrength > 0.0f ? FColor::Cyan : FColor::Orange;

        FVector Prev = FVector::ZeroVector;
        for (int32 i = 0; i <= Segs; ++i)
        {
            const float Alpha = (float)i / Segs;
            const float Ang = FMath::DegreesToRadians(FMath::Lerp(Start, End, Alpha));
            const FVector Pt = Center + Radius * (Fwd2D * FMath::Cos(Ang) + Right2D * FMath::Sin(Ang));
            if (i > 0) DrawDebugLine(World, Prev, Pt, Color, false, -1.0f, 0, 1.5f);
            Prev = Pt;
        }

        const float ArrowAng = TurnStrength > 0.0f ? 150.0f : 30.0f;
        const FVector ArrowDir = Fwd2D.RotateAngleAxis(ArrowAng, Up).GetSafeNormal();
        DrawDebugDirectionalArrow(World, Center, Center + ArrowDir * Radius, 3.0f, Color, false, -1.0f, 0, 1.5f);
    }

    DrawDebugString(World, Loc + Up * 50.0f,
        *FString::Printf(TEXT("Cmd: Fwd %.2f | Turn %.2f"), HighLevelCommand.X, HighLevelCommand.Y),
        nullptr, FColor::White, 0.0f, true);
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

        UE_LOG(LogTemp, Warning,
            TEXT("Joint %d (%s) axes=%d [Twist=%s Swing1=%s Swing2=%s]"),
            i, Con ? *Con->GetName() : TEXT("nullptr"), Cache.GetAxisCount(),
            Cache.bUseTwist  ? TEXT("true") : TEXT("false"),
            Cache.bUseSwing1 ? TEXT("true") : TEXT("false"),
            Cache.bUseSwing2 ? TEXT("true") : TEXT("false"));
    }
}

// ---------------------------------------------------------------------------
// 空间维度查询
// ---------------------------------------------------------------------------

int32 UOrangeRobotEnvComponent::GetLowLevelObservationDim() const
{
    // 躯干固定：躯干高度(1) + 线速度(3) + 角速度(3) + 重力投影(1) = 8
    // 脚部增强：触地×2, 相对高度×2, 滑动速度×2 = 6
    // 关节：每个可控轴 2 维（角度+角速度）
    int32 JointDim = 0;
    for (const FOrangeRobotConstraintAxisCache& AxisCache : JointAxisCaches)
    {
        JointDim += AxisCache.GetAxisCount() * 2;
    }
    return 8 + 6 + JointDim;
}

int32 UOrangeRobotEnvComponent::GetObservationDim() const
{
    return GetLowLevelObservationDim() + (bEnableHighLevelCommand ? 2 : 0);
}

void UOrangeRobotEnvComponent::SetHighLevelCommand(FVector2D InHighLevelCommand)
{
    HighLevelCommand.X = SanitizeFiniteScalar(InHighLevelCommand.X, -1.0f, 1.0f);
    HighLevelCommand.Y = SanitizeFiniteScalar(InHighLevelCommand.Y, -1.0f, 1.0f);
}

void UOrangeRobotEnvComponent::ClearHighLevelCommand()
{
    HighLevelCommand = FVector2D::ZeroVector;
}

void UOrangeRobotEnvComponent::SampleEpisodeHighLevelCommand()
{
    if (!bEnableHighLevelCommand) return;
    if (bSampleHighLevelCommandOnReset)
        SetHighLevelCommand(FVector2D(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f)));
    else
        ClearHighLevelCommand();
}

// ---------------------------------------------------------------------------
// 机器人状态辅助
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
// 初始 Transform 记录
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

    UE_LOG(LogTemp, Warning, TEXT("CaptureInitialTransform: Initial Foot Distances - Left: %.2f cm, Right: %.2f cm"),
        InitialLeftFootDistance, InitialRightFootDistance);

    CacheJointActionAxes();
    LogDriveConstraintStates();
}

void UOrangeRobotEnvComponent::LogDriveConstraintStates() const
{
    UE_LOG(LogTemp, Warning, TEXT("========== OrangeRobot Drive Constraint Debug =========="));
    UE_LOG(LogTemp, Warning, TEXT("DriveConstraints.Num() = %d"), DriveConstraints.Num());
    for (int32 i = 0; i < DriveConstraints.Num(); ++i)
    {
        UPhysicsConstraintComponent* Con = DriveConstraints[i];
        if (!Con) { UE_LOG(LogTemp, Warning, TEXT("Constraint[%d]: nullptr"), i); continue; }

        FConstraintInstanceAccessor Acc(Con);
        TEnumAsByte<EAngularDriveMode::Type> Mode = EAngularDriveMode::TwistAndSwing;
        bool bTVel = false, bSVel = false, bSLVel = false;
        UConstraintInstanceBlueprintLibrary::GetAngularDriveMode(Acc, Mode);
        UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveTwistAndSwing(Acc, bTVel, bSVel);
        UConstraintInstanceBlueprintLibrary::GetAngularVelocityDriveSLERP(Acc, bSLVel);

        EAngularConstraintMotion TM, S1, S2;
        GetAngularMotions(Con, TM, S1, S2);

        const FOrangeRobotConstraintAxisCache Cache = BuildConstraintAxisCache(Con);
        UE_LOG(LogTemp, Warning,
            TEXT("Constraint[%d] %s | Mode=%s | Vel(T=%s S=%s SL=%s) | Mot(T=%s S1=%s S2=%s) | Axes=%d [T=%s,S1=%s,S2=%s]"),
            i, *Con->GetName(),
            AngularDriveModeToString(Mode),
            bTVel  ? TEXT("true") : TEXT("false"),
            bSVel  ? TEXT("true") : TEXT("false"),
            bSLVel ? TEXT("true") : TEXT("false"),
            AngularMotionToString(TM), AngularMotionToString(S1), AngularMotionToString(S2),
            Cache.GetAxisCount(),
            Cache.bUseTwist  ? TEXT("true") : TEXT("false"),
            Cache.bUseSwing1 ? TEXT("true") : TEXT("false"),
            Cache.bUseSwing2 ? TEXT("true") : TEXT("false"));
    }
    UE_LOG(LogTemp, Warning, TEXT("======================================================="));
}

// ---------------------------------------------------------------------------
// Step：施加动作
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
}

// ---------------------------------------------------------------------------
// Step：收集观测（自由度过滤 + 脚部增强）
// ---------------------------------------------------------------------------

TArray<float> UOrangeRobotEnvComponent::CollectLowLevelObservations() const
{
    TArray<float> Obs;
    const int32 NumJoints = DriveConstraints.Num();
    Obs.Reserve(GetLowLevelObservationDim());

    // ---- 躯干核心信息 (8 维) ----
    UPrimitiveComponent* TrunkComp = GetTrunkPrimitive();
    FVector Forward = TrunkComp ? TrunkComp->GetForwardVector() : FVector::ForwardVector;
    FVector Right   = TrunkComp ? TrunkComp->GetRightVector()   : FVector::RightVector;
    FVector Up      = TrunkComp ? TrunkComp->GetUpVector()      : FVector::UpVector;
    FVector WorldVel = TrunkComp
        ? SanitizeFiniteVector(TrunkComp->GetComponentVelocity(), -5000.0f, 5000.0f)
        : FVector::ZeroVector;

    // 躯干高度
    const float TrunkHeight = GetBodyHeight();
    Obs.Add(FMath::Clamp(TrunkHeight / FMath::Max(TrunkHeightNormalization, 1.0f), 0.0f, 2.0f));

    // 局部线速度
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(WorldVel, Forward) / 200.0f, -5.0f, 5.0f));
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(WorldVel, Right) / 200.0f, -5.0f, 5.0f));
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(WorldVel, Up) / 200.0f, -5.0f, 5.0f));

    // 局部角速度
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

    // 重力投影
    const USceneComponent* TiltComp = GetTiltReferenceComponent();
    const FVector TiltUp = TiltComp ? TiltComp->GetUpVector() : FVector::UpVector;
    Obs.Add(SanitizeFiniteScalar(FVector::DotProduct(TiltUp, FVector::UpVector), -1.0f, 1.0f));

    // ---- 脚部增强观测 (6 维) ----
    auto AddFootObs = [&](const UPrimitiveComponent* Foot, float InitialDist)
    {
        float Contact = 0.0f, RelHeight = 0.0f, SlideSpeed = 0.0f;
        if (Foot && TiltCheckComponent)
        {
            const float CurrentDist = TiltCheckComponent->GetComponentLocation().Z - Foot->GetComponentLocation().Z;
            const float Lift = InitialDist - CurrentDist;
            // 触地连续值：抬升越小越接近1
            Contact = 1.0f - FMath::Clamp(Lift / FMath::Max(InitialDist * 0.5f, 1.0f), 0.0f, 1.0f);
            // 相对高度（站立时约1）
            RelHeight = FMath::Clamp(CurrentDist / FMath::Max(InitialDist, 1.0f), 0.0f, 2.0f);
            // 水平滑动速度
            SlideSpeed = FMath::Clamp(GetFootHorizontalSpeed(Foot) / FMath::Max(FootStableSpeedThreshold, 1.0f), 0.0f, 3.0f);
        }
        Obs.Add(Contact);
        Obs.Add(RelHeight);
        Obs.Add(SlideSpeed);
    };
    AddFootObs(FootL, InitialLeftFootDistance);
    AddFootObs(FootR, InitialRightFootDistance);

    // ---- 关节观测（仅可控轴，每轴 2 维） ----
    for (int32 i = 0; i < NumJoints; ++i)
    {
        if (!JointAxisCaches.IsValidIndex(i)) continue;
        const FOrangeRobotConstraintAxisCache& Cache = JointAxisCaches[i];

        // 获取原始角度与角速度
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

    return Obs;
}

TArray<float> UOrangeRobotEnvComponent::CollectObservations() const
{
    TArray<float> Obs = CollectLowLevelObservations();
    Obs.Reserve(GetObservationDim());
    if (bEnableHighLevelCommand)
    {
        Obs.Add(SanitizeFiniteScalar(HighLevelCommand.X, -1.0f, 1.0f));
        Obs.Add(SanitizeFiniteScalar(HighLevelCommand.Y, -1.0f, 1.0f));
    }
    return Obs;
}

// ---------------------------------------------------------------------------
// Step：计算奖励
// ---------------------------------------------------------------------------

float UOrangeRobotEnvComponent::ComputeReward()
{
    // ==================== 基础运动学变量 ====================
    UPrimitiveComponent* TrunkComp = GetTrunkPrimitive();
    const FVector Velocity = TrunkComp ? TrunkComp->GetComponentVelocity() : FVector::ZeroVector;
    const FVector LocalPlanarVel = GetLocalPlanarVelocityFromComponent(TrunkComp, Velocity);
    const float ForwardSpeed = LocalPlanarVel.X;
    const float LateralSpeed = FMath::Abs(LocalPlanarVel.Y);

    // 躯干角速度（世界 Z，用于转向）
    float TrunkAngVelZ = 0.0f;
    if (TrunkComp)
        TrunkAngVelZ = SanitizeFiniteAngleDegrees(TrunkComp->GetPhysicsAngularVelocityInDegrees().Z);

    // 姿态与支撑
    const float UprightDot = GetUprightDot();
    const bool bLStable = IsFootStableSupport(FootL);
    const bool bRStable = IsFootStableSupport(FootR);
    const bool bHasSupport = bLStable || bRStable;
    const bool bDoubleSupport = bLStable && bRStable;
    const bool bSingleSupport = bLStable != bRStable;
    const float BodyHeight = GetBodyHeight();

    // 脚部额外信息（供步态奖励使用）
    const bool bLTouch = IsFootTouchingGround(FootL);
    const bool bRTouch = IsFootTouchingGround(FootR);
    const float LSlide = GetFootHorizontalSpeed(FootL);
    const float RSlide = GetFootHorizontalSpeed(FootR);

    // 命令相关
    const float CmdFwd = HighLevelCommand.X;
    const float CmdTurn = HighLevelCommand.Y;
    const bool bIsStanding = (FMath::Abs(CmdFwd) < 0.05f && FMath::Abs(CmdTurn) < 0.05f);  // 站立模式判定

    // ==================== 奖励累加 ====================
    float Reward = 0.0f;

    // ---------- 1. 生存与姿态（始终启用） ----------
    Reward += AliveReward;

    // 躯干直立奖励（不再乘以支撑因子和高度因子，避免过度耦合）
    const float UprightError = 1.0f - UprightDot;
    Reward -= UprightError * UprightRewardScale;  // 惩罚偏离竖直

    // 身体高度奖励（鼓励维持目标高度，使用指数核）
    const float HeightError = BodyHeight - BodyHeightRewardMax;
    Reward -= FMath::Square(HeightError) * 0.0002f;  // 可调系数，此处用固定小值，亦可新增属性

    // 横向漂移惩罚
    Reward -= LateralSpeed * LateralVelocityPenaltyScale;

    // ---------- 2. 站立专项奖励（仅当命令接近零时启用） ----------
    if (bEnableStandReward && bIsStanding)
    {
        // 惩罚躯干移动速度
        const float StandSpeed = Velocity.Size();
        Reward -= StandSpeed * StandVelocityPenaltyScale;

        // 惩罚躯干角速度
        const float TrunkAngSpeed = TrunkComp ? TrunkComp->GetPhysicsAngularVelocityInDegrees().Size() : 0.0f;
        Reward -= TrunkAngSpeed * StandAngularVelocityPenaltyScale;

        // 奖励双脚稳定支撑（利用触地连续值更好，但此用稳定支撑布尔）
        if (bDoubleSupport)
        {
            Reward += StandStableFootReward;
        }
    }

    // ---------- 3. 命令跟踪（仅当命令非零时有效，禁用无条件前进奖励） ----------
    if (bEnableCommandReward && !bIsStanding)  // 站立模式下不计算命令奖励
    {
        const float DesiredFwd = CmdFwd * MaxForwardSpeed;
        const float DesiredTurn = CmdTurn * MaxTurnSpeedDegPerSec;

        // 指数核匹配前进速度
        const float FwdError = ForwardSpeed - DesiredFwd;
        const float FwdMatch = FMath::Exp(-FwdError * FwdError / (2.0f * FMath::Square(MaxForwardSpeed * 0.5f)));
        // 指数核匹配转向速度
        const float TurnError = TrunkAngVelZ - DesiredTurn;
        const float TurnMatch = FMath::Exp(-TurnError * TurnError / (2.0f * FMath::Square(MaxTurnSpeedDegPerSec * 0.5f)));

        // 直立度仅作为轻度加权，不乘支撑因子
        const float CommandWeight = 1.0f + 0.3f * UprightDot;  // 越直立则跟踪奖励略高
        Reward += (FwdMatch * ForwardCommandRewardWeight + TurnMatch * TurnCommandRewardWeight)
                * CommandMatchBaseReward * CommandWeight;
    }

    // ---------- 4. 步态质量（替换原 bEnableDynamicBalanceReward） ----------
    if (bEnableDynamicBalanceReward)
    {
        // 4.1 双脚支撑鼓励（仅在双支撑时）
        if (bDoubleSupport)
        {
            Reward += DoubleSupportRewardScale;
        }

        // 4.2 脚部滑动惩罚（基于观测中的滑动速度，始终生效）
        //     使用平方惩罚，柔和且平滑
        Reward -= (LSlide * LSlide + RSlide * RSlide) * DualFootShufflePenaltyScale * 0.1f; // 调整系数

        // 4.3 单脚支撑时的步态质量
        if (bSingleSupport)
        {
            const UPrimitiveComponent* SwingFoot = bLStable ? FootR : FootL;
            const float InitDist = (SwingFoot == FootL) ? InitialLeftFootDistance : InitialRightFootDistance;
            const float CurDist = GetFootDistanceFromTrunk(SwingFoot);
            const float ActualLift = FMath::Max(0.0f, InitDist - CurDist);

            // 摆动脚高度惩罚（低于最小高度）
            const float LiftDeficit = FMath::Max(0.0f, SwingFootMinHeight - ActualLift);
            Reward -= LiftDeficit * LiftDeficit * SwingFootHeightPenaltyScale;

            // 摆动脚接触地面惩罚（脚该离地却触地，利用新观测的连续触地值）
            if (bLTouch && bRStable)  // 右脚稳定，左脚应抬起
            {
                // 可通过 GetFootDistanceFromTrunk 判断是否本应抬起
                // 这里简单检查：如果左脚仍在触地范围内，给予小惩罚
                const float LeftContact = 1.0f - FMath::Clamp((InitDist - CurDist) / (InitDist * 0.5f), 0.0f, 1.0f);
                if (LeftContact > 0.5f)
                    Reward -= LeftContact * 0.05f;
            }
            else if (bRTouch && bLStable) // 对称处理
            {
                const float RightContact = 1.0f - FMath::Clamp((InitialRightFootDistance - CurDist) / (InitialRightFootDistance * 0.5f), 0.0f, 1.0f);
                if (RightContact > 0.5f)
                    Reward -= RightContact * 0.05f;
            }

            // 单脚支撑额外奖励（前进且姿态良好）
            if (ForwardSpeed > 10.0f && UprightDot > 0.95f && ActualLift >= SwingFootMinHeight)
            {
                Reward += SingleSupportBonusReward;
            }

            // 躯干偏移支撑中心惩罚（已存在，保留）
            Reward -= GetTrunkSupportOffsetNormalized(bLStable, bRStable) * TrunkSupportOffsetPenaltyScale;
        }
        else if (!bHasSupport) // 双脚均无稳定支撑
        {
            Reward -= UnstableSupportPenaltyScale;
        }
    }

    // ---------- 5. 终止惩罚 ----------
    if (CheckFallen())
    {
        Reward -= FallPenalty;
    }

    // ---------- 6. 动作平滑惩罚（始终启用） ----------
    if (LastAction.Num() == GetActionDim() && PreviousAction.Num() == LastAction.Num())
    {
        float SmoothPen = 0.0f;
        for (int32 i = 0; i < LastAction.Num(); ++i)
        {
            const float Delta = LastAction[i] - PreviousAction[i];
            SmoothPen += Delta * Delta;
        }
        Reward -= SmoothPen * ActionSmoothPenaltyScale;
    }

    // ==================== 调试日志（精简） ====================
    UE_LOG(LogTemp, Verbose,
        TEXT("Reward=%.3f | UpErr=%.3f | Lat=%.1f | Fwd=%.1f Turn=%.1f | CmdF=%.2f CmdT=%.2f | Stand=%s Fall=%s | Step=%d"),
        Reward, UprightError, LateralSpeed,
        ForwardSpeed, TrunkAngVelZ,
        CmdFwd, CmdTurn,
        bIsStanding ? TEXT("Y") : TEXT("N"),
        CheckFallen() ? TEXT("Y") : TEXT("N"),
        CurrentStep);

    return Reward;
}

// ---------------------------------------------------------------------------
// 终止条件
// ---------------------------------------------------------------------------

bool UOrangeRobotEnvComponent::CheckFallen() const
{
    if (HeadComponent)
    {
        if (HeadComponent->GetComponentLocation().Z < HeadGroundHeightThreshold)
            return true;
    }
    const float UprightDot = GetUprightDot();
    if (UprightDot < FMath::Cos(FMath::DegreesToRadians(FallTiltThreshold)))
        return true;
    return false;
}

// ---------------------------------------------------------------------------
// 重置环境
// ---------------------------------------------------------------------------

void UOrangeRobotEnvComponent::ResetEnv()
{
    UE_LOG(LogTemp, Warning, TEXT("======= ResetEnv CALLED! ======="));
    CurrentStep = 0;
    LastAction.Empty();
    PreviousAction.Empty();
    SampleEpisodeHighLevelCommand();

    if (JointActionAxes.Num() != DriveConstraints.Num() || JointAxisCaches.Num() != DriveConstraints.Num())
        CacheJointActionAxes();

    // 仅通过各 BodyLink 的初始 Transform 重置（不再使用 Owner Transform）
    for (int32 i = 0; i < BodyLinks.Num(); ++i)
    {
        UStaticMeshComponent* Link = BodyLinks[i];
        if (Link && InitialBodyLinkTransforms.IsValidIndex(i))
            Link->SetWorldTransform(InitialBodyLinkTransforms[i], false, nullptr, ETeleportType::ResetPhysics);
    }

    // 清零物理速度
    for (UStaticMeshComponent* Link : BodyLinks)
    {
        if (Link && Link->IsSimulatingPhysics())
        {
            Link->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Link->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            Link->WakeAllRigidBodies();
        }
    }

    // 清零关节驱动目标
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

    UE_LOG(LogTemp, Warning, TEXT("ResetEnv completed."));
}

// ---------------------------------------------------------------------------
// IAgent 接口
// ---------------------------------------------------------------------------

EAgentStatus UOrangeRobotEnvComponent::GetStatus_Implementation() { return AgentStatus; }
void UOrangeRobotEnvComponent::SetStatus_Implementation(EAgentStatus NewStatus) { AgentStatus = NewStatus; }

void UOrangeRobotEnvComponent::Define_Implementation(FInteractionDefinition& OutInteractionDefinition)
{
    const int32 ObsDim = GetObservationDim();
    const int32 ActDim = GetActionDim();
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