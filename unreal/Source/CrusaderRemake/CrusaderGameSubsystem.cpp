#include "CrusaderGameSubsystem.h"
#include "CrusaderSpriteBatch.h"
#include "CrusaderLevelInfo.h"
#include "CrusaderWorld.h"
#include "SilencerPawn.h"
#include "CrusaderPlayerController.h"
#include "CrusaderSettings.h"
#include "PaperFlipbookComponent.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"
#include "Camera/CameraActor.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "MediaSoundComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "GameFramework/PlayerController.h"
FString GCruBatchMaterialOverride;
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2D.h"
#include "Usecode/CruEngine.h"
#include "Usecode/CruWorld.h"
#include "Usecode/CruActors.h"
#include "Usecode/Kernel.h"
#include "EngineUtils.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"

bool UCrusaderGameSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* W = Cast<UWorld>(Outer);
    return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

void UCrusaderGameSubsystem::Deinitialize()
{
    StopMovie();
    delete Engine; Engine = nullptr;
    Super::Deinitialize();
}

bool UCrusaderGameSubsystem::TryInit()
{
    UWorld* W = GetWorld();
    ACrusaderLevelInfo* Info = nullptr;
    { TActorIterator<ACrusaderLevelInfo> It(W); if (It) Info = *It; }
    ASilencerPawn* Pawn = Cast<ASilencerPawn>(UGameplayStatics::GetPlayerPawn(W, 0));
    if (!Info || !Pawn) return false;
    bInitTried = true;
    Engine = new FCruEngine();
    if (!Engine->Init(Info->DataDir) || !Placer.LoadAtlas(Info->AtlasJson)) { delete Engine; Engine = nullptr; return true; }
    // static instances per source item, from the builder's InstanceItem arrays
    for (TActorIterator<ACrusaderSpriteBatch> It(W); It; ++It)
    {
        ACrusaderSpriteBatch* B = *It;
        if (B->bCollision) { Collision = B; for (int32 i = 0; i < B->InstanceItem.Num(); ++i) if (B->InstanceItem[i] >= 0) CollisionInstances.Add(B->InstanceItem[i], i); continue; }
        for (int32 i = 0; i < B->InstanceItem.Num(); ++i) if (B->InstanceItem[i] >= 0) StaticInstances.FindOrAdd(B->InstanceItem[i]).Add(TPair<ACrusaderSpriteBatch*, int32>(B, i));
    }
    // one dynamic (movable) batch per atlas, using the builder's material instances MI_Map<NN>[x<K>]_Atlas<i>
    const FString Tag = FString::Printf(TEXT("%02d%s"), Info->MapNum, Placer.Scale > 1 ? *FString::Printf(TEXT("x%d"), Placer.Scale) : TEXT(""));
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    DynamicBatches.SetNum(Placer.NumAtlases);
    for (int32 Ai = 0; Ai < Placer.NumAtlases; ++Ai)
    {
        UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Crusader/Materials/MI_Map%s_Atlas%d.MI_Map%s_Atlas%d"), *Tag, Ai, *Tag, Ai));
        if (!Mat) { UE_LOG(LogCru, Warning, TEXT("game subsystem: no material for atlas %d (rebuild the level)"), Ai); continue; }
        FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ACrusaderSpriteBatch* D = W->SpawnActor<ACrusaderSpriteBatch>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
        D->Mesh->SetMobility(EComponentMobility::Movable);
        D->Mesh->SetStaticMesh(Plane);
        D->Mesh->SetMaterial(0, Mat);
#if WITH_EDITOR
        D->SetActorLabel(FString::Printf(TEXT("Sprites_Dynamic_Atlas%d"), Ai));
#endif
        DynamicBatches[Ai] = D;
    }
    Engine->World->OnItemChanged = [this](const FCruItem& It, ECruItemChange C) { Dirty.Add(It.Id, It.SourceIndex); };
    StartLocation = Pawn->GetActorLocation();
    if (!Engine->LoadMap(Info->MapNum)) { delete Engine; Engine = nullptr; return true; }
    const FVector L = Pawn->GetActorLocation();
    const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Engine->SpawnAvatar((int32)(L.X + 48.f), (int32)(L.Y + 48.f), (int32)FMath::RoundToInt((L.Z - Half) / FCruSpritePlacer::ZK));
    const bool bHadCarryOver = GCruCarryOver.bValid;
    Engine->RestoreCarryOver();
    if (!bHadCarryOver && Engine->MapNum == 1) Engine->StartMission1();   // fresh game: the original start, not the builder's player start
    UE_LOG(LogCru, Display, TEXT("game subsystem: engine running for map %d, %d static sources, %d collision boxes, %d dynamic batches"), Info->MapNum, StaticInstances.Num(), CollisionInstances.Num(), DynamicBatches.Num());
    return true;
}

