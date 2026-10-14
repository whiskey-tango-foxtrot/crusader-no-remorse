#pragma once
#include "CruCore.h"

// Usecode list of fixed-size elements. Port of ScummVM UCList (string lists handled by the machine).
class FUCList
{
public:
    explicit FUCList(uint32 InElementSize, uint32 Capacity = 0) : ElementSize(InElementSize) { if (Capacity) Elements.Reserve(ElementSize * Capacity); }
    const uint8* operator[](uint32 Index) const { return &Elements[Index * ElementSize]; }
    uint16 GetUInt16(uint32 Index) const { return Elements[Index * ElementSize] | (Elements[Index * ElementSize + 1] << 8); }
    void Append(const uint8* E) { Elements.Append(E, ElementSize); ++Size; }
    void AppendUInt16(uint16 V) { uint8 B[2] = { uint8(V), uint8(V >> 8) }; Append(B); }
    void Remove(const uint8* E)
    {
        for (uint32 i = 0; i < Size; ++i)
            if (FMemory::Memcmp(&Elements[i * ElementSize], E, ElementSize) == 0)
            {
                Elements.RemoveAt(i * ElementSize, ElementSize); --Size; --i;
            }
    }
    bool InList(const uint8* E) const
    {
        for (uint32 i = 0; i < Size; ++i)
            if (FMemory::Memcmp(&Elements[i * ElementSize], E, ElementSize) == 0) return true;
        return false;
    }
    void AppendList(const FUCList& L) { for (uint32 i = 0; i < L.Size; ++i) Append(L[i]); }
    void UnionList(const FUCList& L) { for (uint32 i = 0; i < L.Size; ++i) if (!InList(L[i])) Append(L[i]); }
    void SubtractList(const FUCList& L) { for (uint32 i = 0; i < L.Size; ++i) Remove(L[i]); }
    void Free() { Elements.Reset(); Size = 0; }
    uint32 GetSize() const { return Size; }
    uint32 GetElementSize() const { return ElementSize; }
    void Assign(uint32 Index, const uint8* E) { if (Index < Size) FMemory::Memcpy(&Elements[Index * ElementSize], E, ElementSize); }
    void CopyList(const FUCList& L) { Free(); AppendList(L); }
    uint16 GetStringIndex(uint32 Index) const { return Elements[Index * 2] | (Elements[Index * 2 + 1] << 8); }
    TArray<uint8> Elements;
    uint32 ElementSize = 2;
    uint32 Size = 0;
};
