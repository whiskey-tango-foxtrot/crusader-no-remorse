#include "CrusaderPlayerController.h"
#include "CrusaderKeyBindings.h"
#include "CrusaderSettings.h"
#include "CrusaderGameSubsystem.h"
#include "SilencerPawn.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

const TArray<int32>& ACrusaderPlayerController::MissionMaps()
{
    static const TArray<int32> Maps = { 1, 3, 5, 7, 10, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29 };   // first map of each mission
    return Maps;
}

UCrusaderGameSubsystem* ACrusaderPlayerController::Sub() const { return GetWorld() ? GetWorld()->GetSubsystem<UCrusaderGameSubsystem>() : nullptr; }

void ACrusaderPlayerController::OpenMenu(ECruMenu M)
{
    const bool bWasOpen = Menu != ECruMenu::None;
    if (bWasOpen && Menu != M) Stack.Add(Menu);
    Menu = M; Sel = (M == ECruMenu::Difficulty) ? 2 : 0; Capture = ECapture::None; Message.Reset(); HitRects.Reset();
    if (M == ECruMenu::Credits) CreditsStart = FPlatformTime::Seconds();
    if (!bWasOpen) if (UCrusaderGameSubsystem* G = Sub()) G->OnMenuOpened();
}

void ACrusaderPlayerController::CloseMenu()
{
    if (Menu == ECruMenu::Bindings) { FCruKeyBindings::Get().Save(); if (ASilencerPawn* P = Cast<ASilencerPawn>(GetPawn())) P->RebuildBindings(); }
    if (Menu == ECruMenu::Options) FCruSettings::Get().Save();
    Capture = ECapture::None; Message.Reset(); HitRects.Reset();
    if (Stack.Num() > 0) { Menu = Stack.Pop(); Sel = 0; }
    else
    {
        Menu = ECruMenu::None; Stack.Reset();
        if (UCrusaderGameSubsystem* G = Sub()) G->OnMenuClosed();
    }
}

void ACrusaderPlayerController::CloseAllMenus() { Stack.Reset(); if (Menu != ECruMenu::None) { Menu = ECruMenu::None; HitRects.Reset(); if (UCrusaderGameSubsystem* G = Sub()) G->OnMenuClosed(); } }

FString ACrusaderPlayerController::Title() const
{
    switch (Menu)
    {
    case ECruMenu::Options: return TEXT("OPTIONS");
    case ECruMenu::Bindings: return TEXT("KEY BINDINGS");
    case ECruMenu::Missions: return TEXT("MISSION PICKER");
    default: return FString();
    }
}

TArray<FString> ACrusaderPlayerController::ItemLabels() const
{
    const FCruSettings& S = FCruSettings::Get();
    TArray<FString> L;
    switch (Menu)
    {
    case ECruMenu::Main: L = { TEXT("New Game"), TEXT("Load Game"), TEXT("Save Game"), TEXT("Options"), TEXT("Credits"), TEXT("Quit") }; break;
    case ECruMenu::Difficulty: L = { TEXT("Mama's Boy"), TEXT("Weekend Warrior"), TEXT("Loose Cannon"), TEXT("No Remorse") }; break;
    case ECruMenu::Options:
        L = { TEXT("Key Bindings"), FString::Printf(TEXT("Music Volume   < %d >"), S.MusicVolume), FString::Printf(TEXT("Sound Volume   < %d >"), S.SfxVolume),
              FString::Printf(TEXT("Key Help Overlay   < %s >"), S.bShowKeyHelp ? TEXT("On") : TEXT("Off")),
              FString::Printf(TEXT("Display   < %s >"), (GEngine && GEngine->GetGameUserSettings() && GEngine->GetGameUserSettings()->GetFullscreenMode() == EWindowMode::Windowed) ? TEXT("Windowed") : TEXT("Fullscreen")),
              TEXT("Mission Picker (testing)"), TEXT("Back") };
        break;
    case ECruMenu::Missions:
        for (int32 i = 0; i < MissionMaps().Num(); ++i) L.Add(FString::Printf(TEXT("Mission %2d   (map %d)"), i + 1, MissionMaps()[i]));
        L.Add(TEXT("Back"));
        break;
    default: break;
    }
    return L;
}

