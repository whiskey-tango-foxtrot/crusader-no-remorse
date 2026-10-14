#pragma once
#include "CoreMinimal.h"
#include "PaperCharacter.h"
#include "SilencerPawn.generated.h"

class UPaperFlipbook;
class ACameraActor;
class UInputAction;
class UInputMappingContext;

// Player character driven by the original Silencer animation set (shape 1).
// Based on APaperCharacter: capsule root + flipbook sprite + CharacterMovement (flying mode: floors have no collision).
UCLASS()
class CRUSADERREMAKE_API ASilencerPawn : public APaperCharacter
{
    GENERATED_BODY()
public:
    ASilencerPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // Shape number whose flipbooks (F_<shape>_A<action>_D<dir>) drive this character.
    UPROPERTY(EditAnywhere) int32 Shape = 1;
    UPROPERTY(EditAnywhere) int32 StandAction = 0;   // standCru
    UPROPERTY(EditAnywhere) int32 WalkAction = 1;    // walkCru
    UPROPERTY(EditAnywhere) int32 RunAction = 3;     // runCru
    // Engine moves 4*deltaDir units per anim frame; walk cycle sums to 64, run to 96 (shape 1).
    UPROPERTY(EditAnywhere) float WalkSpeed = 226.f;
    UPROPERTY(EditAnywhere) float RunSpeed = 480.f;
    // View width in original pixels. 960 shows one original pixel as two screen pixels on a 1920-wide window (the
    // original game showed 640 across). Keys = / - step through ZoomLevels.
    UPROPERTY(EditAnywhere) TArray<float> ZoomLevels = {640.f, 960.f, 1280.f, 1920.f};
    UPROPERTY(EditAnywhere) int32 ZoomIndex = 1;
    float OrthoWidth() const { return ZoomLevels.IsValidIndex(ZoomIndex) ? ZoomLevels[ZoomIndex] * 2.8284271f : 960.f * 2.8284271f; }
    UPROPERTY(EditAnywhere) float CameraDistance = 8000.f;

    // Separate follow camera actor (the orthographic dimetric view).
    UPROPERTY() ACameraActor* FollowCamera = nullptr;
    UPROPERTY() TArray<class ACrusaderSpriteBatch*> RoofBatches;
    UPROPERTY() TArray<class ACrusaderSpriteBatch*> AllBatches;

private:
    UPROPERTY() TMap<int32, UPaperFlipbook*> Flipbooks;   // key = action*100 + dir8
    UPROPERTY() UInputAction* MoveAction = nullptr;
    UPROPERTY() UInputAction* RunActionInput = nullptr;
    UPROPERTY() UInputAction* ZoomInInput = nullptr;
    UPROPERTY() UInputAction* ZoomOutInput = nullptr;
    UPROPERTY() UInputAction* UseInput = nullptr;
    UPROPERTY() UInputAction* FireInput = nullptr;
    UPROPERTY() UInputAction* KneelInput = nullptr;
    UPROPERTY() UInputAction* SaveInput = nullptr;
    UPROPERTY() UInputAction* LoadInput = nullptr;
    UPROPERTY() UInputAction* UseItemInput = nullptr;
    UPROPERTY() UInputAction* NextItemInput = nullptr;
    UPROPERTY() UInputAction* NextWeaponInput = nullptr;
    UPROPERTY() UInputAction* PrevWeaponInput = nullptr;
    UPROPERTY() UInputAction* BindingsInput = nullptr;
    UPROPERTY() UInputAction* HelpInput = nullptr;
    UPROPERTY() UInputAction* CombatInput = nullptr;
    UPROPERTY() UInputAction* JumpModInput = nullptr;
    UPROPERTY() UInputAction* StepModInput = nullptr;
    UPROPERTY() UInputAction* LookInput = nullptr;
    UPROPERTY() UInputMappingContext* Mapping = nullptr;
    FVector2D MoveInput = FVector2D::ZeroVector;
    bool bRunHeld = false;
    int32 FacingDir16 = 8;   // south
    float DiagTimer = 0.f;
    bool bDiagDone = false;
public:
    // Scripted input for automated tests: console command "Silencer.Move X Y Seconds [Run]".
    FVector2D ScriptedMove = FVector2D::ZeroVector;
    double ScriptedMoveUntil = 0.0;
    bool bScriptedRun = false;
    bool bNoAnimSwitch = false;   // Silencer.NoAnim 1: keep the current flipbook (diagnostics)
    bool bEngineAnim = false;     // the usecode engine is animating the avatar (fire/reload/death): no movement, sprite hidden
    void SetFacing(int32 Dir16) { FacingDir16 = Dir16; }
private:

    void OnMove(const struct FInputActionValue& Value);
    void OnRun(const struct FInputActionValue& Value);
    void OnRunReleased(const struct FInputActionValue& Value);
    void OnZoomIn(const struct FInputActionValue& Value);
    void OnZoomOut(const struct FInputActionValue& Value);
    void OnUse(const struct FInputActionValue& Value);
    void OnFire(const struct FInputActionValue& Value);
    void OnKneel(const struct FInputActionValue& Value);
    void OnSave(const struct FInputActionValue& Value);
    void OnLoad(const struct FInputActionValue& Value);
    void OnUseItem(const struct FInputActionValue& Value);
    void OnNextItem(const struct FInputActionValue& Value);
    void OnNextWeapon(const struct FInputActionValue& Value);
    void OnPrevWeapon(const struct FInputActionValue& Value);
    void OnBindings(const struct FInputActionValue& Value);
    void OnHelp(const struct FInputActionValue& Value);
    void OnCombat(const struct FInputActionValue& Value);
    void OnJumpMod(const struct FInputActionValue& Value); void OnJumpModReleased(const struct FInputActionValue& Value);
    void OnStepMod(const struct FInputActionValue& Value); void OnStepModReleased(const struct FInputActionValue& Value);
    bool TryModifierMove(const FVector2D& Input);
    void UpdateCursorLock();
    void OnLook(const struct FInputActionValue& Value);
public:
    void ApplyZoom();
    void RebuildBindings();   // re-map the input context from FCruKeyBindings
    int32 GetFacingDir16() const { return FacingDir16; }
    FVector AimWorld = FVector::ZeroVector;   // where the mouse points on the floor plane (UE units)
    bool bMouseAim = false;                   // the mouse moved recently: facing and shots follow it
    bool bMoving = false;
    bool bCombatStance = false, bLargeWeapon = false, bRetreating = false;   // set by the game subsystem from the engine
    UPROPERTY(EditAnywhere) int32 RetreatAction = 2;          // retreatSmall (45 = large)
    UPROPERTY(EditAnywhere) int32 CombatStandAction = 4;      // combatStandSmall (37 = large)
    double NextTurnTime = 0.0;
    bool bCursorLocked = false;
    bool bHaveMouse = false;
    bool bJumpHeld = false, bStepHeld = false, bModMoveDone = false;
    double LastMouseMoveTime = -100.0;
    double LastKeyTurnTime = -100.0;   // keyboard turning in the stance overrides the mouse facing briefly
    FVector2D CursorPos = FVector2D(-1.f, -1.f);   // software cursor in viewport pixels, driven by mouse deltas (the OS cursor is hidden and locked)
    UPROPERTY(EditAnywhere) float MouseSensitivity = 1.f;
private:
    UPaperFlipbook* FindFlipbook(int32 Action, int32 Dir8);
    void SetAnim(int32 Action, int32 Dir8);
};
