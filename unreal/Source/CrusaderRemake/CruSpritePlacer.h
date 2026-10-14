#pragma once
#include "CoreMinimal.h"
#include "Usecode/CruCore.h"

// Runtime twin of build_level3d_batched.py: places a sprite frame for an item as a Plane instance (transform + atlas UV).
struct FCruAtlasFrame { int32 Atlas = 0, X = 0, Y = 0, W = 0, H = 0, XOff = 0, YOff = 0; };

class FCruSpritePlacer
{
public:
    bool LoadAtlas(const FString& AtlasJsonPath);
    const FCruAtlasFrame* FindFrame(uint32 Shape, uint32 Frame) const { return Frames.Find((Shape << 16) | (Frame & 0xFFFF)); }
    // Returns false when the frame is not in the atlas.
    bool Place(const FCruItem& It, const FCruShapeInfo& Si, FTransform& OutTransform, float OutCustom[5], int32& OutAtlas) const;
    int32 NumAtlases = 0;
    int32 AtlasSize = 4096;
    int32 Scale = 1;
    static constexpr float PX = 2.8284271f;
    static constexpr float ZK = 2.8284271f / 0.8660254f;
private:
    TMap<uint32, FCruAtlasFrame> Frames;
};