void ACrusaderPlayerController::Adjust(int32 Delta)
{
    if (Menu != ECruMenu::Options) return;
    FCruSettings& S = FCruSettings::Get();
    if (Sel == 1) S.MusicVolume = FMath::Clamp(S.MusicVolume + Delta, 0, 10);
    else if (Sel == 2) S.SfxVolume = FMath::Clamp(S.SfxVolume + Delta, 0, 10);
    else if (Sel == 3) S.bShowKeyHelp = !S.bShowKeyHelp;
    else if (Sel == 4)
    {
        if (UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
        {
            GUS->SetFullscreenMode(GUS->GetFullscreenMode() == EWindowMode::Windowed ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
            GUS->ApplySettings(false); GUS->SaveSettings();
        }
        return;
    }
    else return;
    S.Save();
    if (UCrusaderGameSubsystem* G = Sub()) G->ApplyAudioSettings();
}

void ACrusaderPlayerController::Activate()
{
    UCrusaderGameSubsystem* G = Sub();
    const int32 N = ItemLabels().Num();
    if (Menu == ECruMenu::Main)
    {
        switch (Sel)
        {
        case 0: OpenMenu(ECruMenu::Difficulty); break;
        case 1: if (G && G->LoadGame(0)) CloseAllMenus(); else Message = TEXT("no saved game"); break;
        case 2: Message = (G && G->SaveGame(0)) ? TEXT("game saved") : TEXT("save failed"); break;
        case 3: OpenMenu(ECruMenu::Options); break;
        case 4: OpenMenu(ECruMenu::Credits); break;
        case 5: UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false); break;
        }
    }
    else if (Menu == ECruMenu::Difficulty)
    {
        if (G) { G->SetDifficulty(Sel + 1); CloseAllMenus(); G->NewGame(); }
    }
    else if (Menu == ECruMenu::Options)
    {
        if (Sel == 0) OpenMenu(ECruMenu::Bindings);
        else if (Sel == 5) OpenMenu(ECruMenu::Missions);
        else if (Sel == N - 1) CloseMenu();
        else Adjust(+1);
    }
    else if (Menu == ECruMenu::Missions)
    {
        if (Sel == N - 1) CloseMenu();
        else if (G) { CloseAllMenus(); G->StartMission(Sel + 1); }
    }
    else if (Menu == ECruMenu::Credits) CloseMenu();
}

bool ACrusaderPlayerController::HandleBindingsKey(const FKey& K)
{
    FCruKeyBindings& B = FCruKeyBindings::Get();
    const int32 N = B.Entries.Num();
    if (Capture != ECapture::None)
    {
        if (K == EKeys::Escape) { Capture = ECapture::None; Message = TEXT("cancelled"); return true; }
        FCruKeyBindings::FEntry& E = B.Entries[Sel];
        if (Capture == ECapture::Replace) E.Keys.Reset();
        E.Keys.AddUnique(K);
        for (FCruKeyBindings::FEntry& O : B.Entries) if (&O != &E) O.Keys.Remove(K);   // a key bound elsewhere moves here
        Capture = ECapture::None;
        Message = FString::Printf(TEXT("%s = %s"), *E.Label, *B.Describe(E));
        B.Save();
        if (ASilencerPawn* P = Cast<ASilencerPawn>(GetPawn())) P->RebuildBindings();
        return true;
    }
    if (K == EKeys::Escape || K == EKeys::F10) CloseMenu();
    else if (K == EKeys::Up) Sel = (Sel + N - 1) % N;
    else if (K == EKeys::Down) Sel = (Sel + 1) % N;
    else if (K == EKeys::Enter) { Capture = ECapture::Replace; Message = TEXT("press the new key (Esc cancels)"); }
    else if (K == EKeys::SpaceBar) { Capture = ECapture::Add; Message = TEXT("press a key to add (Esc cancels)"); }
    else if (K == EKeys::BackSpace || K == EKeys::Delete) { B.Entries[Sel].Keys.Reset(); Message = TEXT("cleared"); }
    else if (K == EKeys::R) { B.ResetDefaults(); Message = TEXT("defaults restored"); B.Save(); if (ASilencerPawn* P = Cast<ASilencerPawn>(GetPawn())) P->RebuildBindings(); }
    else if (K == EKeys::LeftMouseButton)
    {
        if (ASilencerPawn* P = Cast<ASilencerPawn>(GetPawn()))
            for (int32 i = 0; i < HitRects.Num(); ++i) if (HitRects[i].IsInside(P->CursorPos)) { Sel = i; Capture = ECapture::Replace; Message = TEXT("press the new key (Esc cancels)"); }
    }
    return true;
}

bool ACrusaderPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    const FKey& K = Params.Key;
    UCrusaderGameSubsystem* G = Sub();
    if (Menu == ECruMenu::None)
    {
        if (K == EKeys::Escape && Params.Event == IE_Pressed)
        {
            if (G && G->IsMoviePlaying()) { UE_LOG(LogTemp, Display, TEXT("Esc skips the movie")); G->SkipMovie(); }           // Esc skips cutscenes and the intro
            else if (G && G->StartupStep == 0) OpenMenu(ECruMenu::Main);
            return true;
        }
        return Super::InputKey(Params);
    }
    // releases still reach the pawn so held movement keys stop cleanly; presses are consumed by the menu
    if (Params.Event == IE_Released) return Super::InputKey(Params);
    if (Params.Event != IE_Pressed) return true;
    if (K.IsAxis1D() || K.IsAxis2D() || K.IsAxis3D() || K.IsTouch()) return true;
    UE_LOG(LogTemp, Display, TEXT("menu key %s (menu %d)"), *K.ToString(), (int32)Menu);
    if (Menu == ECruMenu::Bindings) return HandleBindingsKey(K);
    const int32 N = ItemLabels().Num();
    if (Menu == ECruMenu::Credits) { if (K == EKeys::Escape || K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::LeftMouseButton) CloseMenu(); return true; }
    if (K == EKeys::LeftMouseButton)
    {
        if (ASilencerPawn* P = Cast<ASilencerPawn>(GetPawn()))
            for (int32 i = 0; i < HitRects.Num(); ++i) if (HitRects[i].IsInside(P->CursorPos)) { Sel = i; Activate(); break; }
        return true;
    }
    if (K == EKeys::Escape)
    {
        if (Menu == ECruMenu::Difficulty) return true;   // the original does not allow leaving without choosing
        if (Menu == ECruMenu::Main) { if (G && G->IsRunning()) CloseAllMenus(); }
        else CloseMenu();
    }
    else if (K == EKeys::Up) Sel = N ? (Sel + N - 1) % N : 0;
    else if (K == EKeys::Down) Sel = N ? (Sel + 1) % N : 0;
    else if (K == EKeys::Left) Adjust(-1);
    else if (K == EKeys::Right) Adjust(+1);
    else if (K == EKeys::Enter || K == EKeys::SpaceBar) Activate();
    else if (Menu == ECruMenu::Main || Menu == ECruMenu::Difficulty)
    {
        // number keys choose entries like the original (1-6 / 1-4)
        static const FKey Nums[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
        for (int32 i = 0; i < 6 && i < N; ++i) if (K == Nums[i]) { Sel = i; Activate(); break; }
    }
    return true;
}
