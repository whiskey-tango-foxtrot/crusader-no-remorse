#include "Kernel.h"

FKernel* GCruKernel = nullptr;

FProcess::FProcess(ObjId InItem, uint16 InType) : ItemNum(InItem), Type(InType)
{
    if (GCruKernel) GCruKernel->AssignPid(this);
}

void FProcess::Fail() { Flags |= PROC_FAILED; Terminate(); }

void FProcess::Terminate()
{
    if (Flags & PROC_TERMINATED) return;
    for (ProcId W : Waiting)
        if (FProcess* P = GCruKernel ? GCruKernel->GetProcess(W) : nullptr) P->WakeUp(Result);
    Waiting.Reset();
    Flags |= PROC_TERMINATED;
}

void FProcess::WakeUp(uint32 InResult)
{
    Result = InResult;
    Flags &= ~PROC_SUSPENDED;
    if (GCruKernel) GCruKernel->SetNextProcess(this);
}

void FProcess::WaitFor(ProcId WaitPid)
{
    if (WaitPid)
    {
        FProcess* P = GCruKernel ? GCruKernel->GetProcess(WaitPid) : nullptr;
        if (!P) return;
        if (P->Flags & PROC_TERMINATED) return;
        P->Waiting.Add(Pid);
    }
    Flags |= PROC_SUSPENDED;
}

FKernel::FKernel() { GCruKernel = this; }
FKernel::~FKernel() { Reset(); if (GCruKernel == this) GCruKernel = nullptr; }

void FKernel::Reset()
{
    for (FProcess* P : Processes) if (P != RunningProcess) delete P;
    Processes.Reset(); ByPid.Reset(); CurrentIndex = -1; RunningProcess = nullptr; NextPid = 1;
}

ProcId FKernel::AssignPid(FProcess* Proc)
{
    // ids 1..32766, reuse freed ones lazily
    for (int32 Tries = 0; Tries < 32766; ++Tries)
    {
        ProcId Id = NextPid; NextPid = NextPid >= 32766 ? 1 : NextPid + 1;
        if (!ByPid.Contains(Id)) { Proc->Pid = Id; ByPid.Add(Id, Proc); return Id; }
    }
    Proc->Pid = 0xFFFF; return 0xFFFF;
}

ProcId FKernel::AddProcess(FProcess* Proc)
{
    check(Proc->Pid != 0 && Proc->Pid != 0xFFFF);
    Proc->Flags |= FProcess::PROC_TERM_DISPOSE;
    SetNextProcess(Proc);
    return Proc->Pid;
}

ProcId FKernel::AddProcessExec(FProcess* Proc)
{
    check(Proc->Pid != 0 && Proc->Pid != 0xFFFF);
    Proc->Flags |= FProcess::PROC_TERM_DISPOSE | FProcess::PROC_ACTIVE;
    Processes.Add(Proc);
    FProcess* Old = RunningProcess;
    RunningProcess = Proc;
    Proc->Run();
    RunningProcess = Old;
    return Proc->Pid;
}

void FKernel::SetNextProcess(FProcess* Proc)
{
    if (CurrentIndex >= 0 && CurrentIndex < Processes.Num() && Processes[CurrentIndex] == Proc) return;
    if (Proc->Flags & FProcess::PROC_ACTIVE)
    {
        const int32 Idx = Processes.IndexOfByKey(Proc);
        if (Idx != INDEX_NONE)
        {
            Processes.RemoveAt(Idx);
            if (CurrentIndex >= 0 && Idx < CurrentIndex) --CurrentIndex;
        }
    }
    else Proc->Flags |= FProcess::PROC_ACTIVE;

    if (CurrentIndex < 0) Processes.Insert(Proc, 0);
    else Processes.Insert(Proc, FMath::Min(CurrentIndex + 1, Processes.Num()));
}

FProcess* FKernel::GetProcess(ProcId Pid) const
{
    FProcess* const* P = ByPid.Find(Pid);
    return P ? *P : nullptr;
}

void FKernel::RunProcesses()
{
    ++TickNum;
    if (Processes.Num() == 0) return;
    int32 NumRun = 0;
    TArray<FProcess*> Deferred;   // Crusader: term-deferred processes move to the end of the list
    CurrentIndex = 0;
    while (CurrentIndex < Processes.Num())
    {
        FProcess* P = Processes[CurrentIndex];
        if ((P->Flags & (FProcess::PROC_TERMINATED | FProcess::PROC_TERM_DEFERRED)) == FProcess::PROC_TERM_DEFERRED)
            P->Terminate();
        if (!(P->IsTerminated() || P->IsSuspended()) && (TickNum % FMath::Max(1u, P->TicksPerRun) == 0))
        {
            RunningProcess = P;
            P->Run();
            RunningProcess = nullptr;
            ++NumRun;
            if (NumRun > 8192 && !P->IsTerminated())
            {
                UE_LOG(LogCru, Warning, TEXT("kernel: stuck in process loop, failing pid %d"), P->Pid);
                P->Fail();
            }
            if (CurrentIndex >= Processes.Num() || Processes[CurrentIndex] != P)
            {
                // list changed under us (process moved itself); find it again
                const int32 Idx = Processes.IndexOfByKey(P);
                if (Idx == INDEX_NONE) { continue; }
                CurrentIndex = Idx;
            }
        }
        if (P->Flags & FProcess::PROC_TERMINATED)
        {
            Processes.RemoveAt(CurrentIndex);
            ByPid.Remove(P->Pid);
            if (P->Flags & FProcess::PROC_TERM_DISPOSE) delete P;
        }
        else if (P->Flags & FProcess::PROC_TERM_DEFERRED)
        {
            Processes.RemoveAt(CurrentIndex);
            Deferred.Add(P);
        }
        else ++CurrentIndex;
    }
    CurrentIndex = -1;
    Processes.Append(Deferred);
}

uint32 FKernel::GetNumProcesses(ObjId Obj, uint16 ProcType) const
{
    uint32 Count = 0;
    for (const FProcess* P : Processes)
    {
        if (P->IsTerminated()) continue;
        if ((Obj == 0 || Obj == P->ItemNum) && (ProcType == PROC_TYPE_ALL || ProcType == P->Type)) ++Count;
    }
    return Count;
}

FProcess* FKernel::FindProcess(ObjId Obj, uint16 ProcType) const
{
    for (FProcess* P : Processes)
    {
        if (P->IsTerminated()) continue;
        if ((Obj == 0 || Obj == P->ItemNum) && (ProcType == PROC_TYPE_ALL || ProcType == P->Type)) return P;
    }
    return nullptr;
}

void FKernel::KillProcesses(ObjId Obj, uint16 ProcType, bool bFail)
{
    for (FProcess* P : Processes)
    {
        if (P->ItemNum != 0 && (Obj == 0 || Obj == P->ItemNum) && (ProcType == PROC_TYPE_ALL || ProcType == P->Type) &&
            !(P->Flags & FProcess::PROC_TERMINATED) && !(P->Flags & FProcess::PROC_TERM_DEFERRED))
        {
            if (bFail) P->Fail(); else P->Terminate();
        }
    }
}
