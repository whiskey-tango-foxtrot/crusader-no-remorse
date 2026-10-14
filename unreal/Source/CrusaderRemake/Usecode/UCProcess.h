#pragma once
#include "Kernel.h"
#include "UCStack.h"

class FUsecodeFlex;

// A usecode process. Port of ScummVM UCProcess.
class FUCProcess : public FProcess
{
public:
    FUCProcess(uint16 InClassId, uint16 Offset, uint32 ThisPtr = 0, int32 ThisSize = 0, const uint8* Args = nullptr, int32 ArgSize = 0);
    virtual void Run() override;
    virtual void Terminate() override;
    virtual FString GetClassName() const override { return TEXT("UCProcess"); }
    void FreeOnTerminate(uint16 Index, int32 TypeNum) { FreeList.Add(TPair<uint16, int32>(Index, TypeNum)); }
    void Call(uint16 InClassId, uint16 Offset);
    bool Ret();

    uint16 Bp = 0;
    uint16 ClassId = 0xFFFF;
    uint16 Ip = 0xFFFF;
    uint32 Temp32 = 0;
    FUCStack Stack;
    TArray<TPair<uint16, int32>> FreeList;
};
