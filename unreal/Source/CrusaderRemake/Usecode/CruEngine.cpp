#include "CruEngine.h"
#include "Kernel.h"
#include "UCMachine.h"
#include "CruWorld.h"
#include "CruIntrinsics.h"
#include "CruCombat.h"
#include "CruInventory.h"
#include "CruPresentation.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "CruActors.h"

FCruEngine* GCruEngine = nullptr;
FCruCarryOver GCruCarryOver;

FCruEngine::FCruEngine() { GCruEngine = this; }

FCruEngine::~FCruEngine()
{
    delete Kernel; Kernel = nullptr;
    delete Machine; Machine = nullptr;
    delete World; World = nullptr;
    if (GCruEngine == this) GCruEngine = nullptr;
}

bool FCruEngine::Init(const FString& InDataDir)
{
    DataDir = InDataDir;
    if (!Usecode.Load(DataDir / TEXT("hd/USECODE/EUSECODE.FLX"))) return false;
    Kernel = new FKernel();
    uint32 Count = 0; const FCruIntrinsicEntry* Table = GetRemorseIntrinsics(Count);
    Machine = new FUCMachine(&Usecode, Table, Count);
    World = new FCruWorld();
    if (!World->LoadShapeInfos(DataDir / TEXT("extracted/typeflags.json"))) return false;
    World->LoadNpcTable(DataDir / TEXT("extracted/dtable.json"));
    World->AnimDat.Load(DataDir / TEXT("extracted/anim.json"));
    CruCombat::LoadDamageFlex(DataDir / TEXT("hd/STATIC/DAMAGE.FLX"));
    CruCombat::LoadCombatDat(DataDir / TEXT("hd/STATIC/COMBAT.DAT"));
    CruInventory::LoadEquipTypes(DataDir / TEXT("hd/STATIC/TYPEFLAG.DAT"));
    Reticle = new FTargetReticleProcess();
    Kernel->AddProcess(Reticle);
    return true;
}

int32 GCruDifficulty = 2;

bool FCruEngine::LoadMap(int32 InMapNum)
{
    MapNum = InMapNum;
    World->GameDifficulty = GCruDifficulty;
    // a checkpoint reload replays this level's start script (its cachein / hatch usecode runs while the map loads): no intro cutscene
    if (GCruCarryOver.bValid && GCruCarryOver.Egg < 0 && GCruCarryOver.Map == MapNum) SuppressMoviesUntilTick = Kernel->GetTickNum() + 300;
    return World->LoadMap(DataDir / FString::Printf(TEXT("extracted/maps/map%02d.json"), MapNum));
}

FCruItem* FCruEngine::SpawnAvatar(int32 X, int32 Y, int32 Z)
{
    // a checkpoint reload replays this level's start script (which runs inside EnterFastArea below): no intro cutscene
    FCruItem* Av = World->CreateAvatar(1, X, Y, Z);
    Av->MapNum = MapNum; Av->bActor = true;
    if (const FCruNpcDat* Npc = World->GetNpcDataForShape(1)) if (Npc->MaxHp) Av->Hp = Npc->MaxHp;
    AvatarMaxHp = Av->Hp; Av->Mana = CruInventory::GetMaxEnergy(); StartPos = FIntVector(X, Y, Z);
    // no default weapon: the mission start usecode gives the BA-40 and the other start items through I_addItemCru
    if (AvatarWeaponShape) SetAvatarWeapon(AvatarWeaponShape);
    CameraPos = FIntVector(X, Y, Z);
    World->UpdateFastArea(CameraPos, CameraPos);
    World->EnterFastArea(*Av);
    World->CallUsecodeEvent(*Av, Cru::EV_CACHEIN);   // Crusader calls cachein on the actors of the loaded map too; AVATAR::cachein starts the mission music
    int32 FastChunks = 0, FastItems = 0, FastEggs = 0;
    for (int32 Cy = 0; Cy < Cru::MAP_NUM_CHUNKS; ++Cy) for (int32 Cx = 0; Cx < Cru::MAP_NUM_CHUNKS; ++Cx) if (World->IsChunkFast(Cx, Cy)) ++FastChunks;
    for (auto& P : World->AllItems()) if (P.Value->Flags & Cru::FLG_FASTAREA) ++FastItems;
    for (ObjId E : World->Eggs) if (FCruItem* It = World->GetItem(E)) if (It->Flags & Cru::FLG_FASTAREA)
    {
        ++FastEggs;
        if (FastEggs <= 3) UE_LOG(LogCru, Display, TEXT("fast egg %d at (%d,%d,%d) q %d id %d range %dx%d class %d enterFast@%04X hatch@%04X"), It->Id, It->X, It->Y, It->Z, It->Quality, It->MapNum, It->EggXRange(), It->EggYRange(), 0x900 + It->Quality, Usecode.GetClassEvent(0x900 + It->Quality, 0xF), Usecode.GetClassEvent(0x900 + It->Quality, 7));
    }
    UE_LOG(LogCru, Display, TEXT("spawn avatar at (%d,%d,%d): %d fast chunks, %d fast items, %d fast eggs"), X, Y, Z, FastChunks, FastItems, FastEggs);
    return Av;
}

