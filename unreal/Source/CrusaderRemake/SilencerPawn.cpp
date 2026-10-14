#include "SilencerPawn.h"
#include "CrusaderWorld.h"
#include "CrusaderSpriteBatch.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CapsuleComponent.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputActionValue.h"
#include "EngineUtils.h"
#include "CrusaderGameSubsystem.h"
#include "CrusaderKeyBindings.h"
#include "CrusaderPlayerController.h"
#include "CrusaderSettings.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/ICursor.h"
#if PLATFORM_MAC
#include "Apple/AppleTagRect.h"
#endif
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Usecode/CruWorld.h"
#include "Usecode/CruEngine.h"

using namespace CrusaderWorld;

// Silencer.Move X Y Seconds [Run]: drive the player from the console (used by pie_screenshot.py).
static FAutoConsoleCommandWithWorldAndArgs GSilencerMoveCmd(
    TEXT("Silencer.Move"), TEXT("Silencer.Move X Y Seconds [Run]: scripted movement input"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 3) return;
        for (TActorIterator<ASilencerPawn> It(World); It; ++It)
        {
            It->ScriptedMove = FVector2D(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
            It->ScriptedMoveUntil = FPlatformTime::Seconds() + FCString::Atod(*Args[2]);
            It->bScriptedRun = Args.Num() > 3 && FCString::Atoi(*Args[3]) != 0;
        }
    }));

ASilencerPawn::ASilencerPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    // Footpad of shape 1 is 3x3x10 -> 96 x 96 world units, 80 z units (z scaled by ZK).
    GetCapsuleComponent()->InitCapsuleSize(48.f, 40.f * ZK);

    // Sprite: billboard facing the dimetric camera; the engine anchor (near-right-bottom corner) is at the capsule's base.
    GetSprite()->SetRelativeLocation(FVector(48.f, 48.f, -40.f * ZK));
    GetSprite()->SetRelativeRotation(BillboardRotation());
    GetSprite()->SetRelativeScale3D(FVector(PX, 1.f, ZK));

    UCharacterMovementComponent* Move = GetCharacterMovement();
    Move->DefaultLandMovementMode = MOVE_Flying;
    Move->MaxFlySpeed = WalkSpeed;
    Move->BrakingDecelerationFlying = 100000.f;
    Move->MaxAcceleration = 100000.f;
    Move->bConstrainToPlane = true;
    Move->SetPlaneConstraintNormal(FVector::UpVector);
    Move->GravityScale = 0.f;
    bUseControllerRotationYaw = false;
    Move->bOrientRotationToMovement = false;

    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ASilencerPawn::BeginPlay()
{
    Super::BeginPlay();
    GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    for (int32 Dir = 0; Dir < 8; ++Dir)
        for (int32 Action : {StandAction, WalkAction, RunAction, RetreatAction, CombatStandAction, 37, 45})
            FindFlipbook(Action, Dir);
    SetAnim(StandAction, FacingDir16 / 2);

    FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FollowCamera = GetWorld()->SpawnActor<ACameraActor>(GetActorLocation() - CameraRotation().Vector() * CameraDistance, CameraRotation(), SP);
    if (FollowCamera)
    {
        UCameraComponent* Cam = FollowCamera->GetCameraComponent();
        Cam->ProjectionMode = ECameraProjectionMode::Orthographic;
        Cam->OrthoWidth = OrthoWidth();
        Cam->bAutoCalculateOrthoPlanes = false;
        Cam->OrthoNearClipPlane = 10.f;
        Cam->OrthoFarClipPlane = 50000.f;
        Cam->bConstrainAspectRatio = false;
    }
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (FollowCamera) PC->SetViewTarget(FollowCamera);
        // the OS cursor is hidden and locked to the window; the HUD draws the game's crosshair at the software cursor
        PC->bShowMouseCursor = false;
        FInputModeGameOnly Mode; Mode.SetConsumeCaptureMouseDown(true); PC->SetInputMode(Mode);
        if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
            if (Mapping) Subsystem->AddMappingContext(Mapping, 0);
    }
    TArray<AActor*> Batches;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACrusaderSpriteBatch::StaticClass(), Batches);
    for (AActor* A : Batches)
        if (ACrusaderSpriteBatch* B = Cast<ACrusaderSpriteBatch>(A))
        {
            AllBatches.Add(B);
            if (B->bRoofLayer) RoofBatches.Add(B);
        }
    UE_LOG(LogTemp, Display, TEXT("Silencer: BeginPlay at %s flipbook=%s followcam=%d"), *GetActorLocation().ToString(),
        GetSprite()->GetFlipbook() ? *GetSprite()->GetFlipbook()->GetName() : TEXT("none"), FollowCamera ? 1 : 0);
}

UPaperFlipbook* ASilencerPawn::FindFlipbook(int32 Action, int32 Dir8)
{
    const int32 Key = Action * 100 + Dir8;
    if (UPaperFlipbook** Found = Flipbooks.Find(Key)) return *Found;
    const FString Path = FString::Printf(TEXT("/Game/Crusader/Shapes/%04d/F_%04d_A%03d_D%02d.F_%04d_A%03d_D%02d"), Shape, Shape, Action, Dir8, Shape, Action, Dir8);
    UPaperFlipbook* FB = LoadObject<UPaperFlipbook>(nullptr, *Path);
    if (!FB) UE_LOG(LogTemp, Warning, TEXT("Silencer: missing flipbook %s"), *Path);
    Flipbooks.Add(Key, FB);
    return FB;
}

