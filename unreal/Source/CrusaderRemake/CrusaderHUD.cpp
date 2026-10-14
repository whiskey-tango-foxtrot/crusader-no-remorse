#include "CrusaderHUD.h"
#include "CrusaderKeyBindings.h"
#include "CrusaderPlayerController.h"
#include "CrusaderSettings.h"
#include "SilencerPawn.h"
#include "CrusaderGameSubsystem.h"
#include "Usecode/CruEngine.h"
#include "Usecode/CruWorld.h"
#include "Usecode/CruCombat.h"
#include "Usecode/CruInventory.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "MediaTexture.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Extracted 2D shapes are sheets of up to 16 frames per row; cell = sheet size / (columns, rows).
const ACrusaderHUD::FGumpSheet* ACrusaderHUD::GetSheet(const FString& Kind, int32 Shape)
{
    const FString Key = FString::Printf(TEXT("%s/%d"), *Kind, Shape);
    if (const FGumpSheet* S = Sheets.Find(Key)) return S->Tex ? S : nullptr;
    FGumpSheet& S = Sheets.Add(Key);
    UCrusaderGameSubsystem* Sub = GetWorld()->GetSubsystem<UCrusaderGameSubsystem>();
    const FString Base = (Sub && Sub->GetEngine()) ? Sub->GetEngine()->DataDir / TEXT("extracted") / Kind : FString();
    if (Base.IsEmpty()) return nullptr;
    TSharedPtr<FJsonObject>* M = Manifests.Find(Kind);
    if (!M)
    {
        FString Txt; TSharedPtr<FJsonObject> Obj;
        if (FFileHelper::LoadFileToString(Txt, *(Base / TEXT("manifest.json")))) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Txt), Obj);
        M = &Manifests.Add(Kind, Obj);
    }
    if (!M->IsValid()) return nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
    if (!(*M)->TryGetArrayField(FString::FromInt(Shape), Frames) || !Frames) return nullptr;
    S.Tex = FImageUtils::ImportFileAsTexture2D(Base / FString::Printf(TEXT("%04d.png"), Shape));
    if (!S.Tex) return nullptr;
    HeldTextures.Add(S.Tex);
    S.TexW = S.Tex->GetSizeX(); S.TexH = S.Tex->GetSizeY();
    const int32 N = Frames->Num(), Cols = FMath::Clamp(N, 1, 16), Rows = (N + 15) / 16;
    const int32 CellW = S.TexW / Cols, CellH = S.TexH / FMath::Max(1, Rows);
    for (int32 i = 0; i < N; ++i)
    {
        const TSharedPtr<FJsonObject>* F = nullptr; FGumpFrame G;
        if ((*Frames)[i]->TryGetObject(F) && F)
        {
            G.W = (int32)(*F)->GetNumberField(TEXT("w")); G.H = (int32)(*F)->GetNumberField(TEXT("h"));
            G.XOff = (int32)(*F)->GetNumberField(TEXT("xoff")); G.YOff = (int32)(*F)->GetNumberField(TEXT("yoff"));
        }
        G.CellX = (i % 16) * CellW; G.CellY = (i / 16) * CellH;
        S.Frames.Add(G);
    }
    return &S;
}

void ACrusaderHUD::DrawGump(int32 Shape, int32 Frame, float X, float Y, float Alpha)
{
    const FGumpSheet* S = GetSheet(TEXT("gumps"), Shape);
    if (!S || !S->Frames.IsValidIndex(Frame) || S->Frames[Frame].W <= 1) return;
    const FGumpFrame& F = S->Frames[Frame];
    DrawTexture(S->Tex, X * Scale, Y * Scale, F.W * Scale, F.H * Scale, (float)F.CellX / S->TexW, (float)F.CellY / S->TexH, (float)F.W / S->TexW, (float)F.H / S->TexH, FLinearColor(1, 1, 1, Alpha));
}