void FCruEngine::SetAvatarPosition(int32 X, int32 Y, int32 Z, int32 Dir16)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return;
    if (Dir16 >= 0) Av->Dir = Dir16 & 15;
    if (Av->X != X || Av->Y != Y || Av->Z != Z) World->Move(*Av, X, Y, Z);
}

void FCruEngine::Tick(float DeltaSeconds)
{
    // 60 Hz game ticks; after a hitch catch up by at most 4 ticks so scripted animations never burst
    Accum = FMath::Min(Accum + DeltaSeconds, 4.0 / 60.0);
    while (Accum >= 1.0 / 60.0) { Accum -= 1.0 / 60.0; RunOneTick(); }
}

void FCruEngine::RunOneTick()
{
    if (CameraFollow) if (FCruItem* F = World->GetItem(CameraFollow)) CameraPos = FIntVector(F->X, F->Y, F->Z);
    World->UpdateFastArea(CameraPos, CameraPos);
    if (bMoviePlaying) { ++TicksRun; return; }   // a cutscene is modal in the original
    World->RunEggHatcher();
    Kernel->RunProcesses();
    if (AvatarDeathTick)
    {
        // AvatarDeathProcess: "Silencer terminated" (sfx 9) two seconds after the fall, then the menu; we fade out,
        // show the text and restart the mission at its start point
        const uint32 T = Kernel->GetTickNum() - AvatarDeathTick;
        if (T >= 120 && !bDeathSequenceStarted) { bDeathSequenceStarted = true; World->SfxQueue.Add({ 0, 9, false, false }); StartFade(1.f, 120); bShowDeathText = true; }
        if (T > 360) { bDeathSequenceStarted = false; bShowDeathText = false; RespawnAvatar(); StartFade(0.f, 40); }
    }
    World->FlushDestroyed();
    ++TicksRun;
}

bool FCruEngine::IsOnScreen(const FCruItem& It) const
{
    // screen-space test against the original 640x480 view centred on the camera
    const int32 Sx = (It.X - It.Y) / 4 - (CameraPos.X - CameraPos.Y) / 4;
    const int32 Sy = (It.X + It.Y) / 8 - It.Z - ((CameraPos.X + CameraPos.Y) / 8 - CameraPos.Z);
    return FMath::Abs(Sx) <= World->ScreenW / 2 + 64 && FMath::Abs(Sy) <= World->ScreenH / 2 + 64;
}

ObjId FCruEngine::FindUsableItem() const
{
    const FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return 0;
    const FIntVector A = World->GetCentre(*Av);
    const int32 Az = Av->Z;
    FUCList List(2);
    const uint8 Script[] = { 1, '$' };
    World->AreaSearch(List, Script, 2, 0x200, Av->X, Av->Y);
    for (uint32 i = 0; i < List.GetSize(); ++i)
    {
        const FCruItem* It = World->GetItem(List.GetUInt16(i));
        if (!It || It->Id == Av->Id || (It->Flags & Cru::FLG_HANGING)) continue;
        const FCruShapeInfo& Si = World->Info(*It);
        const uint8 F = Si.Family;
        if (!(It->Shape == 0x4ED || F == Cru::SF_CRUWEAPON || F == Cru::SF_CRUAMMO || F == Cru::SF_CRUBOMB || F == Cru::SF_CRUINVITEM || (Si.Flags & Cru::SI_CRU_SELECTABLE))) continue;
        const FIntVector C = World->GetCentre(*It);
        if (FMath::Abs(C.X - A.X) > 0x100 || FMath::Abs(C.Y - A.Y) > 0x100 || (It->Z - Az) >= 0x50 || (Az - It->Z) >= 0x18) continue;
        return It->Id;
    }
    return 0;
}

bool FCruEngine::UseNearest()
{
    const ObjId Id = FindUsableItem();
    FCruItem* It = Id ? World->GetItem(Id) : nullptr;
    if (!It) { World->SfxQueue.Add({ 0, 0xB0, false, false }); return false; }   // "beeboop" nothing to select
    if (World->Info(*It).Flags & Cru::SI_CRU_SELECTABLE)
    {
        UE_LOG(LogCru, Display, TEXT("use item %d (%s) at (%d,%d,%d)"), It->Id, *Usecode.GetClassName(It->Shape), It->X, It->Y, It->Z);
        World->CallUsecodeEvent(*It, Cru::EV_USE);
    }
    else
    {
        // ItemSelectionProcess::useSelectedItem: pickups are given to the avatar directly, not through the use event
        UE_LOG(LogCru, Display, TEXT("pick up item %d (%s) at (%d,%d,%d)"), It->Id, *CruInventory::ItemName(*It), It->X, It->Y, It->Z);
        CruInventory::AddItemCru(*It, true);
    }
    return true;
}

