#pragma once
#include "CruCore.h"

// Little-endian usecode stack, grows downward. Port of ScummVM BaseUCStack.
class FUCStack
{
public:
    explicit FUCStack(uint32 Len = 0x1000) { Buf.SetNumZeroed(Len); Sp = Len; }
    uint32 GetSize() const { return Buf.Num(); }
    uint32 StackSize() const { return Buf.Num() - Sp; }
    void AddSP(int32 Off) { Sp += Off; }
    uint32 GetSP() const { return Sp; }
    void SetSP(uint32 Pos) { Sp = Pos; }
    void Push1(uint8 V) { --Sp; Buf[Sp] = V; }
    void Push2(uint16 V) { Sp -= 2; Buf[Sp] = V & 0xFF; Buf[Sp + 1] = (V >> 8) & 0xFF; }
    void Push4(uint32 V) { Sp -= 4; Buf[Sp] = V & 0xFF; Buf[Sp + 1] = (V >> 8) & 0xFF; Buf[Sp + 2] = (V >> 16) & 0xFF; Buf[Sp + 3] = (V >> 24) & 0xFF; }
    void Push0(uint32 Count) { Sp -= Count; FMemory::Memzero(Buf.GetData() + Sp, Count); }
    void Push(const uint8* In, uint32 Count) { Sp -= Count; FMemory::Memcpy(Buf.GetData() + Sp, In, Count); }
    uint16 Pop2() { uint16 V = Buf[Sp] | (Buf[Sp + 1] << 8); Sp += 2; return V; }
    uint32 Pop4() { uint32 V = Buf[Sp] | (Buf[Sp + 1] << 8) | (Buf[Sp + 2] << 16) | (uint32(Buf[Sp + 3]) << 24); Sp += 4; return V; }
    void Pop(uint8* Out, uint32 Count) { FMemory::Memcpy(Out, Buf.GetData() + Sp, Count); Sp += Count; }
    uint8 Access1(uint32 Off) const { return Buf[Off]; }
    uint16 Access2(uint32 Off) const { return Buf[Off] | (Buf[Off + 1] << 8); }
    uint32 Access4(uint32 Off) const { return Buf[Off] | (Buf[Off + 1] << 8) | (Buf[Off + 2] << 16) | (uint32(Buf[Off + 3]) << 24); }
    uint8* Access(uint32 Off) { return Buf.GetData() + Off; }
    const uint8* Access(uint32 Off) const { return Buf.GetData() + Off; }
    uint8* Access() { return Buf.GetData() + Sp; }
    void Assign1(uint32 Off, uint8 V) { Buf[Off] = V; }
    void Assign2(uint32 Off, uint16 V) { Buf[Off] = V & 0xFF; Buf[Off + 1] = (V >> 8) & 0xFF; }
    void Assign4(uint32 Off, uint32 V) { Buf[Off] = V & 0xFF; Buf[Off + 1] = (V >> 8) & 0xFF; Buf[Off + 2] = (V >> 16) & 0xFF; Buf[Off + 3] = (V >> 24) & 0xFF; }
    void Assign(uint32 Off, const uint8* In, uint32 Len) { FMemory::Memcpy(Buf.GetData() + Off, In, Len); }
private:
    TArray<uint8> Buf;
    uint32 Sp = 0;
};