bool GCruSkipMenuOnce = false;   // set by NewGame / mission starts so the reloaded level does not show the main menu

bool UCrusaderGameSubsystem::IsMenuOpen() const
{
    const ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0));
    return PC && PC->IsMenuOpen();
}

void UCrusaderGameSubsystem::NewGame()
{
    if (Engine && Engine->MapNum == 1 && Engine->Kernel->GetTickNum() < 30) { Engine->StartPlaying(); return; }   // fresh level: just start playing
    GCruCarryOver = FCruCarryOver();
    GCruSkipMenuOnce = true;
    UGameplayStatics::OpenLevel(GetWorld(), FName(TEXT("/Game/Crusader/Maps/Map01_3D_HD")));
}

void UCrusaderGameSubsystem::StartMission(int32 Mission)
{
    const TArray<int32>& Maps = ACrusaderPlayerController::MissionMaps();
    if (!Engine || Mission < 1 || Mission > Maps.Num()) return;
    GCruSkipMenuOnce = true;
    Engine->StartPlaying();
    Engine->StartMissionAt(Maps[Mission - 1]);
}

void UCrusaderGameSubsystem::SetDifficulty(int32 D)
{
    GCruDifficulty = FMath::Clamp(D, 1, 4);
    if (Engine) Engine->World->GameDifficulty = GCruDifficulty;
    UE_LOG(LogCru, Display, TEXT("difficulty %d"), GCruDifficulty);
}

void UCrusaderGameSubsystem::AvatarMove(int32 Anim, bool bEnterCombat) { if (Engine) Engine->AvatarMove((uint32)Anim, bEnterCombat); }

void UCrusaderGameSubsystem::OnMenuOpened()
{
    if (!Engine || SavedMusicTrack >= 0) return;
    SavedMusicTrack = Engine->MusicTrack;
    Engine->PlayMusic(20);
}

void UCrusaderGameSubsystem::OnMenuClosed()
{
    if (!Engine || SavedMusicTrack < 0) return;
    const int32 T = SavedMusicTrack; SavedMusicTrack = -1;
    Engine->PlayMusic(T);
}

bool UCrusaderGameSubsystem::LoadCredits()
{
    if (Credits.Num()) return true;
    TArray<uint8> Data;
    const FString Path = FPaths::Combine(Engine->DataDir, TEXT("cd/STATIC/CREDITS.DAT"));
    if (!FFileHelper::LoadFileToArray(Data, *Path)) return false;
    FCredScreen Screen;
    int32 i = 4;   // unknown header
    while (i < Data.Num())
    {
        FString Line;
        while (i < Data.Num() && Data[i] != 0) Line.AppendChar((TCHAR)Data[i++]);
        ++i;
        if (Line.IsEmpty()) break;
        const TCHAR C = Line[0];
        if (C == '@') Screen.Lines.Add({ true, Line.Mid(1) });
        else if (C == '$') Screen.Lines.Add({ false, Line.Mid(1) });
        else if (C == '*') { int32 N = 0; while (N < Line.Len() && Line[N] == '*') ++N; Screen.Delay = 60 * N; Credits.Add(Screen); Screen = FCredScreen(); }
    }
    return Credits.Num() > 0;
}

void UCrusaderGameSubsystem::ApplyAudioSettings()
{
    const FCruSettings& S = FCruSettings::Get();
    if (Music) Music->SetVolumeMultiplier(S.MusicScale());
    if (MovieSound) if (UAudioComponent* AC = MovieSound->GetAudioComponent()) AC->SetVolumeMultiplier(S.SfxScale());
}