// Cheat (Silencer.Weapon SHAPE): put the weapon in the inventory (addItemCru rules) and make it the active one.
bool FCruEngine::SetAvatarWeapon(uint32 Shape)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    const FCruWeaponInfo* Wi = CruGetWeaponInfo(Shape);
    if (!Av || !Wi) return false;
    if (!CruInventory::GetFirstItemWithShape(*Av, Shape, true))
    {
        FCruItem* W = World->CreateItem(Shape, 0, 0, 0, 0, Av->MapNum, 0, true);
        if (!W) return false;
        World->MoveToEtherealVoid(*W);
        if (!CruInventory::AddItemCru(*W, false)) { World->DestroyItem(*W); return false; }
    }
    FCruItem* W = CruInventory::GetFirstItemWithShape(*Av, Shape, true);
    if (!W) return false;
    Av->ActiveWeapon = W->Id; AvatarWeaponShape = Shape;
    UE_LOG(LogCru, Display, TEXT("avatar weapon: %s (shape %d, fire type %d, clip %d)"), Wi->Name, Shape, Wi->DamageType, W->Quality);
    return true;
}

bool FCruEngine::NextWeapon(int32 Delta)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av || Av->bDead) return false;
    const ObjId Old = Av->ActiveWeapon;
    CruInventory::NextWeapon(*Av, Delta);
    const FCruItem* W = World->GetItem(Av->ActiveWeapon);
    AvatarWeaponShape = W ? W->Shape : 0;
    bSGA1Loaded = false;
    if (W) UE_LOG(LogCru, Display, TEXT("avatar weapon: %s (clip %d)"), *CruInventory::ItemName(*W), W->Quality);
    return Av->ActiveWeapon != Old;
}

// CruAvatarMoverProcess::tryAttack (without ammo/energy bookkeeping for now)
bool FCruEngine::AvatarFire()
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av || Av->bDead || CruActor::IsBusy(*Av)) return false;
    const FCruItem* W = World->GetItem(Av->ActiveWeapon);
    const FCruWeaponInfo* Wi = W ? CruGetWeaponInfo(W->Shape) : nullptr;
    if (!Wi) return false;
    if (Kernel->GetTickNum() < NextFireTick) return false;
    if (!CruActor::IsInCombat(*Av)) { ToggleCombat(); NextFireTick = Kernel->GetTickNum() + 10; return false; }   // first press: draw the weapon, no shot
    const bool bKneeling = (Av->ActorFlags & CruAnim::ACT_KNEELING) != 0;
    FCruItem* Wpn = World->GetItem(Av->ActiveWeapon);
    int32 ShotsLeft = 1;
    if (Wi->AmmoShape) ShotsLeft = Wpn->Quality; else if (Wi->EnergyUse) ShotsLeft = Av->Mana / Wi->EnergyUse;
    Av->LastAnim = -1;   // the pawn drives idle/walk, so make sure the engine anims start at their first frame
    if (bInfiniteAmmo && !ShotsLeft && !Wi->AmmoShape && Wi->EnergyUse) ShotsLeft = 1;   // cheat: energy weapons never run dry
    if (!ShotsLeft)
    {
        // reload from an ammo item in the inventory (CruAvatarMoverProcess::tryAttack, cru_avatar_mover_process.cpp:608-633)
        FCruItem* Ammo = Wi->AmmoShape ? CruInventory::GetFirstItemWithShape(*Av, Wi->AmmoShape, true) : nullptr;
        if (Ammo || (bInfiniteAmmo && Wi->AmmoShape))
        {
            if (Wpn->Shape == 0x332) bSGA1Loaded = true;   // SG-A1 is special, it reloads every shot
            Wpn->Quality = Wi->ClipSize;
            if (Ammo && !bInfiniteAmmo)
            {
                Ammo->Quality -= 1;
                if (Ammo->Quality == 0) World->DestroyItem(*Ammo);
            }
            UE_LOG(LogCru, Display, TEXT("avatar reload: %s clip %d, ammo left %d"), Wi->Name, Wpn->Quality, Ammo ? Ammo->Quality : -1);
            if (Wi->ReloadSound) World->SfxQueue.Add({ Av->Id, 0x2a, false, false });
            if (!bKneeling) CruActor::DoAnim(*Av, CruAnim::ReloadSmall, CruAnim::DIR_CURRENT);
            NextFireTick = Kernel->GetTickNum() + 15;
        }
        else
        {
            // no shots left (no ammo for this weapon, or out of energy)
            UE_LOG(LogCru, Verbose, TEXT("avatar: no ammo / energy for %s"), Wi->Name);
            World->SfxQueue.Add({ Av->Id, 0x2a, false, false });
            NextFireTick = Kernel->GetTickNum() + 20;
        }
        return false;
    }
    if (Wpn->Shape == 0x332 && !bKneeling && !bSGA1Loaded)
    {
        // the SG-A1 pumps before every shot
        if (Wi->ReloadSound) World->SfxQueue.Add({ Av->Id, 0x2a, false, false });
        CruActor::DoAnim(*Av, CruAnim::ReloadSmall, CruAnim::DIR_CURRENT);
        bSGA1Loaded = true;
        NextFireTick = Kernel->GetTickNum() + 15;
        return false;
    }
    const uint32 FireAnim = bKneeling ? (Wi->bSmall ? 42 : 43) : (Wi->bSmall ? CruAnim::FireSmall : CruAnim::FireLarge);
    const ProcId Pid = CruActor::DoAnim(*Av, FireAnim, Av->Dir);
    if (!Pid) return false;
    if (Wpn->Shape == 0x332) bSGA1Loaded = false;
    if (Wi->AmmoShape) Wpn->Quality = ShotsLeft - 1; else if (Wi->EnergyUse && !bInfiniteAmmo) Av->Mana = FMath::Max(0, Av->Mana - Wi->EnergyUse);
    CruCombat::CheckForAlertingNPCs();
    NextFireTick = Kernel->GetTickNum() + (Wi->ShotDelay ? Wi->ShotDelay : 10);
    return true;
}