void ACrusaderHUD::DrawGumpCentred(int32 Shape, int32 Frame, float Cx, float Cy, float Alpha)
{
    const FGumpSheet* S = GetSheet(TEXT("gumps"), Shape);
    if (!S || !S->Frames.IsValidIndex(Frame)) return;
    DrawGump(Shape, Frame, Cx - S->Frames[Frame].W * 0.5f, Cy - S->Frames[Frame].H * 0.5f, Alpha);
}

float ACrusaderHUD::GumpTextWidth(int32 Font, const FString& Text)
{
    const FGumpSheet* S = GetSheet(TEXT("fonts"), Font);
    if (!S) return 0.f;
    float W = 0.f;
    for (TCHAR C : Text) { const int32 I = (int32)C; W += (S->Frames.IsValidIndex(I) ? S->Frames[I].W : 3) + 1; }
    return W;
}

void ACrusaderHUD::DrawGumpText(int32 Font, const FString& Text, float X, float Y, bool bCentre)
{
    const FGumpSheet* S = GetSheet(TEXT("fonts"), Font);
    if (!S) return;
    if (bCentre) X -= GumpTextWidth(Font, Text) * 0.5f;
    for (TCHAR C : Text)
    {
        const int32 I = (int32)C;
        if (!S->Frames.IsValidIndex(I)) { X += 4; continue; }
        const FGumpFrame& F = S->Frames[I];
        if (F.W > 1) DrawTexture(S->Tex, X * Scale, Y * Scale, F.W * Scale, F.H * Scale, (float)F.CellX / S->TexW, (float)F.CellY / S->TexH, (float)F.W / S->TexW, (float)F.H / S->TexH);
        X += F.W + 1;
    }
}

// CruStatusGump: five 108x28 boxes (shape 1) 15 px from the left, 2 px from the bottom, 17 px apart
void ACrusaderHUD::DrawStatusBar(FCruEngine* E)
{
    const FCruItem* Av = E->World->GetItem(E->World->AvatarId);
    if (!Av) return;
    const float Alpha = 0.8f, BoxW = 108.f, BoxH = 28.f, Gap = 17.f, Y = 480.f - 2.f - BoxH;
    auto BoxX = [&](int32 i) { return 15.f + i * (BoxW + Gap); };
    const FCruItem* W = E->World->GetItem(Av->ActiveWeapon);
    const FCruWeaponInfo* Wi = W ? CruGetWeaponInfo(W->Shape) : nullptr;
    // weapon
    DrawGump(1, 0, BoxX(0), Y, Alpha);
    if (W) if (const FCruGumpIcon* Ic = CruGetGumpIcon(W->Shape)) DrawGumpCentred(Ic->GumpShape, Ic->GumpFrame, BoxX(0) + BoxW * 0.5f, Y + BoxH * 0.5f);
    // ammo: bullets left, clips right (font 15), only for weapons with ammo
    if (Wi && Wi->AmmoType)
    {
        DrawGump(1, 1, BoxX(1), Y, Alpha);
        int32 Clips = 0;
        if (Wi->AmmoShape) if (const FCruItem* A = CruInventory::GetFirstItemWithShape(*Av, Wi->AmmoShape, true)) Clips = A->Quality;
        DrawGumpText(15, FString::FromInt(W->Quality), BoxX(1) + 22, Y + BoxH * 0.5f - 3);
        DrawGumpText(15, FString::FromInt(Clips), BoxX(1) + BoxW * 0.5f + 22, Y + BoxH * 0.5f - 3);
    }
    // inventory: active item icon + count (font 12)
    if (const FCruItem* It = E->World->GetItem(E->ActiveInvItem))
    {
        DrawGump(1, 0, BoxX(2), Y, Alpha);
        if (const FCruGumpIcon* Ic = CruGetGumpIcon(It->Shape)) DrawGumpCentred(Ic->GumpShape, Ic->GumpFrame + (It->Shape == 0x111 ? It->Frame : 0), BoxX(2) + BoxW * 0.5f, Y + BoxH * 0.5f);
        if (It->Quality > 1) DrawGumpText(12, FString::FromInt(It->Quality), BoxX(2) + BoxW * 0.5f + 22, Y + BoxH * 0.5f + 3);
    }
    // health (palette 65) and energy (palette 245) bars: rect (34,7)-(34+w,21), max width 67
    DrawGump(1, 2, BoxX(3), Y, Alpha);
    const int32 Hw = E->AvatarMaxHp ? FMath::Clamp(Av->Hp * 67 / E->AvatarMaxHp, 0, 67) : 67;
    DrawRect(FLinearColor(0.f, 48 / 255.f, 112 / 255.f, 1.f), (BoxX(3) + 34) * Scale, (Y + 7) * Scale, Hw * Scale, 14 * Scale);
    DrawGump(1, 3, BoxX(4), Y, Alpha);
    const int32 MaxE = CruInventory::GetMaxEnergy();
    const int32 Ew = MaxE ? FMath::Clamp(Av->Mana * 67 / MaxE, 0, 67) : 0;
    DrawRect(FLinearColor(152 / 255.f, 4 / 255.f, 4 / 255.f, 1.f), (BoxX(4) + 34) * Scale, (Y + 7) * Scale, Ew * Scale, 14 * Scale);
}