void UCrusaderGameSubsystem::Tick(float DeltaTime)
{
    if (!Engine)
    {
        if (!bInitTried)
        {
            const bool bFresh = !GCruCarryOver.bValid && !GCruSkipMenuOnce;
            GCruSkipMenuOnce = false;
            TryInit();
            if (Engine && bFresh) StartupStep = 1;   // intro movies, then the original menu
            else if (Engine) Engine->StartPlaying();
        }
        if (!Engine) return;
    }
    // start of a fresh game: T01 and T02 intro movies (fire / Esc skip), then the original menu (StartCrusaderProcess)
    if (StartupStep > 0)
    {
        UpdateMovie(DeltaTime);
        if (IsMoviePlaying()) return;
        if (StartupStep == 1) { StartupStep = 2; if (PlayMovie(TEXT("T01"))) return; }
        if (StartupStep == 2) { StartupStep = 3; if (PlayMovie(TEXT("T02"))) return; }
        if (ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0))) { StartupStep = 0; PC->OpenMenu(ECruMenu::Main); }
        return;
    }
    if (bWantMainMenu)
        if (ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(), 0))) { bWantMainMenu = false; PC->OpenMenu(ECruMenu::Main); }
    if (IsMenuOpen())
    {
        // menus pause the usecode engine (and a running cutscene); the menu music keeps playing
        if (MoviePlayer && !bMoviePausedByMenu && MoviePlayer->IsPlaying()) { MoviePlayer->Pause(); bMoviePausedByMenu = true; }
        UpdateMusic();
        return;
    }
    if (bMoviePausedByMenu) { bMoviePausedByMenu = false; if (MoviePlayer) MoviePlayer->Play(); }
    if (ASilencerPawn* Pawn = Cast<ASilencerPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
    {
        const FVector L = Pawn->GetActorLocation();
        const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (Engine->bPendingPawnTeleport)
        {
            // the engine moved the avatar (teleport egg / checkpoint): move the pawn there before syncing the other way
            Engine->bPendingPawnTeleport = false;
            if (const FCruItem* Av = Engine->World->GetItem(Engine->World->AvatarId))
                Pawn->SetActorLocation(FVector(Av->X - 48.f, Av->Y - 48.f, Av->Z * FCruSpritePlacer::ZK + Half + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
        }
        else if (!bAvatarEngineAnim) Engine->SetAvatarPosition((int32)(L.X + 48.f), (int32)(L.Y + 48.f), (int32)FMath::RoundToInt((L.Z - Half) / FCruSpritePlacer::ZK), Pawn->GetFacingDir16());
        Engine->bAimValid = Pawn->bMouseAim;
        Engine->AimPoint = FIntVector((int32)(Pawn->AimWorld.X + 48.f), (int32)(Pawn->AimWorld.Y + 48.f), (int32)FMath::RoundToInt((L.Z - Half) / FCruSpritePlacer::ZK));
        if (Pawn->bMoving) Engine->AvatarWalked();
        // combat stance: the pawn turns / retreats, the engine shows the stance frames while standing still
        Pawn->bCombatStance = Engine->IsAvatarInCombat();
        Pawn->bLargeWeapon = !Engine->AvatarHasSmallWeapon();
        Engine->bAvatarStanceIdle = Pawn->bCombatStance && !Pawn->bMoving && !Pawn->bRetreating;
        if (Engine->bAvatarStanceIdle && !bAvatarEngineAnim) Engine->SetAvatarStanceDir(Pawn->GetFacingDir16());
    }
    Engine->Tick(DeltaTime);
    if (ASilencerPawn* Pawn = Cast<ASilencerPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
    {
        // while the engine animates the avatar (fire, reload, death) its sprite is drawn from the item like any NPC
        const bool bEngineAnim = Engine->ShouldRenderAvatar();
        if (bEngineAnim != bAvatarEngineAnim)
        {
            bAvatarEngineAnim = bEngineAnim;
            UE_LOG(LogCru, Display, TEXT("avatar sprite: %s"), bEngineAnim ? TEXT("engine animation") : TEXT("pawn flipbook"));
            Pawn->bEngineAnim = bEngineAnim;
            Pawn->GetSprite()->SetVisibility(!bEngineAnim);
            Dirty.Add(Cru::kMainActorId, -1);
        }
        if (bAvatarEngineAnim)
        {
            // the engine owns the avatar while an animation runs (rolls, slides, jumps move the item): the pawn follows it
            if (const FCruItem* Av = Engine->World->GetItem(Engine->World->AvatarId); Av && CruActor::IsBusy(*Av))
            {
                const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                const FVector Want(Av->X - 48.f, Av->Y - 48.f, Av->Z * FCruSpritePlacer::ZK + Half + 2.f);
                if (FVector::DistSquared2D(Want, Pawn->GetActorLocation()) > 4.f || FMath::Abs(Want.Z - Pawn->GetActorLocation().Z) > 4.f)
                {
                    UE_LOG(LogCru, Verbose, TEXT("avatar moved by its animation to (%d,%d,%d)"), Av->X, Av->Y, Av->Z);
                    Pawn->SetActorLocation(Want, false, nullptr, ETeleportType::TeleportPhysics);
                }
            }
        }
        if (Engine->PendingMapSwitch)
        {
            const int32 Map = Engine->PendingMapSwitch; Engine->PendingMapSwitch = 0;
            UE_LOG(LogCru, Display, TEXT("game subsystem: opening level for map %d"), Map);
            UGameplayStatics::OpenLevel(GetWorld(), FName(*FString::Printf(TEXT("/Game/Crusader/Maps/Map%02d_3D_HD"), Map)));
            return;
        }
        if (Engine->bAvatarRespawned)
        {
            Engine->bAvatarRespawned = false;
            if (const FCruItem* Av = Engine->World->GetItem(Engine->World->AvatarId))
            {
                const float Half = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                Pawn->SetActorLocation(FVector(Av->X - 48.f, Av->Y - 48.f, Av->Z * FCruSpritePlacer::ZK + Half + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
            }
        }
        UpdateMusic();
        // cutscenes requested by usecode
        if (!Engine->PendingMovie.IsEmpty() && !MoviePlayer)
        {
            const FString Name = Engine->PendingMovie; Engine->PendingMovie.Empty();
            if (!PlayMovie(Name)) Engine->MovieFinished();
        }
        UpdateMovie(DeltaTime);
        DiagTimer += DeltaTime;
        if (!bDiagDone && DiagTimer > 3.f)
        {
            bDiagDone = true;
            if (!GCruBatchMaterialOverride.IsEmpty())
            {
                UMaterialInterface* Ov = nullptr;
                if (GCruBatchMaterialOverride == TEXT("map1tex2") || GCruBatchMaterialOverride == TEXT("dyn2"))
                {
                    const bool bMap1 = GCruBatchMaterialOverride == TEXT("map1tex2");
                    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, bMap1 ? TEXT("/Game/Crusader/Materials/MI_Map01x2_Atlas0.MI_Map01x2_Atlas0") : TEXT("/Game/Crusader/Materials/MI_Map02x2_Atlas0.MI_Map02x2_Atlas0"));
                    UTexture* T = LoadObject<UTexture>(nullptr, TEXT("/Game/Crusader/Materials/Atlas/T_Map02x2_Atlas0.T_Map02x2_Atlas0"));
                    UMaterialInstanceDynamic* Mid = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
                    if (Mid && T) Mid->SetTextureParameterValue(TEXT("Atlas"), T);
                    Ov = Mid;
                    if (UTexture2D* T2 = Cast<UTexture2D>(T))
                        UE_LOG(LogCru, Display, TEXT("batch diag: tex2 rhi %d platformData %d mips %d loaded %d streaming %d"), (T2->GetResource() && T2->GetResource()->TextureRHI.IsValid()) ? 1 : 0, T2->GetPlatformData() ? 1 : 0, T2->GetPlatformData() ? T2->GetPlatformData()->Mips.Num() : -1, (T2->GetPlatformData() && T2->GetPlatformData()->Mips.Num()) ? (T2->GetPlatformData()->Mips[0].BulkData.IsBulkDataLoaded() ? 1 : 0) : -1, T2->IsStreamable() ? 1 : 0);
                }
                else Ov = LoadObject<UMaterialInterface>(nullptr, *GCruBatchMaterialOverride);
                int32 Applied = 0;
                for (TActorIterator<ACrusaderSpriteBatch> It(GetWorld()); It; ++It) if (It->Mesh && !It->bCollision && Ov) { It->Mesh->SetMaterial(0, Ov); It->Mesh->MarkRenderStateDirty(); ++Applied; }
                UE_LOG(LogCru, Display, TEXT("batch diag: material override %s applied to %d batches (%s)"), *GCruBatchMaterialOverride, Applied, Ov ? TEXT("loaded") : TEXT("NOT FOUND"));
            }
            int32 Nb = 0;
            for (TActorIterator<ACrusaderSpriteBatch> It(GetWorld()); It; ++It)
            {
                ACrusaderSpriteBatch* B = *It; if (!B->Mesh) continue; ++Nb;
                FString TexInfo = TEXT("none");
                if (UMaterialInstance* MI = Cast<UMaterialInstance>(B->Mesh->GetMaterial(0)))
                {
                    UTexture* T = nullptr; MI->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Atlas")), T);
                    if (UTexture2D* T2 = Cast<UTexture2D>(T))
                        TexInfo = FString::Printf(TEXT("%s compiling %d default %d resource %d pendingInit %d %dx%d mips %d matCompiled %d"), *T2->GetName(), T2->IsCompiling() ? 1 : 0, T2->GetResource() == nullptr ? 1 : 0, T2->GetResource() ? 1 : 0, T2->HasPendingInitOrStreaming() ? 1 : 0, T2->GetSizeX(), T2->GetSizeY(), T2->GetNumMips(), MI->IsComplete() ? 1 : 0);
                }
                int32 Vis = 0; const int32 Nf = B->Mesh->NumCustomDataFloats;
                if (Nf == 5) for (int32 i = 0; i + 4 < B->Mesh->PerInstanceSMCustomData.Num(); i += 5) if (B->Mesh->PerInstanceSMCustomData[i + 4] > 0.5f) ++Vis;
                UE_LOG(LogCru, Display, TEXT("batch diag: %s vis %d/%d roofLayer %d appliedZLimit %.1f | mesh %s hiddenInGame %d mainPass %d ownerSee %d/%d maxDraw %.0f/%.0f cullVol %d occluder %d castShadow %d lod %d-%d detail %d vis %d actorHidden %d actorTick %d"),
                    *B->GetName(), Vis, B->Mesh->GetInstanceCount(), B->bRoofLayer ? 1 : 0, B->GetAppliedZLimit(),
                    B->Mesh->GetStaticMesh() ? *B->Mesh->GetStaticMesh()->GetName() : TEXT("NONE"), B->Mesh->bHiddenInGame ? 1 : 0, B->Mesh->bRenderInMainPass ? 1 : 0, B->Mesh->bOnlyOwnerSee ? 1 : 0, B->Mesh->bOwnerNoSee ? 1 : 0,
                    B->Mesh->LDMaxDrawDistance, B->Mesh->CachedMaxDrawDistance, B->Mesh->bAllowCullDistanceVolume ? 1 : 0, B->Mesh->bUseAsOccluder ? 1 : 0, B->Mesh->CastShadow ? 1 : 0, B->Mesh->MinLOD, B->Mesh->ForcedLodModel, (int32)B->Mesh->DetailMode, B->Mesh->GetVisibleFlag() ? 1 : 0, B->IsHidden() ? 1 : 0, B->IsActorTickEnabled() ? 1 : 0);
                if (Nb <= 6) UE_LOG(LogCru, Display, TEXT("batch diag: %s instances %d visible %d treeBuilt %d rendered %d mat %s tex %s"), *B->GetName(), B->Mesh->GetInstanceCount(), B->Mesh->IsVisible() ? 1 : 0, B->Mesh->IsTreeFullyBuilt() ? 1 : 0, B->Mesh->WasRecentlyRendered(1.f) ? 1 : 0, B->Mesh->GetMaterial(0) ? *B->Mesh->GetMaterial(0)->GetName() : TEXT("none"), *TexInfo);
            }
            APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
            UE_LOG(LogCru, Display, TEXT("batch diag: %d batches, pawn at %s, view target %s"), Nb, *Pawn->GetActorLocation().ToString(), PC && PC->GetViewTarget() ? *PC->GetViewTarget()->GetName() : TEXT("none"));
            if (PC && PC->PlayerCameraManager)
            {
                const FMinimalViewInfo& V = PC->PlayerCameraManager->GetCameraCacheView();
                UE_LOG(LogCru, Display, TEXT("batch diag: view projection %s orthoWidth %.0f near %.0f far %.0f fov %.0f loc %s rot %s; follow camera %s"), V.ProjectionMode == ECameraProjectionMode::Orthographic ? TEXT("ortho") : TEXT("perspective"), V.OrthoWidth, V.OrthoNearClipPlane, V.OrthoFarClipPlane, V.FOV, *V.Location.ToString(), *V.Rotation.ToString(), Pawn->FollowCamera ? *Pawn->FollowCamera->GetName() : TEXT("none"));
            }
        }
    }
    ApplyChanges();
    PlaySfx();
}

bool UCrusaderGameSubsystem::UseNearest() { return Engine ? Engine->UseNearest() : false; }
bool UCrusaderGameSubsystem::Fire() { return Engine ? Engine->AvatarFire() : false; }
bool UCrusaderGameSubsystem::TeleportToEgg(int32 Map, int32 Egg) { return Engine ? Engine->TeleportToEgg(Map, Egg) : false; }
void UCrusaderGameSubsystem::ToggleKneel() { if (Engine) Engine->ToggleKneel(); }
void UCrusaderGameSubsystem::ToggleCombat() { if (Engine) Engine->ToggleCombat(); }
static FString SavePath(int32 Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("crusader_slot%d.json"), Slot); }
bool UCrusaderGameSubsystem::SaveGame(int32 Slot) { return Engine ? Engine->SaveCheckpoint(SavePath(Slot)) : false; }
bool UCrusaderGameSubsystem::LoadGame(int32 Slot) { return Engine ? Engine->LoadCheckpoint(SavePath(Slot)) : false; }
void UCrusaderGameSubsystem::UseItem() { if (Engine) Engine->UseActiveItem(); }
void UCrusaderGameSubsystem::NextItem() { if (Engine) Engine->NextInvItem(); }

// Cutscenes: the original FLICS/<NAME>.AVI, extracted to data/extracted/video/<NAME>.mp4 (tools/video.py).
// Usecode names "mva1" / "mva3a" mean MVA01 / MVA03A (ScummVM _fixCrusaderMovieName); "01a" may also be "001a".
bool UCrusaderGameSubsystem::PlayMovie(const FString& InName)
{
    if (!Engine) return false;
    FString Name = InName.ToUpper();
    if (Name.StartsWith(TEXT("MVA")) && Name.Len() == 4) Name = FString::Printf(TEXT("MVA0%c"), Name[3]);
    else if (Name == TEXT("MVA3A")) Name = TEXT("MVA03A");
    else if (Name == TEXT("MVA5A")) Name = TEXT("MVA05A");
    const FString Dir = Engine->DataDir / TEXT("extracted/video");
    FString Path = Dir / Name + TEXT(".mp4");
    if (!FPaths::FileExists(Path)) Path = Dir / TEXT("0") + Name + TEXT(".mp4");
    if (!FPaths::FileExists(Path)) { UE_LOG(LogCru, Warning, TEXT("cutscene %s: no video file (%s)"), *InName, *Path); return false; }
    { const bool bKeep = bIntroMovie; bIntroMovie = true; StopMovie(); bIntroMovie = bKeep; }   // replacing a movie must not report a usecode cutscene as finished
    MoviePlayer = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
    MoviePlayer->PlayOnOpen = true;
    MovieTexture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);
    MovieTexture->SetMediaPlayer(MoviePlayer);
    MovieTexture->UpdateResource();
    MovieSource = NewObject<UFileMediaSource>(this, NAME_None, RF_Transient);
    MovieSource->SetFilePath(Path);
    if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
    {
        MovieSound = NewObject<UMediaSoundComponent>(Pawn, NAME_None, RF_Transient);
        MovieSound->SetMediaPlayer(MoviePlayer);
        MovieSound->RegisterComponent();
    }
    MovieTime = 0.f;
    if (!MoviePlayer->OpenSource(MovieSource)) { UE_LOG(LogCru, Warning, TEXT("cutscene %s: cannot open %s"), *InName, *Path); StopMovie(); return false; }
    bIntroMovie = StartupStep > 0;
    if (!bIntroMovie) Engine->bMoviePlaying = true;
    UE_LOG(LogCru, Display, TEXT("cutscene %s: playing %s"), *InName, *Path);
    return true;
}

void UCrusaderGameSubsystem::UpdateMovie(float DeltaTime)
{
    if (!MoviePlayer) return;
    MovieTime += DeltaTime;
    const bool bEnded = MovieTime > 1.f && !MoviePlayer->IsPlaying() && !MoviePlayer->IsPreparing();
    if (bEnded || MovieTime > 600.f) StopMovie();
}

void UCrusaderGameSubsystem::StopMovie()
{
    if (MoviePlayer) { MoviePlayer->Close(); MoviePlayer = nullptr; }
    if (MovieSound) { MovieSound->DestroyComponent(); MovieSound = nullptr; }
    MovieTexture = nullptr; MovieSource = nullptr;
    if (Engine && !bIntroMovie) Engine->MovieFinished();
    bIntroMovie = false;
}

void UCrusaderGameSubsystem::SkipMovie() { if (MoviePlayer) StopMovie(); }
bool UCrusaderGameSubsystem::SetWeapon(int32 Shape) { return Engine ? Engine->SetAvatarWeapon((uint32)Shape) : false; }
bool UCrusaderGameSubsystem::NextWeapon(int32 Delta) { return Engine ? Engine->NextWeapon(Delta) : false; }
void UCrusaderGameSubsystem::SetInfiniteAmmo(bool bEnabled) { if (Engine) Engine->bInfiniteAmmo = bEnabled; }
int32 UCrusaderGameSubsystem::AvatarHp() const { return Engine ? Engine->AvatarHp() : 0; }

void UCrusaderGameSubsystem::UpdateCollision(const FCruItem& It, const FCruShapeInfo& Si, bool bRemove)
{
    if (!Collision) return;
    const int32* Idx = It.SourceIndex >= 0 ? CollisionInstances.Find(It.SourceIndex) : DynamicCollision.Find(It.Id);
    if (!Idx)
    {
        if (It.SourceIndex >= 0 || bRemove || !Si.IsSolid() || Si.Z == 0) return;
        const int32 NewIdx = Collision->Mesh->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000.f), FVector(0.01f)), true);
        Idx = &DynamicCollision.Add(It.Id, NewIdx);
    }
    int32 Xd, Yd, Zd; Si.GetFootpadWorld(Xd, Yd, Zd, It.IsFlipped());
    FTransform T;
    if (bRemove || !Si.IsSolid() || Zd <= 0 || (Xd <= 0 && Yd <= 0))
        T = FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000.f), FVector(0.01f));
    else
    {
        float X = It.X, Y = It.Y, FXd = Xd, FYd = Yd;
        if (Xd == 0 && Yd >= 128) { X += 8; FXd = 16; }
        if (Yd == 0 && Xd >= 128) { Y += 8; FYd = 16; }
        if (FXd <= 0 || FYd <= 0) T = FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000.f), FVector(0.01f));
        else T = FTransform(FRotator::ZeroRotator, FVector(X - FXd / 2, Y - FYd / 2, It.Z * FCruSpritePlacer::ZK + Zd * FCruSpritePlacer::ZK / 2), FVector(FXd / 100.f, FYd / 100.f, Zd * FCruSpritePlacer::ZK / 100.f));
    }
    Collision->Mesh->UpdateInstanceTransform(*Idx, T, true, true, true);
}