bool FCruEngine::ShouldRenderAvatar() const
{
    const FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return false;
    if (Av->bDead || (Av->ActorFlags & CruAnim::ACT_KNEELING)) return true;
    // any running animation process (usecode walk-ins, fire, rolls...) owns the avatar; the pawn follows the item meanwhile
    if (CruActor::IsBusy(*Av)) return true;
    return bAvatarStanceIdle && (Av->ActorFlags & CruAnim::ACT_INCOMBAT);
}

// MainActor::die: the avatar falls, then (5 s later) the original shows the death screen; we respawn at the start.
void FCruEngine::OnAvatarDied() { AvatarDeathTick = Kernel->GetTickNum(); bAvatarInStasis = true; bDeathSequenceStarted = false; }

void FCruEngine::RespawnAvatar()
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return;
    Kernel->KillProcesses(Av->Id, FKernel::PROC_TYPE_ALL, true);
    Av->bDead = false; Av->ActorFlags &= ~(CruAnim::ACT_DEAD | CruAnim::ACT_INCOMBAT | CruAnim::ACT_ANIMLOCK | CruAnim::ACT_KNEELING | CruAnim::ACT_WEAPONREADY);
    World->SetFlags(*Av, Av->Flags & ~(Cru::FLG_BROKEN | Cru::FLG_FLIPPED));
    Av->Hp = AvatarMaxHp; Av->Mana = CruInventory::GetMaxEnergy(); Av->LastAnim = -1; Av->Frame = 0;
    if (Av->Shape != 1) World->SetShape(*Av, 1);
    World->Move(*Av, StartPos.X, StartPos.Y, StartPos.Z);
    bAvatarInStasis = false; AvatarDeathTick = 0; bAvatarRespawned = true;
    UE_LOG(LogCru, Display, TEXT("avatar respawned at (%d,%d,%d)"), StartPos.X, StartPos.Y, StartPos.Z);
}

FString FCruEngine::AvatarStatus() const
{
    const FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return FString();
    const FCruItem* W = World->GetItem(Av->ActiveWeapon);
    const FCruWeaponInfo* Wi = W ? CruGetWeaponInfo(W->Shape) : nullptr;
    FString Wpn = Wi ? FString(Wi->Name) : TEXT("-");
    if (Wi && Wi->AmmoShape) Wpn += FString::Printf(TEXT(" %d/%d"), W->Quality, Wi->ClipSize);
    return FString::Printf(TEXT("HP %d/%d  Energy %d/%d  %s%s%s  |  %s"), Av->Hp, AvatarMaxHp, Av->Mana, CruInventory::GetMaxEnergy(), *Wpn,
        Av->bDead ? TEXT("  DEAD") : TEXT(""), bInfiniteAmmo ? TEXT("  [inf ammo]") : TEXT(""), *CruInventory::Summary(*Av));
}

int32 FCruEngine::AvatarHp() const { const FCruItem* Av = World->GetItem(World->AvatarId); return Av ? Av->Hp : 0; }