static FAutoConsoleCommandWithWorldAndArgs GSilencerFaceCmd(
    TEXT("Silencer.Face"), TEXT("Silencer.Face Dir16: set the facing direction without moving"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 1) return;
        for (TActorIterator<ASilencerPawn> It(World); It; ++It) It->SetFacing(FCString::Atoi(*Args[0]) & 15);
    }));
static FAutoConsoleCommandWithWorldAndArgs GSilencerNoAnimCmd(
    TEXT("Silencer.NoAnim"), TEXT("Silencer.NoAnim 0|1: freeze flipbook selection (diagnostics)"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 1) return;
        for (TActorIterator<ASilencerPawn> It(World); It; ++It) It->bNoAnimSwitch = FCString::Atoi(*Args[0]) != 0;
    }));

static FAutoConsoleCommandWithWorldAndArgs GSilencerHideCmd(
    TEXT("Silencer.HideSprite"), TEXT("Silencer.HideSprite 0|1: hide the player's sprite (diagnostics)"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 1) return;
        for (TActorIterator<ASilencerPawn> It(World); It; ++It) It->GetSprite()->SetVisibility(FCString::Atoi(*Args[0]) == 0);
    }));

void ASilencerPawn::SetAnim(int32 Action, int32 Dir8)
{
    if (bNoAnimSwitch) return;
    if (UPaperFlipbook* FB = FindFlipbook(Action, Dir8))
        if (GetSprite()->GetFlipbook() != FB) GetSprite()->SetFlipbook(FB);
}

void ASilencerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    // Build the Enhanced Input assets in code so the project needs no hand-made input assets.
    MoveAction = NewObject<UInputAction>(this, TEXT("IA_Move"));
    MoveAction->ValueType = EInputActionValueType::Axis2D;
    RunActionInput = NewObject<UInputAction>(this, TEXT("IA_Run"));
    RunActionInput->ValueType = EInputActionValueType::Boolean;
    Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Silencer"));

    auto MakeBool = [&](const TCHAR* Name) { UInputAction* A = NewObject<UInputAction>(this, Name); A->ValueType = EInputActionValueType::Boolean; return A; };
    ZoomInInput = MakeBool(TEXT("IA_ZoomIn")); ZoomOutInput = MakeBool(TEXT("IA_ZoomOut"));
    UseInput = MakeBool(TEXT("IA_Use")); FireInput = MakeBool(TEXT("IA_Fire")); KneelInput = MakeBool(TEXT("IA_Kneel"));
    SaveInput = MakeBool(TEXT("IA_Save")); LoadInput = MakeBool(TEXT("IA_Load"));
    UseItemInput = MakeBool(TEXT("IA_UseItem")); NextItemInput = MakeBool(TEXT("IA_NextItem"));
    NextWeaponInput = MakeBool(TEXT("IA_NextWeapon")); PrevWeaponInput = MakeBool(TEXT("IA_PrevWeapon"));
    BindingsInput = MakeBool(TEXT("IA_Bindings"));
    HelpInput = MakeBool(TEXT("IA_Help"));
    CombatInput = MakeBool(TEXT("IA_Combat"));
    JumpModInput = MakeBool(TEXT("IA_JumpMod")); StepModInput = MakeBool(TEXT("IA_StepMod"));
    LookInput = NewObject<UInputAction>(this, TEXT("IA_Look")); LookInput->ValueType = EInputActionValueType::Axis2D;
    RebuildBindings();

    if (auto* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASilencerPawn::OnMove);
        EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &ASilencerPawn::OnMove);
        EIC->BindAction(RunActionInput, ETriggerEvent::Started, this, &ASilencerPawn::OnRun);
        EIC->BindAction(RunActionInput, ETriggerEvent::Completed, this, &ASilencerPawn::OnRunReleased);
        EIC->BindAction(ZoomInInput, ETriggerEvent::Started, this, &ASilencerPawn::OnZoomIn);
        EIC->BindAction(ZoomOutInput, ETriggerEvent::Started, this, &ASilencerPawn::OnZoomOut);
        EIC->BindAction(UseInput, ETriggerEvent::Started, this, &ASilencerPawn::OnUse);
        EIC->BindAction(FireInput, ETriggerEvent::Triggered, this, &ASilencerPawn::OnFire);
        EIC->BindAction(KneelInput, ETriggerEvent::Started, this, &ASilencerPawn::OnKneel);
        EIC->BindAction(SaveInput, ETriggerEvent::Started, this, &ASilencerPawn::OnSave);
        EIC->BindAction(LoadInput, ETriggerEvent::Started, this, &ASilencerPawn::OnLoad);
        EIC->BindAction(UseItemInput, ETriggerEvent::Started, this, &ASilencerPawn::OnUseItem);
        EIC->BindAction(NextItemInput, ETriggerEvent::Started, this, &ASilencerPawn::OnNextItem);
        EIC->BindAction(NextWeaponInput, ETriggerEvent::Started, this, &ASilencerPawn::OnNextWeapon);
        EIC->BindAction(PrevWeaponInput, ETriggerEvent::Started, this, &ASilencerPawn::OnPrevWeapon);
        EIC->BindAction(BindingsInput, ETriggerEvent::Started, this, &ASilencerPawn::OnBindings);
        EIC->BindAction(HelpInput, ETriggerEvent::Started, this, &ASilencerPawn::OnHelp);
        EIC->BindAction(CombatInput, ETriggerEvent::Started, this, &ASilencerPawn::OnCombat);
        EIC->BindAction(JumpModInput, ETriggerEvent::Started, this, &ASilencerPawn::OnJumpMod);
        EIC->BindAction(JumpModInput, ETriggerEvent::Completed, this, &ASilencerPawn::OnJumpModReleased);
        EIC->BindAction(StepModInput, ETriggerEvent::Started, this, &ASilencerPawn::OnStepMod);
        EIC->BindAction(StepModInput, ETriggerEvent::Completed, this, &ASilencerPawn::OnStepModReleased);
        EIC->BindAction(LookInput, ETriggerEvent::Triggered, this, &ASilencerPawn::OnLook);
    }
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
        if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
            Subsystem->AddMappingContext(Mapping, 0);
}

