#pragma once
#include "CoreMinimal.h"

// World mapping shared with unreal/Scripts/build_level3d.py (see notes/README.md).
namespace CrusaderWorld
{
    constexpr float PX = 2.8284271f;                 // UE units per sprite pixel (horizontal)
    constexpr float ZK = PX / 0.8660254f;            // UE units per engine z unit (= vertical sprite pixel)
    inline FRotator CameraRotation() { return FRotator(-30.f, 225.f, 0.f); }
    inline FVector ScreenRight() { return FVector(1, -1, 0).GetSafeNormal(); }
    inline FVector ScreenUpOnGround() { return FVector(-1, -1, 0).GetSafeNormal(); }
    // Sprite billboard rotation: local X = screen right, local Z = camera up.
    inline FRotator BillboardRotation()
    {
        const FVector Fwd = CameraRotation().Vector();
        const FVector Up = (FVector::UpVector - Fwd * FVector::DotProduct(FVector::UpVector, Fwd)).GetSafeNormal();
        return FRotationMatrix::MakeFromXZ(ScreenRight(), Up).Rotator();
    }
    // Engine direction: 0 = north = (0,-1), 4 = east = (+1,0), 16 steps of 22.5 degrees.
    inline FVector DirToWorld(int Dir16)
    {
        const float A = FMath::DegreesToRadians(Dir16 * 22.5f);
        return FVector(FMath::Sin(A), -FMath::Cos(A), 0.f);
    }
    inline FVector Dir16ToWorld(int32 Dir16)
    {
        // inverse of WorldToDir16: dir 0 = -Y (north), 4 = +X, 8 = +Y, 12 = -X
        const float Rad = FMath::DegreesToRadians(Dir16 * 22.5f);
        return FVector(FMath::Sin(Rad), -FMath::Cos(Rad), 0.f);
    }
    inline int WorldToDir16(const FVector& V)
    {
        float Deg = FMath::RadiansToDegrees(FMath::Atan2(V.X, -V.Y));
        if (Deg < 0) Deg += 360.f;
        return FMath::RoundToInt(Deg / 22.5f) % 16;
    }
}