void FCruEngine::TeleportAvatar(int32 X, int32 Y, int32 Z)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return;
    Kernel->KillProcesses(Av->Id, FKernel::PROC_TYPE_ALL, true);
    World->Move(*Av, X, Y, Z);
    CameraPos = FIntVector(X, Y, Z);
    World->UpdateFastArea(CameraPos, CameraPos);
    World->bJustTeleported = true;
    bPendingPawnTeleport = true;
    if (Reticle) Reticle->AvatarMoved();
    UE_LOG(LogCru, Display, TEXT("avatar teleported to (%d,%d,%d)"), X, Y, Z);
}

bool FCruEngine::TeleportToEgg(int32 Map, int32 EggId)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return false;
    if (Map == 0) Map = MapNum;
    if (Map == MapNum)
    {
        for (const auto& P : World->AllItems())
        {
            const FCruItem* It = P.Value;
            if (World->Info(*It).Family == Cru::SF_TELEPORTEGG && It->Frame == 1 && (It->Quality & 0xff) == EggId)
            {
                TeleportAvatar(It->X, It->Y, It->Z);
                // the mission 1 start teleporter (egg 30) goes straight to another teleport (egg 99): allow the chain
                if (Map == 1 && EggId == 30) World->bJustTeleported = false;
                return true;
            }
        }
        // no such egg: fall back to the lowest-numbered destination egg (the mission start egg is normally 30)
        const FCruItem* Best = nullptr;
        for (const auto& P : World->AllItems())
        {
            const FCruItem* It = P.Value;
            if (World->Info(*It).Family == Cru::SF_TELEPORTEGG && It->Frame == 1 && (!Best || (It->Quality & 0xff) < (Best->Quality & 0xff))) Best = It;
        }
        if (Best) { UE_LOG(LogCru, Warning, TEXT("teleportToEgg: no destination egg %d on map %d, using egg %d"), EggId, Map, Best->Quality & 0xff); TeleportAvatar(Best->X, Best->Y, Best->Z); return true; }
        UE_LOG(LogCru, Warning, TEXT("teleportToEgg: no destination egg %d on map %d"), EggId, Map);
        return false;
    }
    // cross-map: snapshot the avatar and let the game layer load the other level
    FCruCarryOver& C = GCruCarryOver;
    SnapshotAvatar(C);
    C.Map = Map; C.Egg = EggId;
    PendingMapSwitch = Map;
    bCruStasis = true;
    UE_LOG(LogCru, Display, TEXT("teleportToEgg: switching to map %d egg %d with %d inventory items"), Map, EggId, C.Items.Num());
    return true;
}

void FCruEngine::RestoreCarryOver()
{
    FCruCarryOver& C = GCruCarryOver;
    if (!C.bValid || C.Map != MapNum) return;
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return;
    Av->Hp = C.Hp; Av->Mana = C.Mana; Keycards = C.Keycards; ShieldType = C.ShieldType; BatteryType = (ECruBattery)C.BatteryType;
    for (const FCruCarryOver::FEntry& E : C.Items)
        if (FCruItem* It = World->CreateItem(E.Shape, E.Frame, E.Quality, 0, 0, MapNum, 0, true))
        {
            CruInventory::MoveToContainer(*It, *Av);
            if (E.Shape == C.ActiveWeaponShape) Av->ActiveWeapon = It->Id;
        }
    if (C.bTestLoadout && !World->GetItem(Av->ActiveWeapon)) SetAvatarWeapon(0x32E);
    if (!ActiveInvItem) CruInventory::NextInvItem(*Av);
    UE_LOG(LogCru, Display, TEXT("carry-over restored on map %d: hp %d energy %d %d items"), MapNum, Av->Hp, Av->Mana, C.Items.Num());
    const int32 Egg = C.Egg; const FIntVector Pos = C.Pos; const int32 Dir = C.Dir;
    C.bValid = false;
    if (Egg >= 0) TeleportToEgg(MapNum, Egg);
    else { TeleportAvatar(Pos.X, Pos.Y, Pos.Z); if (Dir >= 0) Av->Dir = Dir; SuppressMoviesUntilTick = Kernel->GetTickNum() + 300; }
}

void FCruEngine::StartMission1()
{
    if (MapNum != 1) return;
    TeleportToEgg(1, 30);
    for (const auto& P : World->AllItems())
    {
        const FCruItem* It = P.Value;
        if (World->Info(*It).Family == Cru::SF_TELEPORTEGG && It->Frame == 1 && (It->Quality & 0xff) == 99) { StartPos = FIntVector(It->X, It->Y, It->Z); break; }
    }
    UE_LOG(LogCru, Display, TEXT("mission 1 start: teleport(1, 0x1e), respawn point (%d,%d,%d)"), StartPos.X, StartPos.Y, StartPos.Z);
}

void FCruEngine::StartMissionAt(int32 Map)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return;
    if (Map == MapNum)
    {
        TeleportToEgg(Map, 30);
        if (!World->GetItem(Av->ActiveWeapon)) SetAvatarWeapon(0x32E);
        return;
    }
    FCruCarryOver& C = GCruCarryOver;
    SnapshotAvatar(C);
    C.Map = Map; C.Egg = 30; C.bTestLoadout = true;
    PendingMapSwitch = Map;
    bCruStasis = true;
    UE_LOG(LogCru, Display, TEXT("mission picker: switching to map %d"), Map);
}