void UCrusaderGameSubsystem::ApplyChanges()
{
    if (Dirty.Num() == 0) return;
    TSet<ACrusaderSpriteBatch*> Touched;
    for (auto& DirtyPair : Dirty)
    {
        const ObjId Id = DirtyPair.Key;
        const int32 Source = DirtyPair.Value;
        const FCruItem* It = Engine->World->GetItem(Id);
        // hide the static instances of a changed/destroyed source item (they are replaced by a dynamic instance)
        if (Source >= 0 && !HiddenSources.Contains(Source))
        {
            HiddenSources.Add(Source);
            if (TArray<TPair<ACrusaderSpriteBatch*, int32>>* L = StaticInstances.Find(Source))
                for (auto& P : *L) { P.Key->HideInstanceForever(P.Value); Touched.Add(P.Key); }
            if (!It && Collision && CollisionInstances.Contains(Source))
                Collision->Mesh->UpdateInstanceTransform(CollisionInstances[Source], FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000.f), FVector(0.01f)), true, true, true);
            if (!It) UE_LOG(LogCru, Display, TEXT("render: item %d (source %d) destroyed, static instances hidden"), Id, Source);
        }
        const bool bGone = !It || (It->Flags & (Cru::FLG_ETHEREAL | Cru::FLG_INVISIBLE | Cru::FLG_CONTAINED)) || (It->X == 0 && It->Y == 0) || (Id == Cru::kMainActorId && !bAvatarEngineAnim);
        FTransform T; float Custom[5]; int32 Atlas = -1;
        const FCruShapeInfo* Si = It ? Engine->World->GetShapeInfo(It->Shape) : nullptr;
        const bool bIsEgg = Si && (Si->Family == Cru::SF_GLOBEGG || Si->Family == Cru::SF_UNKEGG || Si->Family == Cru::SF_MONSTEREGG || Si->Family == Cru::SF_TELEPORTEGG);
        const bool bPlaced = !bGone && Si && !Si->IsEditor() && !bIsEgg && Placer.Place(*It, *Si, T, Custom, Atlas);
        if (It && Si && Id != Cru::kMainActorId) UpdateCollision(*It, *Si, bGone);
        TPair<int32, int32>* D = DynamicInstances.Find(Id);
        if (!bPlaced)
        {
            if (D && DynamicBatches.IsValidIndex(D->Key) && DynamicBatches[D->Key]) { DynamicBatches[D->Key]->Mesh->SetCustomDataValue(D->Value, 4, 0.f, false); Touched.Add(DynamicBatches[D->Key]); }
            if (It && !bGone && Si && !Si->IsEditor() && It->Shape != 0) UE_LOG(LogCru, Warning, TEXT("render: no atlas frame for shape %d frame %d (item %d)"), It->Shape, It->Frame, Id);
            continue;
        }
        if (!DynamicBatches.IsValidIndex(Atlas) || !DynamicBatches[Atlas]) continue;
        ACrusaderSpriteBatch* B = DynamicBatches[Atlas];
        if (D && D->Key != Atlas && DynamicBatches.IsValidIndex(D->Key) && DynamicBatches[D->Key]) { DynamicBatches[D->Key]->Mesh->SetCustomDataValue(D->Value, 4, 0.f, false); Touched.Add(DynamicBatches[D->Key]); D = nullptr; DynamicInstances.Remove(Id); }
        int32 Inst;
        if (D) { Inst = D->Value; B->Mesh->UpdateInstanceTransform(Inst, T, true, false, true); }
        else { Inst = B->Mesh->AddInstance(T, true); DynamicInstances.Add(Id, TPair<int32, int32>(Atlas, Inst)); B->InstanceZ.Add(It->Z * FCruSpritePlacer::ZK); }
        UE_LOG(LogCru, Display, TEXT("render: item %d shape %d frame %d -> dynamic instance %d at (%s) uv (%.3f %.3f %.3f %.3f)"), Id, It->Shape, It->Frame, Inst, *T.GetLocation().ToString(), Custom[0], Custom[1], Custom[2], Custom[3]);
        for (int32 i = 0; i < 5; ++i) B->Mesh->SetCustomDataValue(Inst, i, Custom[i], false);
        Touched.Add(B);
    }
    for (ACrusaderSpriteBatch* B : Touched) B->Mesh->MarkRenderStateDirty();
    Dirty.Reset();
}

