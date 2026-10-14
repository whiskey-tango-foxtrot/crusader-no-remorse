#pragma once
#include "CruCore.h"
#include "UsecodeFlex.h"
#include "CruInventory.h"

class FKernel; class FUCMachine; class FCruWorld;

// Avatar state carried from one map (Unreal level) to the next on a cross-map teleport.
struct FCruCarryOver
{
    bool bValid = false; int32 Map = 0, Egg = 0;
    FIntVector Pos = FIntVector::ZeroValue; int32 Dir = -1;   // used instead of Egg when Egg < 0 (checkpoint loads)
    int32 Hp = 0, Mana = 0; uint32 Keycards = 0; uint16 ShieldType = 0; uint8 BatteryType = 0; uint32 ActiveWeaponShape = 0;
    struct FEntry { uint32 Shape, Frame; uint16 Quality; };
    TArray<FEntry> Items;
    bool bTestLoadout = false;   // mission picker: give a BA-40 if the avatar has no weapon
};
extern FCruCarryOver GCruCarryOver;
extern int32 GCruDifficulty;   // 1 Mama's Boy .. 4 No Remorse; applied to every loaded map

// Owns the usecode machine, kernel and item world; ticks them at 60 Hz. Port of the relevant Ultima8Engine bits.
class FCruEngine
{
public:
    FCruEngine();
    ~FCruEngine();
    bool Init(const FString& DataDir);                  // DataDir = the data/ folder (hd/, cd/, extracted/)
    bool LoadMap(int32 MapNum);
    FCruItem* SpawnAvatar(int32 X, int32 Y, int32 Z);
    void SetAvatarPosition(int32 X, int32 Y, int32 Z, int32 Dir16 = -1);  // from the pawn
    void Tick(float DeltaSeconds);                      // accumulates and runs whole 60 Hz ticks
    void RunOneTick();