void FCruEngine::SnapshotAvatar(FCruCarryOver& C)
{
    C = FCruCarryOver();
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av) return;
    C.bValid = true; C.Map = MapNum; C.Egg = -1; C.Pos = FIntVector(Av->X, Av->Y, Av->Z); C.Dir = Av->Dir;
    C.Hp = Av->Hp; C.Mana = Av->Mana; C.Keycards = Keycards; C.ShieldType = ShieldType; C.BatteryType = (uint8)BatteryType;
    if (const FCruItem* W = World->GetItem(Av->ActiveWeapon)) C.ActiveWeaponShape = W->Shape;
    for (ObjId Id : Av->Contents) if (const FCruItem* It = World->GetItem(Id)) C.Items.Add({ It->Shape, It->Frame, It->Quality });
}

bool FCruEngine::SaveCheckpoint(const FString& Path)
{
    FCruCarryOver C; SnapshotAvatar(C);
    if (!C.bValid) return false;
    TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("map"), C.Map); J->SetNumberField(TEXT("x"), C.Pos.X); J->SetNumberField(TEXT("y"), C.Pos.Y); J->SetNumberField(TEXT("z"), C.Pos.Z); J->SetNumberField(TEXT("dir"), C.Dir);
    J->SetNumberField(TEXT("hp"), C.Hp); J->SetNumberField(TEXT("energy"), C.Mana); J->SetNumberField(TEXT("keycards"), C.Keycards);
    J->SetNumberField(TEXT("shield"), C.ShieldType); J->SetNumberField(TEXT("battery"), C.BatteryType); J->SetNumberField(TEXT("weapon"), C.ActiveWeaponShape);
    J->SetNumberField(TEXT("difficulty"), World->GameDifficulty);
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FCruCarryOver::FEntry& E : C.Items)
    {
        TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
        I->SetNumberField(TEXT("shape"), E.Shape); I->SetNumberField(TEXT("frame"), E.Frame); I->SetNumberField(TEXT("q"), E.Quality);
        Items.Add(MakeShared<FJsonValueObject>(I));
    }
    J->SetArrayField(TEXT("items"), Items);
    FString Out; TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(J.ToSharedRef(), Wr);
    const bool bOk = FFileHelper::SaveStringToFile(Out, *Path);
    UE_LOG(LogCru, Display, TEXT("checkpoint %s: map %d at (%d,%d,%d), %d items"), bOk ? TEXT("saved") : TEXT("NOT saved"), C.Map, C.Pos.X, C.Pos.Y, C.Pos.Z, C.Items.Num());
    return bOk;
}

bool FCruEngine::LoadCheckpoint(const FString& Path)
{
    FString Txt; TSharedPtr<FJsonObject> J;
    if (!FFileHelper::LoadFileToString(Txt, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Txt), J) || !J.IsValid()) { UE_LOG(LogCru, Warning, TEXT("checkpoint: cannot read %s"), *Path); return false; }
    FCruCarryOver& C = GCruCarryOver; C = FCruCarryOver();
    C.bValid = true; C.Map = (int32)J->GetNumberField(TEXT("map")); C.Egg = -1;
    C.Pos = FIntVector((int32)J->GetNumberField(TEXT("x")), (int32)J->GetNumberField(TEXT("y")), (int32)J->GetNumberField(TEXT("z"))); C.Dir = (int32)J->GetNumberField(TEXT("dir"));
    C.Hp = (int32)J->GetNumberField(TEXT("hp")); C.Mana = (int32)J->GetNumberField(TEXT("energy")); C.Keycards = (uint32)J->GetNumberField(TEXT("keycards"));
    C.ShieldType = (uint16)J->GetNumberField(TEXT("shield")); C.BatteryType = (uint8)J->GetNumberField(TEXT("battery")); C.ActiveWeaponShape = (uint32)J->GetNumberField(TEXT("weapon"));
    if (J->HasField(TEXT("difficulty"))) { GCruDifficulty = FMath::Clamp((int32)J->GetNumberField(TEXT("difficulty")), 1, 4); World->GameDifficulty = GCruDifficulty; }
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (J->TryGetArrayField(TEXT("items"), Items) && Items)
        for (const TSharedPtr<FJsonValue>& V : *Items)
        {
            const TSharedPtr<FJsonObject>* I = nullptr;
            if (V->TryGetObject(I) && I) C.Items.Add({ (uint32)(*I)->GetNumberField(TEXT("shape")), (uint32)(*I)->GetNumberField(TEXT("frame")), (uint16)(*I)->GetNumberField(TEXT("q")) });
        }
    PendingMapSwitch = C.Map;
    bCruStasis = true;
    UE_LOG(LogCru, Display, TEXT("checkpoint loaded: map %d at (%d,%d,%d), %d items"), C.Map, C.Pos.X, C.Pos.Y, C.Pos.Z, C.Items.Num());
    return true;
}

