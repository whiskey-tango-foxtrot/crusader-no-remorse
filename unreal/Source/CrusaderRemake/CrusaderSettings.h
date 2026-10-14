#pragma once
#include "CoreMinimal.h"

// Player settings (volumes, overlays), saved as Saved/SaveGames/crusader_settings.json.
struct CRUSADERREMAKE_API FCruSettings
{
    int32 MusicVolume = 6;     // 0..10
    int32 SfxVolume = 10;      // 0..10
    bool bShowKeyHelp = true;  // small key list overlay (F1)
    static FCruSettings& Get();
    static FString FilePath();
    bool Load();
    bool Save() const;
    float MusicScale() const { return MusicVolume / 10.f; }
    float SfxScale() const { return SfxVolume / 10.f; }
};