// CruPickupAreaGump / CruPickupGump: rows of 5 translucent 28x28 tiles (shape 2) at (5,5), icon centred in a 60 px area,
// name in font 13, count in font 12; each stays 90 frames
void ACrusaderHUD::DrawPickups(FCruEngine* E)
{
    const uint32 Now = E->Kernel->GetTickNum();
    float Y = 5.f;
    for (const FCruEngine::FPickupToast& T : E->Pickups)
    {
        if (Now - T.StartTick > 180) continue;
        const FCruGumpIcon* Ic = CruGetGumpIcon(T.Shape);
        if (!Ic) continue;
        for (int32 i = 0; i < 5; ++i) DrawGump(2, 0, 5.f + i * 28.f, Y, 0.7f);
        DrawGumpCentred(Ic->GumpShape, Ic->GumpFrame + (T.Shape == 0x111 ? T.Frame : 0), 5.f + 30.f, Y + 14.f);
        DrawGumpText(13, Ic->Name, 5.f + 60.f, Y + 14.f - 5.f);
        if (T.bShowCount && T.Q > 1) DrawGumpText(12, FString::FromInt(T.Q), 5.f + 30.f + 22.f, Y + 14.f + 3.f);
        Y += 35.f;
    }
}

void ACrusaderHUD::DrawHUD()
{
    Super::DrawHUD();
    UCrusaderGameSubsystem* S = GetWorld() ? GetWorld()->GetSubsystem<UCrusaderGameSubsystem>() : nullptr;
    FCruEngine* E = S ? S->GetEngine() : nullptr;
    if (!E || !Canvas) return;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    Scale = H / 480.f;
    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    if (UMediaTexture* Movie = S->GetMovieTexture())
    {
        DrawRect(FLinearColor::Black, 0, 0, W, H);
        const float Vh = H, Vw = H * 4.f / 3.f, Vx = (W - Vw) * 0.5f;
        DrawTexture(Movie, Vx, 0, Vw, Vh, 0, 0, 1, 1);
        if (Font) DrawText(TEXT("fire to skip"), FLinearColor(0.7f, 0.7f, 0.7f, 1.f), 16, H - 28, Font, 1.f);
        return;
    }
    DrawStatusBar(E);
    DrawPickups(E);
    // crosshair (shape 0x4CC) at the software cursor
    if (!bCrosshairTried) { bCrosshairTried = true; Crosshair = FImageUtils::ImportFileAsTexture2D(E->DataDir / TEXT("extracted/shapes_x2/1228/000.png")); if (Crosshair) HeldTextures.Add(Crosshair); }
    ACrusaderPlayerController* CPC = Cast<ACrusaderPlayerController>(GetOwningPlayerController());
    const bool bMenu = CPC && CPC->IsMenuOpen();
    if (!bMenu) DrawKeyHelp(Font);
    if (E->FadeAlpha > 0.f) DrawRect(FLinearColor(E->FadeColor.R / 255.f, E->FadeColor.G / 255.f, E->FadeColor.B / 255.f, E->FadeAlpha), 0, 0, W, H);
    DrawBindingsMenu(Font);
    DrawMenu(Font);
    if (Crosshair)
        if (ASilencerPawn* Pawn = CPC ? Cast<ASilencerPawn>(CPC->GetPawn()) : nullptr)
            if (Pawn->CursorPos.X >= 0.f) { const float Sz = 13.f * Scale; DrawTexture(Crosshair, Pawn->CursorPos.X - Sz * 0.5f, Pawn->CursorPos.Y - Sz * 0.5f, Sz, Sz, 0, 0, 1, 1); }
    if (E->bShowDeathText && Font)
    {
        const FString Txt = TEXT("SILENCER TERMINATED");
        float Tw = 0, Th = 0; GetTextSize(Txt, Tw, Th, Font, 2.f);
        DrawText(Txt, FLinearColor::Red, (W - Tw) * 0.5f, (H - Th) * 0.5f, Font, 2.f);
    }
}