void FCruEngine::ToggleKneel()
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av || Av->bDead || CruActor::IsBusy(*Av)) return;
    if (Av->ActorFlags & CruAnim::ACT_KNEELING) { Av->LastAnim = -1; CruActor::DoAnim(*Av, 41, Av->Dir); }   // kneelEndCru
    else if (CruActor::HasAnim(*Av, 40)) { Av->LastAnim = -1; CruActor::DoAnim(*Av, 40, Av->Dir); }         // kneelStartCru
}

void FCruEngine::ToggleCombat()
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av || Av->bDead || CruActor::IsBusy(*Av) || (Av->ActorFlags & CruAnim::ACT_KNEELING)) return;
    const bool bSmall = AvatarHasSmallWeapon();
    Av->LastAnim = -1;
    if (Av->ActorFlags & CruAnim::ACT_INCOMBAT)
    {
        CruActor::DoAnim(*Av, bSmall ? CruAnim::UnreadySmall : CruAnim::UnreadyLarge, Av->Dir);
        Av->ActorFlags &= ~(CruAnim::ACT_INCOMBAT | CruAnim::ACT_WEAPONREADY);
    }
    else
    {
        if (!World->GetItem(Av->ActiveWeapon)) return;   // nothing to draw
        Av->ActorFlags |= CruAnim::ACT_INCOMBAT;
        CruActor::DoAnim(*Av, bSmall ? CruAnim::ReadySmall : CruAnim::ReadyLarge, Av->Dir);
    }
}

bool FCruEngine::AvatarMove(uint32 Anim, bool bEnterCombat)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av || Av->bDead || CruActor::IsBusy(*Av) || bAvatarInStasis || !CruActor::HasAnim(*Av, Anim))
    {
        UE_LOG(LogCru, Display, TEXT("avatar move %u refused: %s%s%s%s"), Anim, !Av ? TEXT("no avatar ") : TEXT(""), Av && Av->bDead ? TEXT("dead ") : TEXT(""), Av && CruActor::IsBusy(*Av) ? TEXT("busy ") : TEXT(""), bAvatarInStasis ? TEXT("stasis ") : (Av && !CruActor::HasAnim(*Av, Anim) ? TEXT("no such anim") : TEXT("")));
        return false;
    }
    UE_LOG(LogCru, Display, TEXT("avatar move anim %u dir %d"), Anim, Av->Dir);
    if (bEnterCombat && !(Av->ActorFlags & CruAnim::ACT_INCOMBAT)) Av->ActorFlags |= CruAnim::ACT_INCOMBAT;
    Av->LastAnim = -1;
    return CruActor::DoAnim(*Av, Anim, Av->Dir) != 0;
}

bool FCruEngine::IsAvatarInCombat() const
{
    const FCruItem* Av = World->GetItem(World->AvatarId);
    return Av && !Av->bDead && (Av->ActorFlags & CruAnim::ACT_INCOMBAT) != 0;
}

bool FCruEngine::IsAvatarKneeling() const { const FCruItem* Av = World->GetItem(World->AvatarId); return Av && (Av->ActorFlags & CruAnim::ACT_KNEELING) != 0; }

bool FCruEngine::AvatarHasSmallWeapon() const
{
    const FCruItem* Av = World->GetItem(World->AvatarId);
    const FCruItem* W = Av ? World->GetItem(Av->ActiveWeapon) : nullptr;
    const FCruWeaponInfo* Wi = W ? CruGetWeaponInfo(W->Shape) : nullptr;
    return !Wi || Wi->bSmall;
}

void FCruEngine::SetAvatarStanceDir(int32 Dir)
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av || CruActor::IsBusy(*Av)) return;
    const uint32 Anim = (Av->ActorFlags & CruAnim::ACT_KNEELING) ? 46 : (AvatarHasSmallWeapon() ? CruAnim::CombatStandSmall : CruAnim::CombatStandLarge);
    if (const FCruAnimAction* Act = World->AnimDat.Get(Av->Shape, Anim))
    {
        const int32 UseDir = Act->DirCount == 16 ? Dir : Dir & ~1;
        const FCruAnimFrame& F = Act->GetFrame(UseDir, 0);
        Av->Dir = Dir;
        if (Av->Frame != (int32)F.Frame) World->SetFrame(*Av, F.Frame);
        else if (Av->Dir != Dir) World->Notify(*Av, ECruItemChange::Moved);
    }
}

