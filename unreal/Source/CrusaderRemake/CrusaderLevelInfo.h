#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrusaderLevelInfo.generated.h"

// Placed by build_level3d_batched.py: tells the runtime which map/atlas/data this level was built from.
UCLASS()
class CRUSADERREMAKE_API ACrusaderLevelInfo : public AActor
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MapNum = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DataDir;   // absolute path of the data/ folder (hd/, cd/, extracted/); set by the level builder
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString AtlasJson;
};
