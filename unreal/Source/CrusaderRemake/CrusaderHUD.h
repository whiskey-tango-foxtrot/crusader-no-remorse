#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CrusaderHUD.generated.h"

// Draws the usecode-driven presentation with the original gump art: the Crusader status bar (weapon, ammo, inventory,
// health, energy boxes), pickup notifications, palette fades, cutscene video and the death screen.
UCLASS()
class CRUSADERREMAKE_API ACrusaderHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    struct FGumpFrame { int32 W = 0, H = 0, XOff = 0, YOff = 0, CellX = 0, CellY = 0; };
    struct FGumpSheet { UTexture2D* Tex = nullptr; TArray<FGumpFrame> Frames; int32 TexW = 0, TexH = 0; };
    const FGumpSheet* GetSheet(const FString& Kind, int32 Shape);   // Kind = gumps | fonts (data/extracted/<Kind>/NNNN.png + manifest.json)
    void DrawGump(int32 Shape, int32 Frame, float X, float Y, float Alpha = 1.f);           // X,Y in original 640x480 pixels, top-left of the frame
    void DrawGumpCentred(int32 Shape, int32 Frame, float Cx, float Cy, float Alpha = 1.f);
    float GumpTextWidth(int32 Font, const FString& Text);
    void DrawGumpText(int32 Font, const FString& Text, float X, float Y, bool bCentre = false);
    void DrawStatusBar(class FCruEngine* E);
    void DrawPickups(class FCruEngine* E);
    void DrawBindingsMenu(class UFont* Font);
    void DrawMenu(class UFont* Font);
    void DrawMainMenu(class UFont* Font);
    void DrawDifficultyMenu(class UFont* Font);
    void DrawCredits(class UFont* Font);
    void DrawGumpKind(const FString& Kind, int32 Shape, int32 Frame, float X, float Y, FBox2D* OutRect = nullptr);
    void DrawKeyHelp(class UFont* Font);
    TMap<FString, FGumpSheet> Sheets;
    TMap<FString, TSharedPtr<class FJsonObject>> Manifests;
    UPROPERTY() TArray<UTexture2D*> HeldTextures;
    float Scale = 1.f;
    UPROPERTY() UTexture2D* Crosshair = nullptr; bool bCrosshairTried = false;
};