// MainActor::useInventoryItem
void FCruEngine::UseActiveItem()
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    FCruItem* It = World->GetItem(ActiveInvItem);
    if (!Av || !It || bAvatarInStasis) return;
    const uint32 Shape = It->Shape;
    if (Shape == 0x4ed) return;   // credits
    UE_LOG(LogCru, Display, TEXT("avatar uses %s"), *CruInventory::ItemName(*It));
    World->CallUsecodeEvent(*It, Cru::EV_USE);
    if (!World->GetItem(It->Id)) { if (ActiveInvItem == It->Id) NextInvItem(); return; }
    if (Shape != 0x4d4 && Shape != 0x52d && Shape != 0x530 && Shape != 0x52f && Shape != 0x52e)
    {
        It->Quality = It->Quality - 1;
        World->CallUsecodeEvent(*It, Cru::EV_COMBINE);
        if (It->Quality == 0)
        {
            const ObjId Id = It->Id;
            World->DestroyItem(*It);
            if (Id == ActiveInvItem) NextInvItem();
        }
    }
}

void FCruEngine::NextInvItem()
{
    if (FCruItem* Av = World->GetItem(World->AvatarId)) CruInventory::NextInvItem(*Av);
}

ProcId FCruEngine::StartFade(float To, int32 Steps, FColor Color)
{
    if (FProcess* Old = Kernel->GetProcess(FadePid)) if (!Old->IsTerminated()) Old->Terminate();
    FadeColor = Color;
    if (Steps <= 0) { FadeAlpha = To; FadePid = 0; return 0; }
    FadePid = Kernel->AddProcess(new FFadeProcess(FadeAlpha, To, Steps));
    return FadePid;
}

ProcId FCruEngine::PlayMovie(const FString& Name)
{
    if (Kernel->GetTickNum() < SuppressMoviesUntilTick) { UE_LOG(LogCru, Display, TEXT("cutscene %s skipped (checkpoint reload)"), *Name); return 0; }
    PendingMovie = Name; bMoviePlaying = true;
    const ProcId Pid = Kernel->AddProcess(new FMovieProcess());
    UE_LOG(LogCru, Display, TEXT("cutscene requested: %s (process %d)"), *Name, Pid);
    return Pid;
}

void FCruEngine::MovieFinished() { bMoviePlaying = false; PendingMovie.Empty(); }

// CruPickupAreaGump::addPickup / CruPickupGump::updateForNewItem
void FCruEngine::AddPickup(const FCruItem& Item, bool bShowCount)
{
    const uint32 Now = Kernel->GetTickNum();
    Pickups.RemoveAll([&](const FPickupToast& T) { return Now - T.StartTick > 180; });
    for (FPickupToast& T : Pickups)
        if (T.Shape == Item.Shape)
        {
            T.bShowCount = true;
            if (Item.Shape == 0x4ed) T.Q += Item.Quality; else ++T.Q;
            return;
        }
    const bool bAmmo = World->Info(Item).Family == Cru::SF_CRUAMMO;
    Pickups.Add({ Item.Shape, Item.Frame, bAmmo ? 1 : (int32)Item.Quality, bShowCount, Now });
}

void FCruEngine::PlayMusic(int32 Track)
{
    if (Track < 0 || Track > 21) Track = 0;
    if (Track == 16) { Track += M16Offset; M16Offset = (M16Offset + 1) % 3; }   // loop through the three mission 16 parts
    if (Track == MusicTrack) return;
    MusicTrack = Track; bMusicChanged = true;
    UE_LOG(LogCru, Display, TEXT("music: track %d (%s)"), Track, *MusicTrackName());
}

FString FCruEngine::MusicTrackName() const
{
    static const TCHAR* Names[] = { TEXT(""), TEXT("M01"), TEXT("M02"), TEXT("M03"), TEXT("M04"), TEXT("M05"), TEXT("M06"), TEXT("M07"), TEXT("M08"), TEXT("M09"), TEXT("M10"),
        TEXT("M11"), TEXT("M12"), TEXT("M13"), TEXT("M14"), TEXT("M15"), TEXT("M16A"), TEXT("M16B"), TEXT("M16C"), TEXT("CRED"), TEXT("MENU"), TEXT("") };
    return (MusicTrack > 0 && MusicTrack < (int32)UE_ARRAY_COUNT(Names)) ? FString(Names[MusicTrack]) : FString();
}

void FCruEngine::OnTargetItemMoved(const FCruItem& It) { if (Reticle) Reticle->ItemMoved(It); }

void FCruEngine::AvatarWalked()
{
    FCruItem* Av = World->GetItem(World->AvatarId);
    if (!Av || !(Av->ActorFlags & CruAnim::ACT_INCOMBAT) || (Av->ActorFlags & CruAnim::ACT_KNEELING)) return;
    Av->ActorFlags &= ~(CruAnim::ACT_INCOMBAT | CruAnim::ACT_WEAPONREADY);
}