void ACrusaderHUD::DrawGumpKind(const FString& Kind, int32 Shape, int32 Frame, float X, float Y, FBox2D* OutRect)
{
    const FGumpSheet* S = GetSheet(Kind, Shape);
    if (!S || !S->Frames.IsValidIndex(Frame)) return;
    const FGumpFrame& F = S->Frames[Frame];
    DrawTexture(S->Tex, X * Scale, Y * Scale, F.W * Scale, F.H * Scale, (float)F.CellX / S->TexW, (float)F.CellY / S->TexH, (float)F.W / S->TexW, (float)F.H / S->TexH);
    if (OutRect) *OutRect = FBox2D(FVector2D(X * Scale, Y * Scale), FVector2D((X + F.W) * Scale, (Y + F.H) * Scale));
}

void ACrusaderHUD::DrawBindingsMenu(UFont* Font)
{
    ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(GetOwningPlayerController());
    if (!PC || PC->Menu != ECruMenu::Bindings || !Font) return;
    const FCruKeyBindings& B = FCruKeyBindings::Get();
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const float TextScale = FMath::Max(1.f, Scale * 0.9f);
    float Lw = 0, Lh = 0; GetTextSize(TEXT("Xg"), Lw, Lh, Font, TextScale);
    const float Row = Lh + 4.f * Scale;
    const float PanelW = FMath::Min(W - 32.f, 600.f * Scale), PanelH = Row * (B.Entries.Num() + 5) + 16.f * Scale;
    const float X0 = (W - PanelW) * 0.5f, Y0 = (H - PanelH) * 0.5f;
    DrawRect(FLinearColor(0, 0, 0, 0.85f), X0, Y0, PanelW, PanelH);
    float Y = Y0 + 8.f * Scale;
    DrawText(PC->Title(), FLinearColor(1, 0.85f, 0.3f), X0 + 12.f * Scale, Y, Font, TextScale); Y += Row * 1.5f;
    PC->HitRects.Reset();
    for (int32 i = 0; i < B.Entries.Num(); ++i)
    {
        const bool bSel = i == PC->Sel;
        if (bSel) DrawRect(FLinearColor(0.25f, 0.25f, 0.45f, 0.9f), X0 + 4.f * Scale, Y - 2.f * Scale, PanelW - 8.f * Scale, Row);
        PC->HitRects.Add(FBox2D(FVector2D(X0, Y - 2.f * Scale), FVector2D(X0 + PanelW, Y - 2.f * Scale + Row)));
        const FLinearColor Col = bSel ? FLinearColor::White : FLinearColor(0.75f, 0.75f, 0.75f);
        DrawText(B.Entries[i].Label, Col, X0 + 12.f * Scale, Y, Font, TextScale);
        const FString Keys = (bSel && PC->Capture != ACrusaderPlayerController::ECapture::None) ? TEXT("...") : B.Describe(B.Entries[i]);
        DrawText(Keys, Col, X0 + PanelW * 0.5f, Y, Font, TextScale);
        Y += Row;
    }
    Y += Row * 0.5f;
    DrawText(TEXT("Up/Down or click select   Enter rebind   Space add key   Backspace clear   R defaults   Esc back"), FLinearColor(0.6f, 0.6f, 0.6f), X0 + 12.f * Scale, Y, Font, TextScale * 0.8f); Y += Row;
    if (!PC->Message.IsEmpty()) DrawText(PC->Message, FLinearColor(0.5f, 1.f, 0.5f), X0 + 12.f * Scale, Y, Font, TextScale * 0.9f);
}