void ASilencerPawn::RebuildBindings()
{
    if (!Mapping || !MoveAction) return;
    Mapping->UnmapAll();
    const FCruKeyBindings& B = FCruKeyBindings::Get();
    auto MapMove = [&](const TCHAR* Action, bool bNegate, bool bSwizzleToY)
    {
        for (const FKey& Key : B.Keys(Action))
        {
            FEnhancedActionKeyMapping& M = Mapping->MapKey(MoveAction, Key);
            if (bSwizzleToY) { auto* Sw = NewObject<UInputModifierSwizzleAxis>(this); Sw->Order = EInputAxisSwizzle::YXZ; M.Modifiers.Add(Sw); }
            if (bNegate) M.Modifiers.Add(NewObject<UInputModifierNegate>(this));
        }
    };
    MapMove(TEXT("MoveRight"), false, false);   // +X = screen right
    MapMove(TEXT("MoveLeft"), true, false);
    MapMove(TEXT("MoveUp"), false, true);       // +Y = screen up
    MapMove(TEXT("MoveDown"), true, true);
    auto MapAll = [&](UInputAction* A, const TCHAR* Action) { if (A) for (const FKey& Key : B.Keys(Action)) Mapping->MapKey(A, Key); };
    MapAll(RunActionInput, TEXT("Run")); MapAll(FireInput, TEXT("Fire")); MapAll(UseInput, TEXT("Use")); MapAll(KneelInput, TEXT("Kneel"));
    MapAll(NextWeaponInput, TEXT("NextWeapon")); MapAll(PrevWeaponInput, TEXT("PrevWeapon"));
    MapAll(UseItemInput, TEXT("UseItem")); MapAll(NextItemInput, TEXT("NextItem"));
    MapAll(ZoomInInput, TEXT("ZoomIn")); MapAll(ZoomOutInput, TEXT("ZoomOut"));
    MapAll(SaveInput, TEXT("Save")); MapAll(LoadInput, TEXT("Load")); MapAll(BindingsInput, TEXT("Bindings")); MapAll(HelpInput, TEXT("Help")); MapAll(CombatInput, TEXT("Combat")); MapAll(JumpModInput, TEXT("Jump")); MapAll(StepModInput, TEXT("Step"));
    if (LookInput) Mapping->MapKey(LookInput, EKeys::Mouse2D);
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
        if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
            Subsystem->RequestRebuildControlMappings();
}

void ASilencerPawn::OnBindings(const FInputActionValue&) { if (ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(GetController())) { if (PC->Menu == ECruMenu::Bindings) PC->CloseMenu(); else if (!PC->IsMenuOpen()) PC->OpenMenu(ECruMenu::Bindings); } }
void ASilencerPawn::OnCombat(const FInputActionValue&) { if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->ToggleCombat(); }
void ASilencerPawn::OnJumpMod(const FInputActionValue&) { bJumpHeld = true; }
void ASilencerPawn::OnJumpModReleased(const FInputActionValue&) { bJumpHeld = false; }
void ASilencerPawn::OnStepMod(const FInputActionValue&) { bStepHeld = true; }
void ASilencerPawn::OnStepModReleased(const FInputActionValue&) { bStepHeld = false; }

