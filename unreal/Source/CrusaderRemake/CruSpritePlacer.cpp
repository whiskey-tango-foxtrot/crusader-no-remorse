#include "CruSpritePlacer.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FCruSpritePlacer::LoadAtlas(const FString& Path)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogCru, Error, TEXT("placer: cannot read %s"), *Path); return false; }
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
    AtlasSize = (int32)Root->GetNumberField(TEXT("size"));
    Scale = (int32)Root->GetNumberField(TEXT("scale"));
    NumAtlases = Root->GetArrayField(TEXT("atlases")).Num();
    const TSharedPtr<FJsonObject>& F = Root->GetObjectField(TEXT("frames"));
    for (auto& P : F->Values)
    {
        FString L, R; const FString Key(P.Key); if (!Key.Split(TEXT(":"), &L, &R)) continue;
        const TSharedPtr<FJsonObject>& O = P.Value->AsObject();
        FCruAtlasFrame Fr;
        Fr.Atlas = (int32)O->GetNumberField(TEXT("atlas")); Fr.X = (int32)O->GetNumberField(TEXT("x")); Fr.Y = (int32)O->GetNumberField(TEXT("y"));
        Fr.W = (int32)O->GetNumberField(TEXT("w")); Fr.H = (int32)O->GetNumberField(TEXT("h")); Fr.XOff = (int32)O->GetNumberField(TEXT("xoff")); Fr.YOff = (int32)O->GetNumberField(TEXT("yoff"));
        Frames.Add(((uint32)FCString::Atoi(*L) << 16) | ((uint32)FCString::Atoi(*R) & 0xFFFF), Fr);
    }
    UE_LOG(LogCru, Display, TEXT("placer: %d frames, %d atlases of %d, scale %d"), Frames.Num(), NumAtlases, AtlasSize, Scale);
    return true;
}

namespace
{
    struct FBasis { FVector R; float Sxu; FRotator Rot; };
    const FVector Right = FVector(1, -1, 0).GetSafeNormal();
    const FVector FUp = FVector(-1, -1, 0).GetSafeNormal();
    FVector CamUp()
    {
        const FVector Fwd = FRotator(-30.f, 225.f, 0.f).Vector();
        return (FVector::UpVector - Fwd * FVector::DotProduct(FVector::UpVector, Fwd)).GetSafeNormal();
    }
    FRotator RotFromXY(const FVector& X, const FVector& Y) { return FRotationMatrix::MakeFromXY(X, Y).Rotator(); }
    FBasis AlignedBasis(float Ex, float Ey)
    {
        const FVector E = FVector(Ex, Ey, 0).GetSafeNormal(); const FVector Up = CamUp();
        const float A = FCruSpritePlacer::PX / FVector::DotProduct(Right, E);
        const float B = -A * FVector::DotProduct(E, Up);
        FVector R = E * A + Up * B; const float Ln = R.Size(); R /= Ln;
        return { R, Ln, RotFromXY(R, Up) };
    }
}

bool FCruSpritePlacer::Place(const FCruItem& It, const FCruShapeInfo& Si, FTransform& Out, float Custom[5], int32& OutAtlas) const
{
    const FCruAtlasFrame* Fr = FindFrame(It.Shape, It.Frame);
    if (!Fr) return false;
    static const FVector Up = CamUp();
    static const FRotator BRot = RotFromXY(Right, Up), FRot = RotFromXY(Right, FUp);
    static const FBasis XWall = AlignedBasis(-1, 0), YWall = AlignedBasis(0, -1);
    int32 Xd, Yd, Zd; Si.GetFootpadWorld(Xd, Yd, Zd, It.IsFlipped());
    const bool bFlat = Zd == 0 && Xd > 0 && Yd > 0;
    const FVector Anchor((float)It.X, (float)It.Y, It.Z * ZK);
    FVector R, U; float Sxu, Syu; FRotator Rot; FVector Loc = Anchor;
    if (bFlat) { Sxu = PX; Syu = 2 * PX; R = Right; U = FUp; Rot = FRot; Loc.Z += 0.05f; }
    else if (Xd >= 2 * Yd && Xd >= 64) { R = XWall.R; Sxu = XWall.Sxu; Rot = XWall.Rot; Syu = ZK; U = Up; }
    else if (Yd >= 2 * Xd && Yd >= 64) { R = YWall.R; Sxu = YWall.Sxu; Rot = YWall.Rot; Syu = ZK; U = Up; }
    else { Sxu = PX; Syu = ZK; R = Right; U = Up; Rot = BRot; }
    const float Fl = It.IsFlipped() ? -1.f : 1.f;
    const float Cx = (Fr->W / 2.f - Fr->XOff) * Sxu * Fl;
    const float Cy = (Fr->YOff - Fr->H / 2.f) * Syu;
    const FVector Centre = Loc + R * Cx + U * Cy;
    const FVector Scale3((Fr->W * Sxu / 100.f) * Fl, Fr->H * Syu / 100.f, 1.f);
    Out = FTransform(Rot, Centre, Scale3);
    const float S = (float)AtlasSize, K = (float)Scale;
    Custom[0] = Fr->X / S; Custom[1] = (Fr->Y + Fr->H * K) / S; Custom[2] = Fr->W * K / S; Custom[3] = -(Fr->H * K / S); Custom[4] = 1.f;
    OutAtlas = Fr->Atlas;
    return true;
}
