#pragma once
#include "CruCore.h"

// Process scheduler. Port of ScummVM Kernel/Process (Crusader: ticks at 60 Hz, processes run every tick).
class FKernel;

class FProcess
{
public:
    enum : uint32
    {
        PROC_ACTIVE = 0x0001, PROC_SUSPENDED = 0x0002, PROC_TERMINATED = 0x0004, PROC_TERM_DEFERRED = 0x0008,
        PROC_FAILED = 0x0010, PROC_RUNPAUSED = 0x0020, PROC_TERM_DISPOSE = 0x0040, PROC_PREVENT_SAVE = 0x0080
    };
    FProcess(ObjId InItem = 0, uint16 InType = 0);
    virtual ~FProcess() {}
    virtual void Run() = 0;
    virtual FString GetClassName() const { return TEXT("Process"); }

    bool IsActive() const { return (Flags & PROC_ACTIVE) != 0; }
    bool IsTerminated() const { return (Flags & (PROC_TERMINATED | PROC_TERM_DEFERRED)) != 0; }
    bool IsSuspended() const { return (Flags & PROC_SUSPENDED) != 0; }
    void Fail();
    virtual void Terminate();
    void TerminateDeferred() { Flags |= PROC_TERM_DEFERRED; }
    void WaitFor(ProcId Pid);
    void Suspend() { Flags |= PROC_SUSPENDED; }
    void WakeUp(uint32 InResult);

    ProcId Pid = 0xFFFF;
    uint32 Flags = 0;
    uint32 TicksPerRun = 1;
    ObjId ItemNum = 0;
    uint16 Type = 0;
    uint32 Result = 0;
    TArray<ProcId> Waiting;
};

class FKernel
{
public:
    static constexpr uint16 PROC_TYPE_ALL = 6;
    static constexpr uint16 CRU_PROC_TYPE_ALL = 0xC;

    FKernel();
    ~FKernel();
    void Reset();
    ProcId AssignPid(FProcess* Proc);
    ProcId AddProcess(FProcess* Proc);
    ProcId AddProcessExec(FProcess* Proc);
    void RunProcesses();
    FProcess* GetProcess(ProcId Pid) const;
    void SetNextProcess(FProcess* Proc);
    FProcess* GetRunningProcess() const { return RunningProcess; }
    uint32 GetNumProcesses(ObjId Obj, uint16 ProcType) const;
    FProcess* FindProcess(ObjId Obj, uint16 ProcType) const;
    void KillProcesses(ObjId Obj, uint16 ProcType, bool bFail);
    uint32 GetTickNum() const { return TickNum; }
    int32 NumProcesses() const { return Processes.Num(); }
    const TArray<FProcess*>& GetProcesses() const { return Processes; }

private:
    TArray<FProcess*> Processes;   // run order
    TMap<ProcId, FProcess*> ByPid;
    int32 CurrentIndex = -1;       // index into Processes while running, -1 otherwise
    uint32 TickNum = 0;
    FProcess* RunningProcess = nullptr;
    ProcId NextPid = 1;
};

extern FKernel* GCruKernel;