// Original Jump (Ctrl) / Step (Alt) + direction moves (CruAvatarMoverProcess::handleCombatMode). Direction keys are
// read as forward / back / left / right relative to the avatar's facing; one move per key press.
bool ASilencerPawn::TryModifierMove(const FVector2D& Input)
{
    if (!bJumpHeld && !bStepHeld) { bModMoveDone = false; return false; }
    if (Input.IsNearlyZero(0.3f)) { bModMoveDone = false; return true; }   // modifier held, waiting for a direction
    if (bModMoveDone || bEngineAnim) return true;
    UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>();
    if (!S) return true;
    FCruEngine* E = S->GetEngine();
    const bool bKneeling = E && E->IsAvatarKneeling();
    const bool bFwd = Input.Y > 0.5f, bBack = Input.Y < -0.5f, bLeft = Input.X < -0.5f, bRight = Input.X > 0.5f;
    int32 Anim = -1;
    if (bJumpHeld)
    {
        if (bFwd) Anim = 56;                            // jumpForward
        else if (bBack) { if (!bKneeling) Anim = 40; } // kneelStartCru
        else if (bLeft) Anim = bKneeling ? 23 : 61;     // (kneel)combatRollLeft
        else if (bRight) Anim = bKneeling ? 24 : 62;
    }
    else
    {
        if (bFwd) Anim = bKneeling ? 52 : (bLargeWeapon ? 44 : 36);    // kneelingAdvance / advance
        else if (bBack) Anim = bKneeling ? 51 : (bLargeWeapon ? 45 : 2); // kneelingRetreat / retreat
        else if (bLeft) Anim = bKneeling ? 41 : 28;     // slideLeft (kneeling: stand up)
        else if (bRight) Anim = bKneeling ? 41 : 29;
    }
    bModMoveDone = true;
    if (Anim >= 0) S->AvatarMove(Anim, true);
    return true;
}

void ASilencerPawn::OnHelp(const FInputActionValue&) { FCruSettings& S = FCruSettings::Get(); S.bShowKeyHelp = !S.bShowKeyHelp; S.Save(); }
void ASilencerPawn::OnLook(const FInputActionValue& Value)
{
    // the cursor position comes from the OS cursor (see Tick); the deltas only mark mouse activity for aiming
    const FVector2D D = Value.Get<FVector2D>();
    if (D.IsNearlyZero() || CursorPos.X < 0.f || FMath::Abs(D.X) > 250.f || FMath::Abs(D.Y) > 250.f) return;
    LastMouseMoveTime = FPlatformTime::Seconds();
}
void ASilencerPawn::OnMove(const FInputActionValue& Value) { MoveInput = Value.Get<FVector2D>(); }
void ASilencerPawn::OnRun(const FInputActionValue&) { bRunHeld = true; }
void ASilencerPawn::OnRunReleased(const FInputActionValue&) { bRunHeld = false; }
void ASilencerPawn::OnZoomIn(const FInputActionValue&) { ZoomIndex = FMath::Max(0, ZoomIndex - 1); ApplyZoom(); }
void ASilencerPawn::OnZoomOut(const FInputActionValue&) { ZoomIndex = FMath::Min(ZoomLevels.Num() - 1, ZoomIndex + 1); ApplyZoom(); }
void ASilencerPawn::OnUse(const FInputActionValue&)
{
    if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->UseNearest();
}

void ASilencerPawn::OnFire(const FInputActionValue&)
{
    if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) { if (S->IsMoviePlaying()) { UE_LOG(LogTemp, Display, TEXT("fire input skips the movie")); S->SkipMovie(); } else S->Fire(); }
}
void ASilencerPawn::OnSave(const FInputActionValue&) { if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->SaveGame(0); }
void ASilencerPawn::OnLoad(const FInputActionValue&) { if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->LoadGame(0); }
// Silencer.Save [slot] / Silencer.Load [slot]: checkpoint saves (F5 / F9 use slot 0)
static FAutoConsoleCommandWithWorldAndArgs GSilencerSaveCmd(TEXT("Silencer.Save"), TEXT("Silencer.Save [slot]"), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->SaveGame(Args.Num() ? FCString::Atoi(*Args[0]) : 0); }));
static FAutoConsoleCommandWithWorldAndArgs GSilencerLoadCmd(TEXT("Silencer.Load"), TEXT("Silencer.Load [slot]"), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->LoadGame(Args.Num() ? FCString::Atoi(*Args[0]) : 0); }));
void ASilencerPawn::OnKneel(const FInputActionValue&) { if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->ToggleKneel(); }
void ASilencerPawn::OnUseItem(const FInputActionValue&) { if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->UseItem(); }
void ASilencerPawn::OnNextItem(const FInputActionValue&) { if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->NextItem(); }

// Silencer.Kneel / Silencer.UseItem / Silencer.NextItem / Silencer.Movie NAME: scripted equivalents of the keys
static FAutoConsoleCommandWithWorld GSilencerKneelCmd(TEXT("Silencer.Kneel"), TEXT("toggle kneeling"), FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->ToggleKneel(); }));
static FAutoConsoleCommandWithWorld GSilencerUseItemCmd(TEXT("Silencer.UseItem"), TEXT("use the active inventory item"), FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->UseItem(); }));
static FAutoConsoleCommandWithWorld GSilencerNextItemCmd(TEXT("Silencer.NextItem"), TEXT("select the next inventory item"), FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->NextItem(); }));
static FAutoConsoleCommandWithWorldAndArgs GSilencerMovieCmd(TEXT("Silencer.Movie"), TEXT("Silencer.Movie NAME: play a cutscene (e.g. 01A, mva1)"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { if (UCrusaderGameSubsystem* S = (World && Args.Num()) ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->PlayMovie(Args[0]); }));

void ASilencerPawn::OnNextWeapon(const FInputActionValue&)
{
    // Shift+Tab cycles backwards (shift is also the run modifier, which does not matter while switching weapons)
    if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->NextWeapon(bRunHeld ? -1 : 1);
}

void ASilencerPawn::OnPrevWeapon(const FInputActionValue&)
{
    if (UCrusaderGameSubsystem* S = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>()) S->NextWeapon(-1);
}

// Silencer.NextWeapon / Silencer.PrevWeapon: cycle the active weapon through the inventory
static FAutoConsoleCommandWithWorld GSilencerNextWeaponCmd(TEXT("Silencer.NextWeapon"), TEXT("select the next weapon in the inventory"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->NextWeapon(1);
    }));