// The original Crusader menu (CruMenuGump): four background quarters (gumps 54-57, MISC palette) and six two-frame
// buttons (58-63); the pressed frame marks the selection. Drawn in 640x480 units centred on the screen.
void ACrusaderHUD::DrawMainMenu(UFont* Font)
{
    ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(GetOwningPlayerController());
    if (!PC) return;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const float Ox = (W / Scale - 640.f) * 0.5f, Oy = 120.f;
    DrawRect(FLinearColor::Black, 0, 0, W, H);
    DrawGumpKind(TEXT("gumps_misc"), 54, 0, Ox, Oy);
    DrawGumpKind(TEXT("gumps_misc"), 55, 0, Ox + 319.f, Oy);
    DrawGumpKind(TEXT("gumps_misc"), 56, 0, Ox, Oy + 120.f);
    DrawGumpKind(TEXT("gumps_misc"), 57, 0, Ox + 319.f, Oy + 120.f);
    static const int32 Bx[] = { 45, 45, 45, 446, 488, 550 }, By[] = { 50, 101, 151, 58, 151, 198 };
    PC->HitRects.SetNum(6);
    for (int32 i = 0; i < 6; ++i) DrawGumpKind(TEXT("gumps_misc"), 58 + i, PC->Sel == i ? 1 : 0, Ox + Bx[i], Oy + By[i], &PC->HitRects[i]);
    if (Font)
    {
        const float TextScale = FMath::Max(1.f, Scale * 0.8f);
        DrawText(TEXT("Up/Down or mouse   Enter / click   1-6   Esc resumes"), FLinearColor(0.5f, 0.5f, 0.5f), (Ox + 45.f) * Scale, (Oy + 250.f) * Scale, Font, TextScale);
        if (!PC->Message.IsEmpty()) DrawText(PC->Message, FLinearColor(0.5f, 1.f, 0.5f), (Ox + 45.f) * Scale, (Oy + 270.f) * Scale, Font, TextScale);
    }
}

// DifficultyGump: title 73 at (185,77), levels 74-77 (left + right frames) at x 158, y 120 + 59 * i, DIFF palette.
void ACrusaderHUD::DrawDifficultyMenu(UFont* Font)
{
    ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(GetOwningPlayerController());
    if (!PC) return;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const float Ox = (W / Scale - 640.f) * 0.5f;
    DrawRect(FLinearColor::Black, 0, 0, W, H);
    DrawGumpKind(TEXT("gumps_diff"), 73, 0, Ox + 185.f, 77.f);
    PC->HitRects.SetNum(4);
    for (int32 i = 0; i < 4; ++i)
    {
        const float Y = 120.f + 59.f * i;
        const FGumpSheet* S = GetSheet(TEXT("gumps_diff"), 74 + i);
        const float Lw = (S && S->Frames.Num() > 0) ? S->Frames[0].W : 170.f, Rw = (S && S->Frames.Num() > 1) ? S->Frames[1].W : 151.f, Hh = (S && S->Frames.Num() > 0) ? S->Frames[0].H : 42.f;
        if (PC->Sel == i) DrawRect(FLinearColor(0.5f, 0.5f, 0.5f), (Ox + 158.f - 2.f) * Scale, (Y - 2.f) * Scale, (Lw + Rw + 4.f) * Scale, (Hh + 4.f) * Scale);
        DrawGumpKind(TEXT("gumps_diff"), 74 + i, 0, Ox + 158.f, Y);
        DrawGumpKind(TEXT("gumps_diff"), 74 + i, 1, Ox + 158.f + Lw, Y);
        PC->HitRects[i] = FBox2D(FVector2D((Ox + 158.f) * Scale, Y * Scale), FVector2D((Ox + 158.f + Lw + Rw) * Scale, (Y + Hh) * Scale));
    }
}

