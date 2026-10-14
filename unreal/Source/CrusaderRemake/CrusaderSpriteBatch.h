#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrusaderSpriteBatch.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

// A frame-cycling level item (typeflag anim type/data/speed, see ScummVM Item::animateItem). Every frame of the
// shape is a separate instance in this batch; animation toggles which one is visible.
USTRUCT(BlueprintType)
struct FCrusaderSpriteAnim
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) int32 Type = 0;      // 1/3 cycle, 2 random frame, 4 random start then cycle, 6 loop 1..n
    UPROPERTY(EditAnywhere) int32 Data = 0;      // see animateItem: 0 always, 1 50% chance, 2+ frame blocks
    UPROPERTY(EditAnywhere) int32 Speed = 1;     // game ticks (1/30 s) per step
    UPROPERTY(EditAnywhere) int32 Frame = 0;     // current frame
    UPROPERTY(EditAnywhere) TArray<int32> Instances;   // instance index per frame (-1 = frame missing)
    float Accum = 0.f;
};

// One instanced mesh holding thousands of level sprites (or collision boxes).
// Sprites are unit quads instanced with per-instance custom data = atlas UV rect (u, v, du, dv) and visibility (index 4).
// Roof batches hide the instances above and around the player (see FindRoofZLimit / ApplyZLimit).
UCLASS()
class CRUSADERREMAKE_API ACrusaderSpriteBatch : public AActor
{
    GENERATED_BODY()
public:
    ACrusaderSpriteBatch();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite) UHierarchicalInstancedStaticMeshComponent* Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRoofLayer = false;
    float GetAppliedZLimit() const { return AppliedZLimit; }
    // Per instance (roof layers only): world-space footprint xmin, ymin, xmax, ymax and the roof's bottom z.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector4> RoofBounds;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<float> RoofZ;
    // Bottom z (UE units) of every instance, parallel to the instance list (all layers).
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<float> InstanceZ;
    // Frame-cycling items living in this batch.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCrusaderSpriteAnim> Anims;
    // Per instance: index of the source item in the map JSON (-1 = none); lets the runtime replace changed items.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<int32> InstanceItem;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AtlasIndex = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCollision = false;
    // Permanently hide one instance (the runtime took it over).
    void HideInstanceForever(int32 Instance);

    // Original engine rule (ScummVM GameMapGump): the lowest roof directly above the player sets a z limit,
    // and nothing at or above that height is drawn.
    UFUNCTION(BlueprintCallable) float FindRoofZLimit(const FVector& PlayerPos) const;
    UFUNCTION(BlueprintCallable) void ApplyZLimit(float ZLimit);

private:
    TArray<uint8> RoofHidden;    // per instance: hidden by the roof z limit
    TArray<uint8> AnimHidden;    // per instance: not the current frame of its animation
    float AppliedZLimit = TNumericLimits<float>::Max();
    void EnsureArrays();
    bool SetVisible(int32 Instance);   // pushes the combined visibility to custom data; true if it changed
    void StepAnim(FCrusaderSpriteAnim& A, bool& bDirty);
};