static FAutoConsoleCommandWithWorld GSilencerPrevWeaponCmd(TEXT("Silencer.PrevWeapon"), TEXT("select the previous weapon in the inventory"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->NextWeapon(-1);
    }));

// Silencer.InfiniteAmmo 0|1: cheat, reload without ammo items and never drain energy
static FAutoConsoleCommandWithWorldAndArgs GSilencerInfiniteAmmoCmd(TEXT("Silencer.InfiniteAmmo"), TEXT("Silencer.InfiniteAmmo 0|1"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 1) return;
        if (UCrusaderGameSubsystem* S = World->GetSubsystem<UCrusaderGameSubsystem>()) S->SetInfiniteAmmo(FCString::Atoi(*Args[0]) != 0);
    }));

// Silencer.TeleportEgg MAP EGG: MainActor::teleport to a destination egg (other maps open their level, carrying the inventory)
static FAutoConsoleCommandWithWorldAndArgs GSilencerTeleportEggCmd(TEXT("Silencer.TeleportEgg"), TEXT("Silencer.TeleportEgg map egg"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 2) return;
        if (UCrusaderGameSubsystem* S = World->GetSubsystem<UCrusaderGameSubsystem>()) S->TeleportToEgg(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]));
    }));

// Silencer.BatchMaterial PATH: diagnostics - replace every sprite batch material 3 s after the next level initialises
extern FString GCruBatchMaterialOverride;
static FAutoConsoleCommandWithWorldAndArgs GSilencerBatchMaterialCmd(TEXT("Silencer.BatchMaterial"), TEXT("Silencer.BatchMaterial /Path/To/Material"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        FString M = Args.Num() ? Args[0] : FString();
        if (M == TEXT("grid")) M = TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial");
        else if (M == TEXT("map1")) M = TEXT("/Game/Crusader/Materials/MI_Map01x2_Atlas0.MI_Map01x2_Atlas0");
        else if (M == TEXT("map2")) M = TEXT("/Game/Crusader/Materials/MI_Map02x2_Atlas0.MI_Map02x2_Atlas0");
        GCruBatchMaterialOverride = M;   // map1tex2 / dyn2 are handled by the subsystem
    }));

// Silencer.Fire: fire the avatar's weapon once
extern bool GCruCombatDebug;
// Silencer.SweepCheck [radius]: sweep a bullet-sized box through every solid item near the avatar and report the ones
// the sweep test misses (diagnostic for shots passing through walls)
static FAutoConsoleCommandWithWorldAndArgs GSilencerSweepCheckCmd(TEXT("Silencer.SweepCheck"), TEXT("Silencer.SweepCheck [radius]: report solid items near the avatar that a bullet sweep misses"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
    {
        if (!GCruWorld) return;
        const FCruItem* Av = GCruWorld->GetItem(GCruWorld->AvatarId);
        if (!Av) return;
        const int32 Radius = Args.Num() ? FCString::Atoi(*Args[0]) : 2000;
        int32 Tested = 0, Missed = 0;
        for (const auto& P : GCruWorld->AllItems())
        {
            const FCruItem* It = P.Value;
            if (!It || It->Id == Av->Id || (It->ExtFlags & Cru::EXT_SPRITE) || FMath::Abs(It->X - Av->X) > Radius || FMath::Abs(It->Y - Av->Y) > Radius) continue;
            const FCruShapeInfo& Si = GCruWorld->Info(*It);
            if (!(Si.Flags & Cru::SI_SOLID)) continue;
            int32 Xd, Yd, Zd; GCruWorld->GetFootpadWorld(*It, Xd, Yd, Zd);
            if (Zd < 16 || Xd < 1 || Yd < 1) continue;
            ++Tested;
            const int32 Z = It->Z + FMath::Min(8, Zd / 2);
            const int32 Dims[3] = { 1, 1, 1 };
            // through the middle of the footpad along x, then along y
            const FIntVector Sx(It->X + 200, It->Y - Yd / 2, Z), Ex(It->X - Xd - 200, It->Y - Yd / 2, Z);
            const FIntVector Sy(It->X - Xd / 2, It->Y + 200, Z), Ey(It->X - Xd / 2, It->Y - Yd - 200, Z);
            TArray<FCruWorld::FSweepItem> Hx, Hy;
            GCruWorld->SweepTest(Sx, Ex, Dims, Cru::SI_SOLID, Av->Id, true, &Hx);
            GCruWorld->SweepTest(Sy, Ey, Dims, Cru::SI_SOLID, Av->Id, true, &Hy);
            const bool bHitX = Hx.ContainsByPredicate([&](const FCruWorld::FSweepItem& H) { return H.Item == It->Id; });
            const bool bHitY = Hy.ContainsByPredicate([&](const FCruWorld::FSweepItem& H) { return H.Item == It->Id; });
            if (!bHitX || !bHitY)
            {
                ++Missed;
                if (Missed <= 20) UE_LOG(LogCru, Display, TEXT("sweepcheck: item %d shape %d frame %d at (%d,%d,%d) footpad %dx%dx%d flags %x missed (x %d y %d) fast %d"), It->Id, It->Shape, It->Frame, It->X, It->Y, It->Z, Xd, Yd, Zd, Si.Flags, bHitX, bHitY, (It->Flags & Cru::FLG_FASTAREA) ? 1 : 0);
            }
        }
        UE_LOG(LogCru, Display, TEXT("sweepcheck: %d of %d solid items within %d missed"), Missed, Tested, Radius);
    }));
