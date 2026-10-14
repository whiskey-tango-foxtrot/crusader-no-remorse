#pragma once
#include "CruCore.h"

// EUSECODE.FLX reader (Crusader layout). Entry 1 = class names (13 bytes each after a 4 byte header),
// entry classid+2 = class code. Port of ScummVM UsecodeFlex / Usecode::get_class_event.
class FUsecodeFlex
{
public:
    bool Load(const FString& Path);
    const uint8* GetClass(uint32 ClassId) const;
    uint32 GetClassSize(uint32 ClassId) const;
    FString GetClassName(uint32 ClassId) const;
    uint32 GetClassBaseOffset(uint32 ClassId) const;
    uint32 GetClassEventCount(uint32 ClassId) const;
    uint32 GetClassEvent(uint32 ClassId, uint32 EventId) const;
    uint32 NumEntries() const { return Entries.Num(); }
    const uint8* GetEntry(uint32 Index, uint32& Size) const { Size = GetSize(Index); return GetObject(Index); }   // generic flex access
private:
    TArray<uint8> Data;
    struct FEntry { uint32 Offset = 0, Size = 0; };
    TArray<FEntry> Entries;
    const uint8* GetObject(uint32 Index) const { return Entries.IsValidIndex(Index) && Entries[Index].Size ? Data.GetData() + Entries[Index].Offset : nullptr; }
    uint32 GetSize(uint32 Index) const { return Entries.IsValidIndex(Index) ? Entries[Index].Size : 0; }
};