// CruCreditsGump: screens of title / name lines from static/credits.dat, each shown for its delay (frames at 60 Hz).
void ACrusaderHUD::DrawCredits(UFont* Font)
{
    ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(GetOwningPlayerController());
    UCrusaderGameSubsystem* S = GetWorld() ? GetWorld()->GetSubsystem<UCrusaderGameSubsystem>() : nullptr;
    if (!PC || !S || !Font) return;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    DrawRect(FLinearColor::Black, 0, 0, W, H);
    if (!S->LoadCredits()) { DrawText(TEXT("credits text not found (data/cd/STATIC/CREDITS.DAT)"), FLinearColor::White, 40, H * 0.5f, Font, Scale); return; }
    const double Frames = (FPlatformTime::Seconds() - PC->CreditsStart) * 60.0;
    double Acc = 0; int32 Screen = -1;
    for (int32 i = 0; i < S->Credits.Num(); ++i) { Acc += S->Credits[i].Delay; if (Frames < Acc) { Screen = i; break; } }
    if (Screen < 0) { PC->CloseMenu(); return; }
    const TArray<UCrusaderGameSubsystem::FCredLine>& Lines = S->Credits[Screen].Lines;
    const float TitleScale = Scale * 1.3f, NameScale = Scale * 1.1f;
    float Lh = 0, Tmp = 0; GetTextSize(TEXT("Xg"), Tmp, Lh, Font, NameScale);
    const float Row = Lh + 6.f * Scale;
    float Y = (H - Row * Lines.Num()) * 0.5f;
    for (const UCrusaderGameSubsystem::FCredLine& L : Lines)
    {
        float Tw = 0, Th = 0; GetTextSize(L.Text, Tw, Th, Font, L.bTitle ? TitleScale : NameScale);
        DrawText(L.Text, L.bTitle ? FLinearColor(0.4f, 1.f, 0.4f) : FLinearColor::White, (W - Tw) * 0.5f, Y, Font, L.bTitle ? TitleScale : NameScale);
        Y += Row + (L.bTitle ? 4.f * Scale : 0.f);
    }
    DrawText(TEXT("Esc / Enter: back"), FLinearColor(0.5f, 0.5f, 0.5f), 16, H - 28, Font, 1.f);
}

