#include "CrusaderSpriteBatch.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

ACrusaderSpriteBatch::ACrusaderSpriteBatch()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    Mesh = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Static);
    Mesh->NumCustomDataFloats = 5;
    Mesh->SetCastShadow(false);
    Mesh->bCastDynamicShadow = false;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACrusaderSpriteBatch::BeginPlay()
{
    Super::BeginPlay();
    // Levels loaded straight into a game world (OpenLevel) keep the cluster tree the Python builder saved, which does
    // not match the instances for the large batches (only a subset renders); rebuild it like the editor does on register.
    if (Anims.Num() == 0) return;
    EnsureArrays();
    bool bDirty = false;
    for (FCrusaderSpriteAnim& A : Anims)
    {
        for (int32 f = 0; f < A.Instances.Num(); ++f)
            if (A.Instances[f] >= 0 && A.Instances[f] < AnimHidden.Num())
            {
                AnimHidden[A.Instances[f]] = (f == A.Frame) ? 0 : 1;
                bDirty |= SetVisible(A.Instances[f]);
            }
    }
    if (bDirty && Mesh) Mesh->MarkRenderStateDirty();
    SetActorTickEnabled(true);
}

void ACrusaderSpriteBatch::HideInstanceForever(int32 i)
{
    EnsureArrays();
    if (!AnimHidden.IsValidIndex(i)) return;
    AnimHidden[i] = 1;
    for (FCrusaderSpriteAnim& A : Anims) for (int32& Inst : A.Instances) if (Inst == i) Inst = -1;
    SetVisible(i);
}

void ACrusaderSpriteBatch::EnsureArrays()
{
    const int32 N = Mesh ? Mesh->GetInstanceCount() : 0;
    if (RoofHidden.Num() != N) RoofHidden.Init(0, N);
    if (AnimHidden.Num() != N) AnimHidden.Init(0, N);
}

bool ACrusaderSpriteBatch::SetVisible(int32 i)
{
    if (!Mesh || i < 0 || i >= RoofHidden.Num()) return false;
    const float Want = (RoofHidden[i] || AnimHidden[i]) ? 0.f : 1.f;
    const float Have = Mesh->PerInstanceSMCustomData.IsValidIndex(i * 5 + 4) ? Mesh->PerInstanceSMCustomData[i * 5 + 4] : -1.f;
    if (FMath::IsNearlyEqual(Want, Have)) return false;
    Mesh->SetCustomDataValue(i, 4, Want, false);
    return true;
}

void ACrusaderSpriteBatch::StepAnim(FCrusaderSpriteAnim& A, bool& bDirty)
{
    // Port of ScummVM Item::animateItem (Crusader branch)
    const int32 Count = A.Instances.Num();
    if (Count == 0) return;
    const int32 Old = A.Frame;
    int32 F = A.Frame;
    switch (A.Type)
    {
    case 2:
        if (FMath::RandBool()) F = FMath::RandRange(0, Count - 1);
        break;
    case 1: case 3:
        if (A.Data == 0 || (A.Data == 1 && FMath::RandBool())) { if (++F >= Count) F = 0; }
        else if (A.Data > 1) { ++F; const int32 Num = (F - 1) / A.Data; if (F == (Num + 1) * A.Data) F = Num * A.Data; if (F >= Count) F = 0; }
        break;
    case 4:
        if (F != 0 || FMath::RandRange(0, A.Data + 1) == 0) { if (++F >= Count) F = 0; }
        break;
    case 6:
        if (A.Data == 0 || (A.Data == 1 && FMath::RandBool())) { if (F != 0) { if (++F >= Count) F = 1; } }
        else if (A.Data > 1) { if (F % A.Data) { if (++F >= Count) F = 1; } }
        break;
    default: break;   // 5 = usecode driven, 0 = none
    }
    if (F == Old) return;
    A.Frame = F;
    if (A.Instances.IsValidIndex(Old) && A.Instances[Old] >= 0) { AnimHidden[A.Instances[Old]] = 1; bDirty |= SetVisible(A.Instances[Old]); }
    if (A.Instances.IsValidIndex(F) && A.Instances[F] >= 0) { AnimHidden[A.Instances[F]] = 0; bDirty |= SetVisible(A.Instances[F]); }
}

void ACrusaderSpriteBatch::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Anims.Num() == 0) return;
    EnsureArrays();
    bool bDirty = false;
    for (FCrusaderSpriteAnim& A : Anims)
    {
        const float Period = FMath::Max(1, A.Speed) / 30.f;   // game ticks run at 30 Hz
        A.Accum += DeltaSeconds;
        int32 Steps = 0;
        while (A.Accum >= Period && Steps < 8) { A.Accum -= Period; ++Steps; StepAnim(A, bDirty); }
        if (A.Accum >= Period) A.Accum = 0.f;
    }
    if (bDirty && Mesh) Mesh->MarkRenderStateDirty();
}

float ACrusaderSpriteBatch::FindRoofZLimit(const FVector& P) const
{
    float Limit = TNumericLimits<float>::Max();
    if (!bRoofLayer) return Limit;
    for (int32 i = 0; i < RoofBounds.Num() && i < RoofZ.Num(); ++i)
    {
        const FVector4& B = RoofBounds[i];
        if (P.X >= B.X && P.X <= B.Z && P.Y >= B.Y && P.Y <= B.W && RoofZ[i] > P.Z + 1.f)
            Limit = FMath::Min(Limit, RoofZ[i]);
    }
    return Limit;
}

void ACrusaderSpriteBatch::ApplyZLimit(float ZLimit)
{
    if (!Mesh || FMath::IsNearlyEqual(ZLimit, AppliedZLimit)) return;
    AppliedZLimit = ZLimit;
    EnsureArrays();
    const int32 N = FMath::Min(InstanceZ.Num(), RoofHidden.Num());
    bool bDirty = false;
    for (int32 i = 0; i < N; ++i)
    {
        const uint8 Want = InstanceZ[i] >= ZLimit - 0.5f ? 1 : 0;
        if (Want != RoofHidden[i])
        {
            RoofHidden[i] = Want;
            bDirty |= SetVisible(i);
        }
    }
    if (bDirty) Mesh->MarkRenderStateDirty();
}
