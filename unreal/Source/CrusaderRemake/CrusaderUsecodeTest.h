#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CrusaderUsecodeTest.generated.h"

// Headless harness: runs the usecode engine on a map with a scripted avatar path and logs what the scripts do.
UCLASS()
class CRUSADERREMAKE_API UCrusaderUsecodeTest : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Start/End are world units (file coords * 2 for x/y). WalkTicks = ticks to move from Start to End; Ticks = total.
    UFUNCTION(BlueprintCallable, Category = "Crusader")
    static FString RunHeadless(const FString& DataDir, int32 Map, int32 Ticks, FVector Start, FVector End, int32 WalkTicks, const FString& TraceClasses, int32 UseAtTick, int32 FireAtTick = 0, int32 FireDir = -1, int32 WeaponShape = 0, bool bInfiniteAmmo = false);
};
