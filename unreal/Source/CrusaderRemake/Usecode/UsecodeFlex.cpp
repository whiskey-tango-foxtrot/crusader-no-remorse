#include "UsecodeFlex.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY(LogCru);

static uint32 RdU32(const uint8* P) { return P[0] | (P[1] << 8) | (P[2] << 16) | (uint32(P[3]) << 24); }

bool FUsecodeFlex::Load(const FString& Path)
{
    if (!FFileHelper::LoadFileToArray(Data, *Path) || Data.Num() < 0x80 || Data[0x51] != 0x1A)
    {
        UE_LOG(LogCru, Error, TEXT("usecode: cannot read flex %s"), *Path);
        return false;
    }
    const uint32 Count = RdU32(Data.GetData() + 0x54);
    Entries.SetNum(Count);
    for (uint32 i = 0; i < Count; ++i)
    {
        const uint8* P = Data.GetData() + 0x80 + 8 * i;
        if (0x80 + 8 * i + 8 > (uint32)Data.Num()) break;
        Entries[i].Offset = RdU32(P); Entries[i].Size = RdU32(P + 4);
        if (Entries[i].Offset + Entries[i].Size > (uint32)Data.Num()) { Entries[i].Size = 0; }
    }
    UE_LOG(LogCru, Display, TEXT("usecode: %s loaded, %d entries"), *Path, Count);
    return true;
}

const uint8* FUsecodeFlex::GetClass(uint32 ClassId) const { return GetObject(ClassId + 2); }
uint32 FUsecodeFlex::GetClassSize(uint32 ClassId) const { return GetSize(ClassId + 2); }

FString FUsecodeFlex::GetClassName(uint32 ClassId) const
{
    if (GetSize(ClassId + 2) == 0) return FString();
    const uint8* Names = GetObject(1);
    if (!Names || 4 + 13 * ClassId + 13 > GetSize(1)) return FString();
    const char* P = reinterpret_cast<const char*>(Names + 4 + 13 * ClassId);
    char Buf[14]; FMemory::Memcpy(Buf, P, 13); Buf[13] = 0;
    return FString(ANSI_TO_TCHAR(Buf));
}

uint32 FUsecodeFlex::GetClassBaseOffset(uint32 ClassId) const
{
    const uint8* Obj = GetClass(ClassId);
    if (!Obj || GetClassSize(ClassId) < 12) return 0;
    return RdU32(Obj + 8) - 1;
}

uint32 FUsecodeFlex::GetClassEventCount(uint32 ClassId) const
{
    if (GetClassSize(ClassId) == 0) return 0;
    return (GetClassBaseOffset(ClassId) + 19) / 6;
}

uint32 FUsecodeFlex::GetClassEvent(uint32 ClassId, uint32 EventId) const
{
    if (GetClassSize(ClassId) == 0) return 0;
    if (EventId >= GetClassEventCount(ClassId)) return 0;
    const uint8* D = GetClass(ClassId);
    const uint32 Off = 20 + EventId * 6 + 2;
    if (Off + 4 > GetClassSize(ClassId)) return 0;
    return RdU32(D + Off);
}
