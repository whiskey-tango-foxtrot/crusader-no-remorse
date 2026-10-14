#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CruSpritePlacer.h"
#include "Usecode/CruCore.h"
#include "CrusaderGameSubsystem.generated.h"

class FCruEngine; class ACrusaderSpriteBatch; class ACrusaderLevelInfo; class UMediaPlayer; class UMediaTexture; class UFileMediaSource; class UMediaSoundComponent;

// Runs the usecode engine for the loaded level and mirrors item changes into the sprite/collision batches.
UCLASS()
class CRUSADERREMAKE_API UCrusaderGameSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCrusaderGameSubsystem, STATGROUP_Tickables); }
    virtual bool IsTickable() const override { return true; }

    UFUNCTION(BlueprintCallable, Category = "Crusader") bool UseNearest();
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool Fire();
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool TeleportToEgg(int32 Map, int32 Egg);
    UFUNCTION(BlueprintCallable, Category = "Crusader") void ToggleKneel();
    UFUNCTION(BlueprintCallable, Category = "Crusader") void ToggleCombat();
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool SaveGame(int32 Slot);
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool LoadGame(int32 Slot);
    UFUNCTION(BlueprintCallable, Category = "Crusader") void UseItem();
    UFUNCTION(BlueprintCallable, Category = "Crusader") void NextItem();
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool PlayMovie(const FString& Name);   // cutscene by FLICS name (e.g. 01A, mva1)
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool IsMoviePlaying() const { return MoviePlayer != nullptr; }
    UFUNCTION(BlueprintCallable, Category = "Crusader") void SkipMovie();
    UMediaTexture* GetMovieTexture() const { return MoviePlayer ? MovieTexture : nullptr; }
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool SetWeapon(int32 Shape);
    UFUNCTION(BlueprintCallable, Category = "Crusader") bool NextWeapon(int32 Delta);          // cycle the active weapon (Delta < 0: previous)
    UFUNCTION(BlueprintCallable, Category = "Crusader") void SetInfiniteAmmo(bool bEnabled);   // cheat
    UFUNCTION(BlueprintCallable, Category = "Crusader") int32 AvatarHp() const;
    bool IsRunning() const { return Engine != nullptr; }
    void NewGame();                       // restart mission 1 (reloads the level unless the game has just started)
    void StartMission(int32 Mission);     // mission picker (1-15)
    void ApplyAudioSettings();            // music / sfx volume from FCruSettings
    void SetDifficulty(int32 D);
    void AvatarMove(int32 Anim, bool bEnterCombat);
    void OnMenuOpened();                  // original menu music (track 20), restored on close
    void OnMenuClosed();
    int32 StartupStep = 0;                // 1: play T01, 2: play T02, 3: open the original menu
    struct FCredLine { bool bTitle; FString Text; };
    struct FCredScreen { TArray<FCredLine> Lines; int32 Delay = 60; };
    TArray<FCredScreen> Credits;          // static/credits.dat (CD) screens
    bool LoadCredits();
    bool IsMenuOpen() const;
    FCruEngine* GetEngine() const { return Engine; }

private:
    bool TryInit();
    void ApplyChanges();
    void PlaySfx();
    void UpdateCollision(const FCruItem& It, const FCruShapeInfo& Si, bool bRemove);

    FCruEngine* Engine = nullptr;
    FCruSpritePlacer Placer;
    bool bInitTried = false;
    bool bWantMainMenu = false, bMoviePausedByMenu = false;
    int32 SavedMusicTrack = -1;
    bool bIntroMovie = false;             // the current movie is T01/T02 (not a usecode cutscene): do not touch the engine's movie state
    TMap<int32, TArray<TPair<ACrusaderSpriteBatch*, int32>>> StaticInstances;   // source index -> instances
    TMap<int32, int32> CollisionInstances;                                        // source index -> collision instance
    TSet<int32> HiddenSources;
    UPROPERTY() ACrusaderSpriteBatch* Collision = nullptr;
    UPROPERTY() TArray<ACrusaderSpriteBatch*> DynamicBatches;                      // per atlas
    TMap<ObjId, TPair<int32, int32>> DynamicInstances;                            // objid -> (atlas, instance)
    TMap<ObjId, int32> Dirty;   // objid -> source index at notification time
    UPROPERTY() TMap<int32, TObjectPtr<class USoundWave>> SfxCache;   // UPROPERTY: the packaged game garbage-collects unreferenced waves (crashed PlaySound2D)
    TMap<ObjId, int32> DynamicCollision;                                          // runtime-created solid items
    FVector StartLocation = FVector::ZeroVector;
    bool bAvatarEngineAnim = false;
    float DiagTimer = 0.f; bool bDiagDone = false;
    void StopMovie();
    void UpdateMovie(float DeltaTime);
    UPROPERTY() UMediaPlayer* MoviePlayer = nullptr;
    UPROPERTY() UMediaTexture* MovieTexture = nullptr;
    UPROPERTY() UFileMediaSource* MovieSource = nullptr;
    UPROPERTY() UMediaSoundComponent* MovieSound = nullptr;
    float MovieTime = 0.f;
    void UpdateMusic();
    UPROPERTY() class UAudioComponent* Music = nullptr;
    UPROPERTY() class USoundAttenuation* SfxAttenuation = nullptr;
    bool bListenerSet = false;
};