    bool IsOnScreen(const FCruItem& It) const;
    // ItemSelectionProcess::selectNextItem + useSelectedItem: use the nearest selectable item (door panels, switches...)
    ObjId FindUsableItem() const;
    bool UseNearest();
    void CameraMoveTo(int32 X, int32 Y, int32 Z) { CameraPos = FIntVector(X, Y, Z); CameraFollow = 0; }
    void CameraCenterOn(ObjId Id) { CameraFollow = Id; }
    bool HasKeycard(int32 K) const { return (Keycards & (1 << K)) != 0; }
    bool AvatarFire();                                   // fire the avatar's weapon in its facing direction
    void ToggleKneel();                                  // kneelStartCru / kneelEndCru
    void ToggleCombat();                                 // original 'c' key: draw (readyWeapon) / holster (unreadyWeapon)
    bool AvatarMove(uint32 Anim, bool bEnterCombat);     // one-shot move anim (roll, slide, jump, advance, retreat); the anim moves the item
    bool IsAvatarInCombat() const;
    void StartPlaying() { bAvatarInStasis = false; }
    void StartMission1();                                // StartCrusaderProcess: teleport(1, 0x1e), whose teleporter chains to egg 99 (the fenced platform)     // StartCrusaderProcess: the mission cachein scripts set the avatar in stasis; play begins without it
    bool IsAvatarKneeling() const;
    bool AvatarHasSmallWeapon() const;                   // false = large weapon anims (combatStandLarge / retreatLarge)
    void SetAvatarStanceDir(int32 Dir);                  // idle in combat: show the combat-stand frame for the facing (the pawn drives movement)
    bool bAvatarStanceIdle = false;                      // set by the game layer: avatar standing still in combat (engine draws the stance frame)
    void UseActiveItem();                                // MainActor::useInventoryItem on the active inventory item
    void NextInvItem();
    // presentation: fades (0 = none, 1 = fully FadeColor) and cutscenes
    ProcId StartFade(float To, int32 Steps, FColor Color = FColor::Black);
    ProcId PlayMovie(const FString& Name);               // returns the movie process the usecode waits on (MovieGump notify process)                 // sets PendingMovie; the game layer starts playback
    void MovieFinished();
    float FadeAlpha = 0.f; FColor FadeColor = FColor::Black; ProcId FadePid = 0;
    FString PendingMovie; bool bMoviePlaying = false;
    uint32 SuppressMoviesUntilTick = 0;                  // a checkpoint reload replays the level start script; skip its intro cutscene
    bool bShowDeathText = false;
    // mouse aiming: the game layer sets the crosshair point (world units); shots with no auto-aim target go there
    FIntVector AimPoint = FIntVector::ZeroValue; bool bAimValid = false;
    class FTargetReticleProcess* Reticle = nullptr;
    void OnTargetItemMoved(const FCruItem& It);
    void AvatarWalked();                                 // moving out of combat stows the weapon (CruAvatarMoverProcess)
    // music (CruMusicProcess): track numbers from usecode, 1-15 = M01-M15, 16 cycles M16A/B/C, 19 cred, 20 menu
    void PlayMusic(int32 Track);
    FString MusicTrackName() const;
    int32 MusicTrack = 0; bool bMusicChanged = false; int32 M16Offset = 0;
    // pickup notifications (CruPickupAreaGump): drawn by the HUD for 90 frames (180 ticks)
    struct FPickupToast { uint32 Shape; uint32 Frame; int32 Q; bool bShowCount; uint32 StartTick; };
    TArray<FPickupToast> Pickups;
    void AddPickup(const FCruItem& Item, bool bShowCount);
    // MainActor::teleport(map, egg): same map -> move the avatar to the destination egg; other map -> hand the avatar
    // state to GCruCarryOver and ask the game layer to open that map's level (PendingMapSwitch)
    bool TeleportToEgg(int32 Map, int32 EggId);
    void StartMissionAt(int32 Map);                      // mission picker: go to the map's start egg (30, else the lowest) with the current inventory
    void TeleportAvatar(int32 X, int32 Y, int32 Z);
    void RestoreCarryOver();                             // after SpawnAvatar on the new map
    // checkpoint saves: map, avatar position and the carry-over state (inventory, health, energy, keycards, shield,
    // battery) as JSON; world / NPC state is not saved, loading restarts the level
    bool SaveCheckpoint(const FString& Path);
    bool LoadCheckpoint(const FString& Path);            // fills GCruCarryOver and requests the map switch
    void SnapshotAvatar(FCruCarryOver& C);
    int32 PendingMapSwitch = 0;
    bool bPendingPawnTeleport = false;                   // consumed by the game layer: move the pawn to the avatar item
    bool ShouldRenderAvatar() const;                     // true while an engine animation (fire/reload/death) drives the avatar sprite
    void OnAvatarDied();
    void RespawnAvatar();
    FString AvatarStatus() const;                        // HUD line
    FIntVector StartPos = FIntVector::ZeroValue;
    uint32 AvatarDeathTick = 0;
    bool bDeathSequenceStarted = false;
    bool bAvatarRespawned = false;                       // consumed by the game layer (teleports the pawn)
    bool bSGA1Loaded = false;
    bool SetAvatarWeapon(uint32 Shape);                  // cheat: add the weapon to the inventory and select it
    bool NextWeapon(int32 Delta = 1);                    // cycle the active weapon through the inventory (Delta < 0: previous)
    int32 AvatarHp() const;
    uint32 AvatarWeaponShape = 0;                        // shape of the active weapon (0 = none)
    bool bInfiniteAmmo = false;                          // cheat (Silencer.InfiniteAmmo 1): reloads without ammo, no energy drain
    ECruBattery BatteryType = ECruBattery::Chemical;     // MainActor::_cruBatteryType (new game: chemical, 2500 max energy)
    ProcId ShieldSpriteProc = 0;                         // MainActor::_shieldSpriteProc
    uint16 ShieldType = 0;                               // MainActor::_shieldType (0 none, 1 ionic, 2 plasma, 3 graviton)
    ObjId ActiveInvItem = 0;                             // MainActor::_activeInvItem
    int32 AvatarMaxHp = 150;
    uint32 NextFireTick = 0;

    FUsecodeFlex Usecode;
    FKernel* Kernel = nullptr;
    FUCMachine* Machine = nullptr;
    FCruWorld* World = nullptr;
    FIntVector CameraPos = FIntVector::ZeroValue;
    ObjId CameraFollow = Cru::kMainActorId;
    bool bAvatarInStasis = false, bCruStasis = false, bTeleporting = false;
    uint32 Keycards = 0;
    FString DataDir;
    int32 MapNum = 0;
    double Accum = 0.0;
    uint32 TicksRun = 0;
};

extern FCruEngine* GCruEngine;