static FAutoConsoleCommandWithWorldAndArgs GSilencerCombatDebugCmd(TEXT("Silencer.CombatDebug"), TEXT("Silencer.CombatDebug 0|1: log attacks, visibility and reticle scans"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*) { GCruCombatDebug = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0; }));

// Silencer.Menu open Main|Pause|Options|Missions|Bindings | close | up | down | enter | left | right: drive the menus from the console (tests)
static FAutoConsoleCommandWithWorldAndArgs GSilencerMenuCmd(TEXT("Silencer.Menu"), TEXT("Silencer.Menu open NAME|close|up|down|enter|left|right"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        ACrusaderPlayerController* PC = World ? Cast<ACrusaderPlayerController>(UGameplayStatics::GetPlayerController(World, 0)) : nullptr;
        if (!PC || Args.Num() == 0) return;
        const FString A = Args[0].ToLower();
        auto Key = [&](const FKey& K) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE_Pressed, 1.f)); };
        if (A == TEXT("open") && Args.Num() > 1)
        {
            const FString N = Args[1].ToLower();
            PC->OpenMenu(N == TEXT("main") || N == TEXT("pause") ? ECruMenu::Main : N == TEXT("options") ? ECruMenu::Options : N == TEXT("missions") ? ECruMenu::Missions : N == TEXT("difficulty") ? ECruMenu::Difficulty : N == TEXT("credits") ? ECruMenu::Credits : ECruMenu::Bindings);
        }
        else if (A == TEXT("close")) PC->CloseAllMenus();
        else if (A == TEXT("up")) Key(EKeys::Up); else if (A == TEXT("down")) Key(EKeys::Down); else if (A == TEXT("enter")) Key(EKeys::Enter);
        else if (A == TEXT("left")) Key(EKeys::Left); else if (A == TEXT("right")) Key(EKeys::Right); else if (A == TEXT("esc")) Key(EKeys::Escape);
    }));

// Silencer.Mission N: mission picker from the console
static FAutoConsoleCommandWithWorldAndArgs GSilencerMissionCmd(TEXT("Silencer.Mission"), TEXT("Silencer.Mission N: start mission N (1-15) at its start egg"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (UCrusaderGameSubsystem* S = World && Args.Num() ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->StartMission(FCString::Atoi(*Args[0]));
    }));

static FAutoConsoleCommandWithWorld GSilencerCombatCmd(TEXT("Silencer.Combat"), TEXT("draw / holster the avatar's weapon (combat stance)"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->ToggleCombat(); }));

static FAutoConsoleCommandWithWorld GSilencerSkipIntroCmd(TEXT("Silencer.SkipIntro"), TEXT("skip the intro movies and the menu, straight into the game (tests)"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) { S->StartupStep = 0; S->SkipMovie(); if (S->GetEngine()) S->GetEngine()->StartPlaying(); if (ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(UGameplayStatics::GetPlayerController(World, 0))) PC->CloseAllMenus(); } }));

static FAutoConsoleCommandWithWorldAndArgs GSilencerAnimCmd(TEXT("Silencer.Anim"), TEXT("Silencer.Anim N: play a one-shot avatar move animation (61/62 rolls, 28/29 slides, 56 jump, 36 advance, 2 retreat)"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { if (UCrusaderGameSubsystem* S = World && Args.Num() ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->AvatarMove(FCString::Atoi(*Args[0]), true); }));

static FAutoConsoleCommandWithWorld GSilencerFireCmd(TEXT("Silencer.Fire"), TEXT("fire the avatar's weapon"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) { if (S->IsMoviePlaying()) S->SkipMovie(); else S->Fire(); }
    }));

// Silencer.Weapon SHAPE: give the avatar a weapon by shape number (0x32E BA-40, 0x333 RP-22, 0x332 SG-A1...)
static FAutoConsoleCommandWithWorldAndArgs GSilencerWeaponCmd(TEXT("Silencer.Weapon"), TEXT("Silencer.Weapon shape"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 1) return;
        if (UCrusaderGameSubsystem* S = World->GetSubsystem<UCrusaderGameSubsystem>()) S->SetWeapon(FCString::Strtoi(*Args[0], nullptr, 0));
    }));

// Silencer.Teleport X Y [Z]: move the pawn (UE units; z defaults to the floor) for automated tests
static FAutoConsoleCommandWithWorldAndArgs GSilencerTeleportCmd(TEXT("Silencer.Teleport"), TEXT("Silencer.Teleport X Y [Z]"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 2) return;
        for (TActorIterator<ASilencerPawn> It(World); It; ++It)
            It->SetActorLocation(FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), Args.Num() > 2 ? FCString::Atof(*Args[2]) : 40.f * ZK), false, nullptr, ETeleportType::TeleportPhysics);
    }));

