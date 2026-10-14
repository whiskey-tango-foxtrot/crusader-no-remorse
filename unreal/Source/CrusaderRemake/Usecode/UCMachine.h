#pragma once
#include "CruCore.h"
#include "UCList.h"

class FUCProcess;
class FUsecodeFlex;

typedef uint32 (*FCruIntrinsic)(const uint8* Args, uint32 ArgSize);
struct FCruIntrinsicEntry { FCruIntrinsic Fn; const TCHAR* Name; };

// The usecode virtual machine. Port of ScummVM UCMachine (Crusader: No Remorse semantics).
class FUCMachine
{
public:
    enum UCSegments : uint16 { SEG_STACK = 0x0000, SEG_STACK_FIRST = 0x0001, SEG_STACK_LAST = 0x7FFE, SEG_STRING = 0x8000, SEG_LIST = 0x8001, SEG_OBJ = 0x8002, SEG_GLOBAL = 0x8003 };

    FUCMachine(FUsecodeFlex* InUsecode, const FCruIntrinsicEntry* Table, uint32 TableCount);
    ~FUCMachine();
    void Reset();
    void ExecProcess(FUCProcess* P);

    const FString& GetString(uint16 Id) const;
    FUCList* GetList(uint16 Id) const;
    uint16 AssignString(const FString& S);
    uint16 AssignList(FUCList* L);
    uint16 DuplicateString(uint16 Id);
    void FreeString(uint16 Id);
    void FreeList(uint16 Id);
    void FreeStringList(uint16 Id);

    static uint32 ListToPtr(uint16 L) { return (uint32(SEG_LIST) << 16) | L; }
    static uint32 StringToPtr(uint16 S) { return (uint32(SEG_STRING) << 16) | S; }
    static uint32 StackToPtr(uint16 Pid, uint16 Off) { return (uint32(SEG_STACK + Pid) << 16) | Off; }
    static uint32 GlobalToPtr(uint16 Off) { return (uint32(SEG_GLOBAL) << 16) | Off; }
    static uint32 ObjectToPtr(uint16 ObjID) { return (uint32(SEG_OBJ) << 16) | ObjID; }
    static uint16 PtrToObject(uint32 Ptr);
    bool AssignPointer(uint32 Ptr, const uint8* Data, uint32 Size);
    bool DereferencePointer(uint32 Ptr, uint8* Data, uint32 Size);

    uint32 GetGlobal(uint32 Pos, uint32 N) const;
    void SetGlobal(uint32 Pos, uint32 N, uint32 Val);

    FUsecodeFlex* Usecode = nullptr;
    bool bTrace = false;
    TSet<uint16> TraceClasses;
    TSet<uint32> UnimplementedReported;
    uint64 OpCount = 0;

private:
    const FCruIntrinsicEntry* Intrinsics = nullptr;
    uint32 IntrinsicCount = 0;
    TArray<uint8> Globals;
    TMap<uint16, FUCList*> ListHeap;
    TMap<uint16, FString> StringHeap;
    uint16 NextListId = 1, NextStringId = 1;
    FString EmptyString;
};

extern FUCMachine* GCruMachine;
