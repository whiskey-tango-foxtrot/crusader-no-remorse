#include "UCProcess.h"
#include "UCMachine.h"

FUCProcess::FUCProcess(uint16 InClassId, uint16 Offset, uint32 ThisPtr, int32 ThisSize, const uint8* Args, int32 ArgSize)
{
    ClassId = 0xFFFF; Ip = 0xFFFF; Bp = 0;
    uint16 ThisSp = 0;
    if (ThisPtr != 0 && ThisSize > 0)
    {
        Stack.AddSP(-ThisSize);
        GCruMachine->DereferencePointer(ThisPtr, Stack.Access(), ThisSize);
        ThisSp = Stack.GetSP();
    }
    if (Args && ArgSize > 0) Stack.Push(Args, ArgSize);
    if (ThisSp != 0) Stack.Push4(FUCMachine::StackToPtr(Pid, ThisSp));
    Call(InClassId, Offset);
}

void FUCProcess::Run()
{
    if (Flags & PROC_SUSPENDED) return;
    GCruMachine->ExecProcess(this);
}

void FUCProcess::Call(uint16 InClassId, uint16 Offset)
{
    Stack.Push2(ClassId);
    Stack.Push2(Ip);
    Stack.Push2(Bp);
    ClassId = InClassId;
    Ip = Offset;
    Bp = static_cast<uint16>(Stack.GetSP());
}

bool FUCProcess::Ret()
{
    Stack.SetSP(Bp);
    Bp = Stack.Pop2();
    Ip = Stack.Pop2();
    ClassId = Stack.Pop2();
    return Ip == 0xFFFF && ClassId == 0xFFFF;
}

void FUCProcess::Terminate()
{
    for (auto& F : FreeList)
    {
        switch (F.Value)
        {
        case 1: GCruMachine->FreeString(F.Key); break;
        case 2: GCruMachine->FreeStringList(F.Key); break;
        case 3: GCruMachine->FreeList(F.Key); break;
        }
    }
    FreeList.Reset();
    FProcess::Terminate();
}