// Silencer.Use: use the nearest selectable item (door panels, switches)
static FAutoConsoleCommandWithWorld GSilencerUseCmd(TEXT("Silencer.Use"), TEXT("use the nearest selectable item"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (UCrusaderGameSubsystem* S = World ? World->GetSubsystem<UCrusaderGameSubsystem>() : nullptr) S->UseNearest();
    }));

void ASilencerPawn::ApplyZoom()
{
    if (FollowCamera) FollowCamera->GetCameraComponent()->OrthoWidth = OrthoWidth();
    UE_LOG(LogTemp, Display, TEXT("Silencer zoom: %.0f original px across"), OrthoWidth() / PX);
}

// Silencer.Zoom N: set the zoom level index from the console (used by pie_screenshot.py)
static FAutoConsoleCommandWithWorldAndArgs GSilencerZoomCmd(
    TEXT("Silencer.Zoom"), TEXT("Silencer.Zoom index: 0 = closest"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 1) return;
        for (TActorIterator<ASilencerPawn> It(World); It; ++It) { It->ZoomIndex = FMath::Clamp(FCString::Atoi(*Args[0]), 0, It->ZoomLevels.Num() - 1); It->ApplyZoom(); }
    }));

void ASilencerPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (FollowCamera)
        FollowCamera->SetActorLocation(GetActorLocation() - CameraRotation().Vector() * CameraDistance);
    {
        float ZLimit = TNumericLimits<float>::Max();
        for (ACrusaderSpriteBatch* B : RoofBatches)
            if (B) ZLimit = FMath::Min(ZLimit, B->FindRoofZLimit(GetActorLocation()));
        for (ACrusaderSpriteBatch* B : AllBatches)
            if (B) B->ApplyZLimit(ZLimit);
    }
    // frame-time telemetry (average over 2 s windows) for optimisation work
    static double Acc = 0; static int32 Frames = 0;
    Acc += DeltaSeconds; ++Frames;
    if (Acc >= 2.0) { UE_LOG(LogTemp, Display, TEXT("Silencer fps: %.1f (%.2f ms)"), Frames / Acc, 1000.0 * Acc / Frames); Acc = 0; Frames = 0; }

    DiagTimer += DeltaSeconds;
    if (DiagTimer > 2.f)
    {
        DiagTimer = 0.f;
        UPaperFlipbook* CurFB = GetSprite()->GetFlipbook();
#if WITH_EDITORONLY_DATA
        UPaperSprite* Spr = CurFB ? CurFB->GetSpriteAtFrame(GetSprite()->GetPlaybackPositionInFrames()) : nullptr;
        if (Spr)
            if (UTexture2D* Tex = Spr->GetSourceTexture())
                UE_LOG(LogTemp, Display, TEXT("Silencer tex: %s baked=%s compiling=%d default=%d resource=%d size=%dx%d mat=%s | actorRot=%s spriteLoc=%s spriteRot=%s bounds=%s"),
                    *Tex->GetName(), Spr->GetBakedTexture() ? *Spr->GetBakedTexture()->GetName() : TEXT("NONE"), Tex->IsCompiling() ? 1 : 0, Tex->GetResource() == nullptr ? 1 : 0, Tex->GetResource() ? 1 : 0,
                    Tex->GetSizeX(), Tex->GetSizeY(),
                    GetSprite()->GetMaterial(0) ? *GetSprite()->GetMaterial(0)->GetName() : TEXT("none"),
                    *GetActorRotation().ToString(), *GetSprite()->GetComponentLocation().ToString(), *GetSprite()->GetComponentRotation().ToString(),
                    *GetSprite()->Bounds.GetBox().ToString());
#endif
    }
    if (!bDiagDone && DiagTimer > 1.f)
    {
        bDiagDone = true;
        UE_LOG(LogTemp, Display, TEXT("Silencer diag: proxy=%d visible=%d recentlyRendered=%d worldLoc=%s flipbook=%s"),
            GetSprite()->SceneProxy ? 1 : 0, GetSprite()->IsVisible() ? 1 : 0, GetSprite()->WasRecentlyRendered(0.5f) ? 1 : 0,
            *GetSprite()->GetComponentLocation().ToString(), GetSprite()->GetFlipbook() ? *GetSprite()->GetFlipbook()->GetName() : TEXT("none"));
    }

    const bool bScripted = FPlatformTime::Seconds() < ScriptedMoveUntil;
    const FVector2D Input = bScripted ? ScriptedMove : MoveInput;
    const bool bRun = bScripted ? bScriptedRun : bRunHeld;
    bMoving = !Input.IsNearlyZero(0.1f) && !bEngineAnim;
    // mouse aiming: project the software cursor onto the floor plane at the pawn's feet; the facing follows it while it moves
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        int32 Vw = 0, Vh = 0; PC->GetViewportSize(Vw, Vh);
        if (CursorPos.X < 0.f && Vw > 0) CursorPos = FVector2D(Vw * 0.5f, Vh * 0.5f);
        // the OS cursor is hidden and locked to the window (UpdateCursorLock); its viewport position is the software cursor
        float Mx = 0.f, My = 0.f;
        if (PC->GetMousePosition(Mx, My) && Vw > 0)
        {
            const FVector2D M(Mx, My);
            if (!M.Equals(CursorPos, 0.5f)) { if (bHaveMouse) LastMouseMoveTime = FPlatformTime::Seconds(); CursorPos = M; }
            bHaveMouse = true;
        }
        if (Vw > 0) { CursorPos.X = FMath::Clamp(CursorPos.X, 0.f, (float)Vw - 1.f); CursorPos.Y = FMath::Clamp(CursorPos.Y, 0.f, (float)Vh - 1.f); }
        FVector O, D;
        if (CursorPos.X >= 0.f && PC->DeprojectScreenPositionToWorld(CursorPos.X, CursorPos.Y, O, D) && FMath::Abs(D.Z) > KINDA_SMALL_NUMBER)
        {
            const float FloorZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            AimWorld = O + D * ((FloorZ - O.Z) / D.Z);
        }
    }
    UpdateCursorLock();
    const double Now = FPlatformTime::Seconds();
    bMouseAim = Now - LastMouseMoveTime < 2.0 && Now - LastKeyTurnTime > 0.5 && !bScripted;
    if (bMouseAim && !bEngineAnim)
    {
        const FVector ToAim = AimWorld - GetActorLocation();
        if (ToAim.SizeSquared2D() > 400.f) FacingDir16 = WorldToDir16(FVector(ToAim.X, ToAim.Y, 0.f));
    }
    if (TryModifierMove(Input)) { bMoving = false; bRetreating = false; if (!bEngineAnim) SetAnim(bCombatStance ? (bLargeWeapon ? 37 : CombatStandAction) : StandAction, FacingDir16 / 2); return; }
    // combat stance (original handleCombatMode): left/right turn in place, back retreats facing the target,
    // forward walks and stows the weapon (handled by the engine when bMoving)
    bRetreating = false;
    const bool bStanceKeys = bCombatStance && !bEngineAnim && !bScripted;
    if (bStanceKeys && Input.Y < -0.3f && FMath::Abs(Input.X) < 0.5f)
    {
        bRetreating = true; bMoving = false;
        const FVector Back = -Dir16ToWorld(FacingDir16);
        GetCharacterMovement()->MaxFlySpeed = WalkSpeed * 0.75f;
        AddMovementInput(Back, 1.f);
        SetAnim(bLargeWeapon ? 45 : RetreatAction, FacingDir16 / 2);
        return;
    }
    if (bStanceKeys && FMath::Abs(Input.X) > 0.3f && Input.Y <= 0.3f)
    {
        bMoving = false;
        if (Now >= NextTurnTime)
        {
            NextTurnTime = Now + 0.12;
            LastKeyTurnTime = Now;
            FacingDir16 = (FacingDir16 + (Input.X > 0 ? 1 : 15)) % 16;
        }
        SetAnim(bLargeWeapon ? 37 : CombatStandAction, FacingDir16 / 2);
        return;
    }
    if (bEngineAnim) {}
    else if (bMoving)
    {
        FVector Dir = ScreenRight() * Input.X + ScreenUpOnGround() * Input.Y;
        Dir.Normalize();
        if (!bMouseAim) FacingDir16 = WorldToDir16(Dir);
        GetCharacterMovement()->MaxFlySpeed = bRun ? RunSpeed : WalkSpeed;
        AddMovementInput(Dir, 1.f);
        SetAnim(bRun ? RunAction : WalkAction, FacingDir16 / 2);
    }
    else
    {
        SetAnim(bCombatStance ? (bLargeWeapon ? 37 : CombatStandAction) : StandAction, FacingDir16 / 2);
    }
}