void UCrusaderGameSubsystem::PlaySfx()
{
    for (const FCruWorld::FSfxRequest& R : Engine->World->SfxQueue)
    {
        if (R.bStop) { Engine->World->NoteSfxStopped(R.Item, R.Sfx); continue; }
        if (R.Sfx < 0) continue;
        TObjectPtr<USoundWave>* Cached = SfxCache.Find(R.Sfx);
        USoundWave* S = Cached ? Cached->Get() : nullptr;
        if (!Cached) { S = LoadObject<USoundWave>(nullptr, *FString::Printf(TEXT("/Game/Crusader/SFX/S_%03d.S_%03d"), R.Sfx, R.Sfx)); SfxCache.Add(R.Sfx, S); }
        if (!S) continue;
        Engine->World->NoteSfxStarted(R.Item, R.Sfx, (uint32)FMath::CeilToInt(S->Duration * 60.f));
        const FCruItem* It = R.Item ? Engine->World->GetItem(R.Item) : nullptr;
        if (!SfxAttenuation)
        {
            // positional effects fall off with distance from the player (the listener is the pawn, not the camera)
            SfxAttenuation = NewObject<USoundAttenuation>(this);
            SfxAttenuation->Attenuation.bAttenuate = true;
            SfxAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
            SfxAttenuation->Attenuation.AttenuationShapeExtents = FVector(1200.f, 0.f, 0.f);
            SfxAttenuation->Attenuation.FalloffDistance = 2800.f;
            SfxAttenuation->Attenuation.bSpatialize = true;
        }
        const float Vol = FCruSettings::Get().SfxScale();
        if (It && It->Id != Cru::kMainActorId) UGameplayStatics::PlaySoundAtLocation(GetWorld(), S, FVector(It->X, It->Y, It->Z * FCruSpritePlacer::ZK), Vol, 1.f, 0.f, SfxAttenuation);
        else UGameplayStatics::PlaySound2D(GetWorld(), S, Vol);
    }
    Engine->World->SfxQueue.Reset();
}

// CruMusicProcess: one looping track at a time, chosen by usecode
void UCrusaderGameSubsystem::UpdateMusic()
{
    if (!bListenerSet)
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
            if (APawn* Pawn = PC->GetPawn()) { PC->SetAudioListenerOverride(Pawn->GetRootComponent(), FVector::ZeroVector, FRotator::ZeroRotator); bListenerSet = true; }
    if (!Engine->bMusicChanged) return;
    Engine->bMusicChanged = false;
    if (Music) { Music->Stop(); Music = nullptr; }
    const FString Name = Engine->MusicTrackName();
    if (Name.IsEmpty()) return;
    USoundWave* S = LoadObject<USoundWave>(nullptr, *FString::Printf(TEXT("/Game/Crusader/Music/%s.%s"), *Name, *Name));
    if (!S) { UE_LOG(LogCru, Warning, TEXT("music: no asset for %s (run Scripts/import_music.py)"), *Name); return; }
    Music = UGameplayStatics::SpawnSound2D(GetWorld(), S, FCruSettings::Get().MusicScale(), 1.f, 0.f, nullptr, true, false);
}
