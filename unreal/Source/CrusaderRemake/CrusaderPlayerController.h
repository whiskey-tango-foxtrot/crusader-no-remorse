#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CrusaderPlayerController.generated.h"

UENUM()
enum class ECruMenu : uint8 { None, Main, Options, Bindings, Missions, Difficulty, Credits };

// Player controller with the menus drawn by ACrusaderHUD: the original Crusader menu (gump art: New Game, Load Game,
// Save Game, Options, Credits, Quit), the difficulty screen, credits, and text panels for options / key bindings /
// the mission picker. Keyboard (Up/Down/Enter/Esc, Left/Right adjust) and mouse (hit rects filled by the HUD).
// While a menu is open key presses are consumed here and the game subsystem pauses the usecode engine.
UCLASS()
class CRUSADERREMAKE_API ACrusaderPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    void OpenMenu(ECruMenu M);
    void CloseMenu();
    void CloseAllMenus();
    bool IsMenuOpen() const { return Menu != ECruMenu::None; }
    ECruMenu Menu = ECruMenu::None;
    int32 Sel = 0;
    FString Message;
    enum class ECapture { None, Replace, Add } Capture = ECapture::None;
    TArray<FString> ItemLabels() const;   // entries of the current text menu
    FString Title() const;
    static const TArray<int32>& MissionMaps();
    TArray<FBox2D> HitRects;              // screen rects of the current menu's entries (filled by the HUD each frame)
    double CreditsStart = 0.0;            // time the credits screen opened
    void Activate();
private:
    TArray<ECruMenu> Stack;               // menus to return to
    void Adjust(int32 Delta);
    bool HandleBindingsKey(const FKey& K);
    class UCrusaderGameSubsystem* Sub() const;
};