// Keep the OS cursor hidden and locked to the game window while it is in the foreground (Slate's own capture needs a
// click it does not always get on macOS); the crosshair is the software cursor driven by mouse deltas.
void ASilencerPawn::UpdateCursorLock()
{
    if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return;
    TSharedPtr<SWindow> Win = GEngine->GameViewport->GetWindow();
    TSharedPtr<ICursor> Cursor = FSlateApplication::Get().GetPlatformCursor();
    if (!Win.IsValid() || !Cursor.IsValid() || !Win->GetNativeWindow().IsValid()) return;
    const bool bWant = FSlateApplication::Get().IsActive() && Win->GetNativeWindow()->IsForegroundWindow();
    if (bWant)
    {
        const FSlateRect R = Win->GetRectInScreen();
        RECT Rc; Rc.left = (int32)R.Left + 2; Rc.top = (int32)R.Top + 2; Rc.right = (int32)R.Right - 2; Rc.bottom = (int32)R.Bottom - 2;
        Cursor->Lock(&Rc);
        if (!bCursorLocked)
        {
            Cursor->Show(false);
            UE_LOG(LogTemp, Display, TEXT("cursor: locked to the game window (%d,%d)-(%d,%d)"), Rc.left, Rc.top, Rc.right, Rc.bottom);
        }
    }
    else if (bCursorLocked)
    {
        Cursor->Lock(nullptr);
        Cursor->Show(true);
        UE_LOG(LogTemp, Display, TEXT("cursor: released (window not in the foreground)"));
    }
    bCursorLocked = bWant;
}