void ACrusaderHUD::DrawMenu(UFont* Font)
{
    ACrusaderPlayerController* PC = Cast<ACrusaderPlayerController>(GetOwningPlayerController());
    if (!PC || PC->Menu == ECruMenu::None || PC->Menu == ECruMenu::Bindings) return;
    if (PC->Menu == ECruMenu::Main) { DrawMainMenu(Font); return; }
    if (PC->Menu == ECruMenu::Difficulty) { DrawDifficultyMenu(Font); return; }
    if (PC->Menu == ECruMenu::Credits) { DrawCredits(Font); return; }
    if (!Font) return;
    const TArray<FString> Items = PC->ItemLabels();
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const float TextScale = FMath::Max(1.f, Scale * 1.1f);
    float Lw = 0, Lh = 0; GetTextSize(TEXT("Xg"), Lw, Lh, Font, TextScale);
    const float Row = Lh + 6.f * Scale;
    const float PanelW = FMath::Min(W - 32.f, 460.f * Scale), PanelH = Row * (Items.Num() + 3) + 24.f * Scale;
    const float X0 = (W - PanelW) * 0.5f, Y0 = (H - PanelH) * 0.5f;
    DrawRect(FLinearColor(0, 0, 0, 0.85f), X0, Y0, PanelW, PanelH);
    float Y = Y0 + 12.f * Scale;
    float Tw = 0, Th = 0; GetTextSize(PC->Title(), Tw, Th, Font, TextScale * 1.2f);
    DrawText(PC->Title(), FLinearColor(1, 0.85f, 0.3f), X0 + (PanelW - Tw) * 0.5f, Y, Font, TextScale * 1.2f); Y += Row * 1.6f;
    PC->HitRects.Reset();
    for (int32 i = 0; i < Items.Num(); ++i)
    {
        const bool bSel = i == PC->Sel;
        if (bSel) DrawRect(FLinearColor(0.25f, 0.25f, 0.45f, 0.9f), X0 + 8.f * Scale, Y - 3.f * Scale, PanelW - 16.f * Scale, Row);
        PC->HitRects.Add(FBox2D(FVector2D(X0, Y - 3.f * Scale), FVector2D(X0 + PanelW, Y - 3.f * Scale + Row)));
        DrawText(Items[i], bSel ? FLinearColor::White : FLinearColor(0.75f, 0.75f, 0.75f), X0 + 24.f * Scale, Y, Font, TextScale);
        Y += Row;
    }
    Y += Row * 0.3f;
    const TCHAR* Hint = PC->Menu == ECruMenu::Options ? TEXT("Up/Down select   Left/Right adjust   Enter / click   Esc back") : TEXT("Up/Down select   Enter / click   Esc back");
    DrawText(Hint, FLinearColor(0.6f, 0.6f, 0.6f), X0 + 12.f * Scale, Y, Font, TextScale * 0.7f);
    if (!PC->Message.IsEmpty()) DrawText(PC->Message, FLinearColor(0.5f, 1.f, 0.5f), X0 + 12.f * Scale, Y + Row * 0.8f, Font, TextScale * 0.8f);
}

void ACrusaderHUD::DrawKeyHelp(UFont* Font)
{
    if (!Font || !FCruSettings::Get().bShowKeyHelp) return;
    const FCruKeyBindings& B = FCruKeyBindings::Get();
    const float W = Canvas->SizeX;
    const float TextScale = FMath::Max(0.8f, Scale * 0.7f);
    float Lw = 0, Lh = 0; GetTextSize(TEXT("Xg"), Lw, Lh, Font, TextScale);
    const float Row = Lh + 2.f * Scale;
    TArray<FString> Lines;
    for (const FCruKeyBindings::FEntry& E : B.Entries) Lines.Add(FString::Printf(TEXT("%s: %s"), *E.Label, *B.Describe(E)));
    Lines.Add(TEXT("Esc: menu"));
    float MaxW = 0.f;
    for (const FString& L : Lines) { float Tw = 0, Th = 0; GetTextSize(L, Tw, Th, Font, TextScale); MaxW = FMath::Max(MaxW, Tw); }
    const float PanelW = MaxW + 16.f * Scale, PanelH = Row * Lines.Num() + 12.f * Scale;
    const float X0 = W - PanelW - 8.f * Scale, Y0 = 8.f * Scale;
    DrawRect(FLinearColor(0, 0, 0, 0.6f), X0, Y0, PanelW, PanelH);
    float Y = Y0 + 6.f * Scale;
    for (const FString& L : Lines) { DrawText(L, FLinearColor(0.85f, 0.85f, 0.85f), X0 + 8.f * Scale, Y, Font, TextScale); Y += Row; }
}
