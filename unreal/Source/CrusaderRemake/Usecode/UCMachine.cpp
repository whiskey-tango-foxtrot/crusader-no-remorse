#include "UCMachine.h"
#include "UCProcess.h"
#include "UsecodeFlex.h"
#include "CruWorld.h"

FUCMachine* GCruMachine = nullptr;

namespace
{
    struct FCodeStream
    {
        const uint8* Data; uint32 Size; uint32 Pos;
        uint8 ReadByte() { return Pos < Size ? Data[Pos++] : (Pos++, 0); }
        int8 ReadSByte() { return (int8)ReadByte(); }
        uint16 ReadU16() { uint16 V = ReadByte(); V |= ReadByte() << 8; return V; }
        uint32 ReadU32() { uint32 V = ReadU16(); V |= uint32(ReadU16()) << 16; return V; }
        void Read(void* Out, uint32 N) { for (uint32 i = 0; i < N; ++i) ((uint8*)Out)[i] = ReadByte(); }
        void Seek(uint32 P) { Pos = P; }
    };
}

FUCMachine::FUCMachine(FUsecodeFlex* InUsecode, const FCruIntrinsicEntry* Table, uint32 TableCount)
    : Usecode(InUsecode), Intrinsics(Table), IntrinsicCount(TableCount)
{
    GCruMachine = this;
    Reset();
}

FUCMachine::~FUCMachine()
{
    for (auto& L : ListHeap) delete L.Value;
    ListHeap.Reset();
    if (GCruMachine == this) GCruMachine = nullptr;
}

void FUCMachine::Reset()
{
    Globals.SetNumZeroed(0x1000);
    SetGlobal(0x3C, 2, 1);   // avatar object number (Remorse)
    for (auto& L : ListHeap) delete L.Value;
    ListHeap.Reset(); StringHeap.Reset();
}

uint32 FUCMachine::GetGlobal(uint32 Pos, uint32 N) const
{
    if (N == 0 || Pos + N > (uint32)Globals.Num()) return 0;
    if (N == 1) return Globals[Pos];
    return (Globals[Pos] << 8) | Globals[Pos + 1];
}

void FUCMachine::SetGlobal(uint32 Pos, uint32 N, uint32 Val)
{
    if (N == 0 || Pos + N > (uint32)Globals.Num()) return;
    if (N == 1) Globals[Pos] = (uint8)Val;
    else { Globals[Pos] = (uint8)((Val & 0xFF00) >> 8); Globals[Pos + 1] = (uint8)(Val & 0xFF); }
}

const FString& FUCMachine::GetString(uint16 Id) const { const FString* S = StringHeap.Find(Id); return S ? *S : EmptyString; }
FUCList* FUCMachine::GetList(uint16 Id) const { FUCList* const* L = ListHeap.Find(Id); return L ? *L : nullptr; }

uint16 FUCMachine::AssignString(const FString& S)
{
    for (int32 T = 0; T < 65534; ++T)
    {
        uint16 Id = NextStringId; NextStringId = NextStringId >= 65534 ? 1 : NextStringId + 1;
        if (!StringHeap.Contains(Id)) { StringHeap.Add(Id, S); return Id; }
    }
    return 0;
}

uint16 FUCMachine::AssignList(FUCList* L)
{
    for (int32 T = 0; T < 65534; ++T)
    {
        uint16 Id = NextListId; NextListId = NextListId >= 65534 ? 1 : NextListId + 1;
        if (!ListHeap.Contains(Id)) { ListHeap.Add(Id, L); return Id; }
    }
    delete L; return 0;
}

uint16 FUCMachine::DuplicateString(uint16 Id) { return AssignString(GetString(Id)); }
void FUCMachine::FreeString(uint16 Id) { StringHeap.Remove(Id); }
void FUCMachine::FreeList(uint16 Id) { FUCList* L = nullptr; if (ListHeap.RemoveAndCopyValue(Id, L)) delete L; }
void FUCMachine::FreeStringList(uint16 Id)
{
    FUCList* L = nullptr;
    if (ListHeap.RemoveAndCopyValue(Id, L))
    {
        for (uint32 i = 0; i < L->GetSize(); ++i) FreeString(L->GetStringIndex(i));
        delete L;
    }
}

uint16 FUCMachine::PtrToObject(uint32 Ptr)
{
    const uint16 Segment = Ptr >> 16, Offset = Ptr & 0xFFFF;
    if (Segment >= SEG_STACK_FIRST && Segment <= SEG_STACK_LAST)
    {
        FUCProcess* Proc = (FUCProcess*)(GCruKernel ? GCruKernel->GetProcess(Segment) : nullptr);
        if (!Proc || Proc->Stack.GetSize() < (uint32)Offset + 2) return 0;
        return Proc->Stack.Access2(Offset);
    }
    if (Segment == SEG_OBJ || Segment == SEG_STRING) return Offset;
    if (Segment == SEG_GLOBAL) return GCruMachine->GetGlobal(Offset, 2);
    return 0;
}

bool FUCMachine::AssignPointer(uint32 Ptr, const uint8* Data, uint32 Size)
{
    const uint16 Segment = Ptr >> 16, Offset = Ptr & 0xFFFF;
    if (Segment >= SEG_STACK_FIRST && Segment <= SEG_STACK_LAST)
    {
        FUCProcess* Proc = (FUCProcess*)(GCruKernel ? GCruKernel->GetProcess(Segment) : nullptr);
        if (!Proc) { UE_LOG(LogCru, Warning, TEXT("usecode: assign to stack of missing process %d"), Segment); return false; }
        Proc->Stack.Assign(Offset, Data, Size);
        return true;
    }
    if (Segment == SEG_GLOBAL)
    {
        if (Size == 1) SetGlobal(Offset, 1, Data[0]);
        else if (Size == 2) SetGlobal(Offset, 2, (Data[1] << 8) | Data[0]);
        return true;
    }
    UE_LOG(LogCru, Warning, TEXT("usecode: assign to segment %04X"), Segment);
    return false;
}

bool FUCMachine::DereferencePointer(uint32 Ptr, uint8* Data, uint32 Size)
{
    const uint16 Segment = Ptr >> 16, Offset = Ptr & 0xFFFF;
    if (Segment >= SEG_STACK_FIRST && Segment <= SEG_STACK_LAST)
    {
        FUCProcess* Proc = (FUCProcess*)(GCruKernel ? GCruKernel->GetProcess(Segment) : nullptr);
        if (!Proc) { UE_LOG(LogCru, Warning, TEXT("usecode: deref stack of missing process %d"), Segment); return false; }
        FMemory::Memcpy(Data, Proc->Stack.Access(Offset), Size);
        return true;
    }
    if (Segment == SEG_OBJ)
    {
        if (Size != 2) return false;
        Data[0] = (uint8)Offset; Data[1] = (uint8)(Offset >> 8);
        return true;
    }
    if (Segment == SEG_GLOBAL)
    {
        if (Size == 1) Data[0] = (uint8)GetGlobal(Offset, 1);
        else if (Size == 2) { uint16 V = GetGlobal(Offset, 2); Data[0] = (uint8)V; Data[1] = (uint8)(V >> 8); }
        return true;
    }
    UE_LOG(LogCru, Warning, TEXT("usecode: deref segment %04X"), Segment);
    return false;
}

#define TRACE(fmt, ...) do { if (Trace) UE_LOG(LogCru, Display, TEXT("  %04X:%04X %02X " fmt), P->ClassId, OpIp, Opcode, ##__VA_ARGS__); } while (0)

void FUCMachine::ExecProcess(FUCProcess* P)
{
    check(P);
    uint32 Base = Usecode->GetClassBaseOffset(P->ClassId);
    const uint8* ClassData = Usecode->GetClass(P->ClassId);
    uint32 ClassSize = Usecode->GetClassSize(P->ClassId);
    if (!ClassData || ClassSize <= Base) { UE_LOG(LogCru, Warning, TEXT("usecode: empty class %d, killing pid %d"), P->ClassId, P->Pid); P->TerminateDeferred(); return; }
    FCodeStream Cs{ ClassData + Base, ClassSize - Base, P->Ip };
    const bool Trace = bTrace || TraceClasses.Contains(P->ClassId);
    if (Trace) UE_LOG(LogCru, Display, TEXT("tick %u pid %u item %u type %u class %u (%s) ip %04X"), GCruKernel->GetTickNum(), P->Pid, P->ItemNum, P->Type, P->ClassId, *Usecode->GetClassName(P->ClassId), P->Ip);

    bool Cede = false, Error = false, GoUntilCede = false;
    auto ReloadStream = [&]()
    {
        Base = Usecode->GetClassBaseOffset(P->ClassId);
        ClassData = Usecode->GetClass(P->ClassId); ClassSize = Usecode->GetClassSize(P->ClassId);
        Cs = FCodeStream{ ClassData ? ClassData + Base : nullptr, ClassData && ClassSize > Base ? ClassSize - Base : 0, P->Ip };
        if (!ClassData) { Error = true; }
    };

    while (!Cede && !Error && !P->IsTerminated())
    {
        if (Cs.Pos >= Cs.Size) { UE_LOG(LogCru, Warning, TEXT("usecode: ran off end of class %d"), P->ClassId); Error = true; break; }
        const uint32 OpIp = Cs.Pos;
        const uint8 Opcode = Cs.ReadByte();
        ++OpCount;
        int8 si8a; uint8 ui8a; uint16 ui16a, ui16b; uint32 ui32a, ui32b; int16 si16a, si16b; int32 si32a, si32b;
        FUCStack& S = P->Stack;

        switch (Opcode)
        {
        case 0x00: si8a = Cs.ReadSByte(); ui16a = S.Pop2(); S.Assign1(P->Bp + si8a, (uint8)ui16a); TRACE("pop byte [BP%+d] = %02X", si8a, ui16a); break;
        case 0x01: si8a = Cs.ReadSByte(); ui16a = S.Pop2(); S.Assign2(P->Bp + si8a, ui16a); TRACE("pop [BP%+d] = %04X", si8a, ui16a); break;
        case 0x02: si8a = Cs.ReadSByte(); ui32a = S.Pop4(); S.Assign4(P->Bp + si8a, ui32a); TRACE("pop dword [BP%+d] = %08X", si8a, ui32a); break;
        case 0x03: { si8a = Cs.ReadSByte(); uint8 Sz = Cs.ReadByte(); uint8 Buf[256]; S.Pop(Buf, Sz); S.Assign(P->Bp + si8a, Buf, Sz); TRACE("pop huge"); break; }
        case 0x08: P->Result = S.Pop4(); TRACE("pop result = %08X", P->Result); break;
        case 0x09:
        {
            si8a = Cs.ReadSByte(); ui32a = Cs.ReadByte(); int8 si8b = Cs.ReadSByte();
            ui16a = S.Pop2() - 1; ui16b = S.Access2(P->Bp + si8a);
            FUCList* L = GetList(ui16b);
            if (!L) { UE_LOG(LogCru, Warning, TEXT("usecode: assign element to invalid list %u"), ui16b); Error = true; break; }
            if (si8b) { L->Assign(ui16a, S.Access()); S.Pop2(); }
            else { L->Assign(ui16a, S.Access()); S.AddSP(ui32a); }
            TRACE("assign element");
            break;
        }
        case 0x0A: ui16a = (uint16)(int16)Cs.ReadSByte(); S.Push2(ui16a); TRACE("push sbyte %04X", ui16a); break;
        case 0x0B: ui16a = Cs.ReadU16(); S.Push2(ui16a); TRACE("push %04X", ui16a); break;
        case 0x0C: ui32a = Cs.ReadU32(); S.Push4(ui32a); TRACE("push dword %08X", ui32a); break;
        case 0x0D:
        {
            ui16a = Cs.ReadU16();
            TArray<char> Str; Str.SetNumZeroed(ui16a + 1); Cs.Read(Str.GetData(), ui16a);
            ui16b = Cs.ReadByte();
            if (ui16b != 0) { UE_LOG(LogCru, Warning, TEXT("usecode: missing string terminator")); Error = true; }
            S.Push2(AssignString(FString(ANSI_TO_TCHAR(Str.GetData()))));
            TRACE("push string \"%s\"", ANSI_TO_TCHAR(Str.GetData()));
            break;
        }
        case 0x0E:
        {
            ui16a = Cs.ReadByte(); ui16b = Cs.ReadByte();
            FUCList* L = new FUCList(ui16a, ui16b);
            S.AddSP(ui16a * (ui16b - 1));
            for (uint32 i = 0; i < ui16b; ++i) { L->Append(S.Access()); S.AddSP(-(int32)ui16a); }
            S.AddSP(ui16a * (ui16b + 1));
            S.Push2(AssignList(L));
            TRACE("create list %02X (%02X)", ui16b, ui16a);
            break;
        }
        case 0x0F:
        {
            const uint16 ArgBytes = Cs.ReadByte(); const uint16 Func = Cs.ReadU16();
            TRACE("calli %04X (%d bytes) %s", Func, ArgBytes, Func < IntrinsicCount ? Intrinsics[Func].Name : TEXT("?"));
            if (Func >= IntrinsicCount || Intrinsics[Func].Fn == nullptr)
            {
                P->Temp32 = 0;
                if (!UnimplementedReported.Contains(Func))
                {
                    UnimplementedReported.Add(Func);
                    UE_LOG(LogCru, Warning, TEXT("usecode: unimplemented intrinsic %03X '%s' (class %s)"), Func, Func < IntrinsicCount ? Intrinsics[Func].Name : TEXT("?"), *Usecode->GetClassName(P->ClassId));
                }
            }
            else
            {
                TArray<uint8> ArgBuf; ArgBuf.SetNumZeroed(ArgBytes);
                S.Pop(ArgBuf.GetData(), ArgBytes); S.AddSP(-(int32)ArgBytes);
                P->Temp32 = Intrinsics[Func].Fn(ArgBuf.GetData(), ArgBytes);
            }
            break;
        }
        case 0x11:
        {
            uint16 NewClass = Cs.ReadU16(); uint16 NewOffset = Cs.ReadU16();
            NewOffset = Usecode->GetClassEvent(NewClass, NewOffset);
            TRACE("call %04X:%04X", NewClass, NewOffset);
            P->Ip = (uint16)Cs.Pos;
            P->Call(NewClass, NewOffset);
            ReloadStream();
            break;
        }
        case 0x12: P->Temp32 = S.Pop2(); TRACE("pop temp = %04X", P->Temp32); break;
        case 0x13: P->Temp32 = S.Pop4(); break;
        case 0x14: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2((uint16)(si16a + si16b)); TRACE("add"); break;
        case 0x15: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push4((uint32)(si32a + si32b)); break;
        case 0x16:
            ui16a = S.Pop2(); ui16b = S.Pop2();
            if (ui16b == 0) { Error = true; break; }
            StringHeap.FindOrAdd(ui16b) += GetString(ui16a); FreeString(ui16a); S.Push2(ui16b);
            break;
        case 0x17:
        {
            ui16a = S.Pop2(); ui16b = S.Pop2();
            FUCList* A = GetList(ui16a); FUCList* B = GetList(ui16b);
            if (A && B)
            {
                if (A->GetElementSize() != B->GetElementSize()) Error = true; else B->AppendList(*A);
                if (ui16a != ui16b) FreeList(ui16a);
                S.Push2(ui16b);
            }
            else S.Push2(A ? ui16a : (B ? ui16b : 0));
            TRACE("append");
            break;
        }
        case 0x19:
        {
            ui32a = Cs.ReadByte(); ui16a = S.Pop2(); ui16b = S.Pop2();
            FUCList* Src = GetList(ui16a); FUCList* Dst = GetList(ui16b);
            if (!Src || !Dst) Error = true;
            else
            {
                for (uint32 i = 0; i < Src->GetSize(); ++i)
                {
                    const FString& Str = GetString(Src->GetStringIndex(i));
                    bool bIn = false;
                    for (uint32 j = 0; j < Dst->GetSize(); ++j) if (GetString(Dst->GetStringIndex(j)) == Str) { bIn = true; break; }
                    if (!bIn) Dst->Append((*Src)[i]); else FreeString(Src->GetStringIndex(i));
                }
                Src->Free(); FreeList(ui16a);
            }
            S.Push2(ui16b);
            break;
        }
        case 0x1A:
        {
            Cs.ReadByte(); ui16a = S.Pop2(); ui16b = S.Pop2();
            FUCList* Src = GetList(ui16a); FUCList* Dst = GetList(ui16b);
            if (!Src || !Dst) Error = true;
            else
            {
                for (uint32 i = 0; i < Src->GetSize(); ++i)
                {
                    const FString Str = GetString(Src->GetStringIndex(i));
                    for (uint32 j = 0; j < Dst->GetSize(); ++j)
                        if (GetString(Dst->GetStringIndex(j)) == Str) { FreeString(Dst->GetStringIndex(j)); Dst->Elements.RemoveAt(j * 2, 2); --Dst->Size; --j; }
                }
                FreeStringList(ui16a);
            }
            S.Push2(ui16b);
            break;
        }
        case 0x1B:
        {
            Cs.ReadByte(); ui16a = S.Pop2(); ui16b = S.Pop2();
            FUCList* Src = GetList(ui16a); FUCList* Dst = GetList(ui16b);
            if (!Src || !Dst) Error = true; else { Dst->SubtractList(*Src); FreeList(ui16a); }
            S.Push2(ui16b);
            TRACE("remove list");
            break;
        }
        case 0x1C: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2((uint16)(si16b - si16a)); TRACE("sub"); break;
        case 0x1D: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push4((uint32)(si32b - si32a)); break;
        case 0x1E: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2((uint16)(si16a * si16b)); TRACE("mul"); break;
        case 0x1F: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push4((uint32)(si32a * si32b)); break;
        case 0x20: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16a ? (uint16)(si16b / si16a) : 0); TRACE("div"); break;
        case 0x21: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push4(si32a ? (uint32)(si32b / si32a) : 0); break;
        case 0x22: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16a ? (uint16)(si16b % si16a) : 0); break;
        case 0x23: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push4(si32a ? (uint32)(si32b % si32a) : 0); break;
        case 0x24: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16a == si16b ? 1 : 0); TRACE("cmp %d %d", si16b, si16a); break;
        case 0x25: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push2(si32a == si32b ? 1 : 0); break;
        case 0x26: ui16a = S.Pop2(); ui16b = S.Pop2(); S.Push2(GetString(ui16b) == GetString(ui16a) ? 1 : 0); FreeString(ui16a); FreeString(ui16b); break;
        case 0x28: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16b < si16a ? 1 : 0); TRACE("lt"); break;
        case 0x29: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push2(si32b < si32a ? 1 : 0); break;
        case 0x2A: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16b <= si16a ? 1 : 0); TRACE("le"); break;
        case 0x2B: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push2(si32b <= si32a ? 1 : 0); break;
        case 0x2C: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16b > si16a ? 1 : 0); TRACE("gt"); break;
        case 0x2D: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push2(si32b > si32a ? 1 : 0); break;
        case 0x2E: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16b >= si16a ? 1 : 0); TRACE("ge"); break;
        case 0x2F: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push2(si32b >= si32a ? 1 : 0); break;
        case 0x30: ui16a = S.Pop2(); S.Push2(!ui16a ? 1 : 0); TRACE("not"); break;
        case 0x31: ui32a = S.Pop4(); S.Push2(!ui32a ? 1 : 0); break;
        case 0x32: ui16a = S.Pop2(); ui16b = S.Pop2(); S.Push2((ui16a && ui16b) ? 1 : 0); TRACE("and"); break;
        case 0x33: ui32a = S.Pop4(); ui32b = S.Pop4(); S.Push4((ui32a && ui32b) ? 1 : 0); break;
        case 0x34: ui16a = S.Pop2(); ui16b = S.Pop2(); S.Push2((ui16a || ui16b) ? 1 : 0); TRACE("or"); break;
        case 0x35: ui32a = S.Pop4(); ui32b = S.Pop4(); S.Push4((ui32a || ui32b) ? 1 : 0); break;
        case 0x36: si16a = (int16)S.Pop2(); si16b = (int16)S.Pop2(); S.Push2(si16a != si16b ? 1 : 0); TRACE("ne"); break;
        case 0x37: si32a = (int32)S.Pop4(); si32b = (int32)S.Pop4(); S.Push2(si32a != si32b ? 1 : 0); break;
        case 0x38:
        {
            ui16a = Cs.ReadByte(); ui32a = Cs.ReadByte(); ui16b = S.Pop2();
            FUCList* L = GetList(ui16b);
            if (!L) { Error = true; }
            else if (ui32a)
            {
                const FString Str = GetString(S.Pop2()); bool bFound = false;
                for (uint32 i = 0; i < L->GetSize(); ++i) if (GetString(L->GetStringIndex(i)) == Str) { bFound = true; break; }
                S.Push2(bFound ? 1 : 0); FreeStringList(ui16b);
            }
            else { bool bFound = L->InList(S.Access()); S.AddSP(ui16a); S.Push2(bFound ? 1 : 0); FreeList(ui16b); }
            TRACE("in list");
            break;
        }
        case 0x39: ui16a = S.Pop2(); ui16b = S.Pop2(); S.Push2(ui16a & ui16b); TRACE("bit_and"); break;
        case 0x3A: ui16a = S.Pop2(); ui16b = S.Pop2(); S.Push2(ui16a | ui16b); TRACE("bit_or"); break;
        case 0x3B: ui16a = S.Pop2(); S.Push2(~ui16a); TRACE("bit_not"); break;
        case 0x3C: ui16b = S.Pop2(); si16a = (int16)S.Pop2(); S.Push2((uint16)(si16a << ui16b)); break;
        case 0x3D: ui16b = S.Pop2(); si16a = (int16)S.Pop2(); S.Push2((uint16)(si16a >> ui16b)); break;
        case 0x3E: si8a = Cs.ReadSByte(); ui16a = (uint16)(int16)(int8)S.Access1(P->Bp + si8a); S.Push2(ui16a); TRACE("push byte [BP%+d] = %02X", si8a, ui16a); break;
        case 0x3F: si8a = Cs.ReadSByte(); ui16a = S.Access2(P->Bp + si8a); S.Push2(ui16a); TRACE("push [BP%+d] = %04X", si8a, ui16a); break;
        case 0x40: si8a = Cs.ReadSByte(); ui32a = S.Access4(P->Bp + si8a); S.Push4(ui32a); TRACE("push dword [BP%+d] = %08X", si8a, ui32a); break;
        case 0x41: si8a = Cs.ReadSByte(); ui16a = S.Access2(P->Bp + si8a); S.Push2(DuplicateString(ui16a)); break;
        case 0x42:
        {
            si8a = Cs.ReadSByte(); ui16a = Cs.ReadByte(); ui16b = S.Access2(P->Bp + si8a);
            FUCList* L = new FUCList(ui16a);
            if (FUCList* Src = GetList(ui16b)) L->CopyList(*Src);
            S.Push2(AssignList(L));
            TRACE("push list [BP%+d]", si8a);
            break;
        }
        case 0x43:
        {
            si8a = Cs.ReadSByte(); ui16b = S.Access2(P->Bp + si8a);
            FUCList* L = new FUCList(2);
            if (FUCList* Src = GetList(ui16b)) for (uint32 i = 0; i < Src->GetSize(); ++i) L->AppendUInt16(DuplicateString(Src->GetStringIndex(i)));
            S.Push2(AssignList(L));
            break;
        }
        case 0x44:
        {
            ui32a = Cs.ReadByte(); ui32b = Cs.ReadByte();
            ui16a = S.Pop2() - 1; ui16b = S.Pop2();
            FUCList* L = GetList(ui16b);
            if (!L) S.Push0(ui32a);
            else if (ui32b) S.Push2(DuplicateString(L->GetStringIndex(ui16a)));
            else if (ui16a < L->GetSize()) S.Push((*L)[ui16a], ui32a);
            else { UE_LOG(LogCru, Warning, TEXT("usecode: push element %d from list of %d"), ui16a, L->GetSize()); S.Push0(ui32a); }
            TRACE("push element %d", ui16a);
            break;
        }
        case 0x45: si8a = Cs.ReadSByte(); ui16b = Cs.ReadByte(); S.Push(S.Access(P->Bp + si8a), ui16b); break;
        case 0x4B: si8a = Cs.ReadSByte(); S.Push4(StackToPtr(P->Pid, P->Bp + si8a)); TRACE("push addr [BP%+d]", si8a); break;
        case 0x4C:
            ui16a = Cs.ReadByte(); ui32a = S.Pop4(); S.AddSP(-(int32)ui16a);
            if (!DereferencePointer(ui32a, S.Access(), ui16a)) Error = true;
            TRACE("push indirect %d bytes", ui16a);
            break;
        case 0x4D:
            ui16a = Cs.ReadByte(); ui32a = S.Pop4();
            if (AssignPointer(ui32a, S.Access(), ui16a)) S.AddSP(ui16a); else Error = true;
            TRACE("pop indirect %d bytes", ui16a);
            break;
        case 0x4E: ui16a = Cs.ReadU16(); ui16b = Cs.ReadByte(); ui32a = GetGlobal(ui16a, ui16b); S.Push2((uint16)ui32a); TRACE("push global [%04X %d] = %X", ui16a, ui16b, ui32a); break;
        case 0x4F: ui16a = Cs.ReadU16(); ui16b = Cs.ReadByte(); ui32a = S.Pop2(); SetGlobal(ui16a, ui16b, ui32a); TRACE("pop global [%04X %d] = %X", ui16a, ui16b, ui32a); break;
        case 0x50:
            if (P->Ret()) { TRACE("ret from process"); P->TerminateDeferred(); }
            else { TRACE("ret to %04X:%04X", P->ClassId, P->Ip); ReloadStream(); }
            break;
        case 0x51:
            si16a = (int16)Cs.ReadU16(); ui16b = S.Pop2();
            if (!ui16b) { Cs.Seek(Cs.Pos + si16a); TRACE("jne taken -> %04X", Cs.Pos); } else TRACE("jne not taken");
            break;
        case 0x52: si16a = (int16)Cs.ReadU16(); Cs.Seek(Cs.Pos + si16a); TRACE("jmp -> %04X", Cs.Pos); break;
        case 0x53: TRACE("suspend"); GoUntilCede = false; Cede = true; break;
        case 0x54:
        {
            Cs.ReadU16();
            ui16a = S.Pop2(); ui16b = S.Pop2(); S.Push2(ui16a);
            FProcess* Proc = GCruKernel->GetProcess(ui16b); FProcess* Proc2 = GCruKernel->GetProcess(ui16a);
            TRACE("implies %d waits for %d", ui16b, ui16a);
            if (Proc && Proc2) { Proc->WaitFor(ui16a); GoUntilCede = true; }
            else
            {
                UE_LOG(LogCru, Warning, TEXT("usecode: implies with missing pid (%u, %u)"), ui16a, ui16b);
                if ((ui16a && !Proc2) || (ui16b && !Proc)) Error = true;
            }
            break;
        }
        case 0x57:
        {
            const int32 ArgBytes = Cs.ReadByte(); const int32 ThisSize = Cs.ReadByte();
            uint16 NewClass = Cs.ReadU16(); uint16 Offset = Cs.ReadU16();
            const uint32 ThisPtr = S.Pop4();
            Offset = Usecode->GetClassEvent(NewClass, Offset);
            TRACE("spawn %04X:%04X (%s) args %d this %d", NewClass, Offset, *Usecode->GetClassName(NewClass), ArgBytes, ThisSize);
            FUCProcess* NewProc = new FUCProcess(NewClass, Offset, ThisPtr, ThisSize, S.Access(), ArgBytes);
            P->Temp32 = GCruKernel->AddProcessExec(NewProc);
            break;
        }
        case 0x59: S.Push2(P->Pid); TRACE("push pid"); break;
        case 0x5A: ui16a = Cs.ReadByte(); if (ui16a & 1) ui16a++; if (ui16a > 0) S.Push0(ui16a); TRACE("init %d", ui16a); break;
        case 0x5B: Cs.ReadU16(); break;
        case 0x5C: Cs.ReadU16(); for (int i = 0; i < 9; ++i) Cs.ReadByte(); break;
        case 0x5D: S.Push2((uint8)(P->Temp32 & 0xFF)); TRACE("push byte retval %02X", P->Temp32 & 0xFF); break;
        case 0x5E: S.Push2((uint16)(P->Temp32 & 0xFFFF)); TRACE("push retval %04X", P->Temp32 & 0xFFFF); break;
        case 0x5F: S.Push4(P->Temp32); TRACE("push dword retval %08X", P->Temp32); break;
        case 0x60: si32a = (int16)S.Pop2(); S.Push4((uint32)si32a); break;
        case 0x61: si16a = (int16)S.Pop4(); S.Push2((uint16)si16a); break;
        case 0x62: si8a = Cs.ReadSByte(); FreeString(S.Access2(P->Bp + si8a)); break;
        case 0x63: si8a = Cs.ReadSByte(); FreeStringList(S.Access2(P->Bp + si8a)); break;
        case 0x64: si8a = Cs.ReadSByte(); FreeList(S.Access2(P->Bp + si8a)); TRACE("free list [BP%+d]", si8a); break;
        case 0x65: si8a = Cs.ReadSByte(); FreeString(S.Access2(S.GetSP() + si8a)); break;
        case 0x66: si8a = Cs.ReadSByte(); FreeList(S.Access2(S.GetSP() + si8a)); TRACE("free list [SP%+d]", si8a); break;
        case 0x67: si8a = Cs.ReadSByte(); FreeStringList(S.Access2(S.GetSP() + si8a)); break;
        case 0x69: si8a = Cs.ReadSByte(); S.Push4(StringToPtr(S.Access2(P->Bp + si8a))); break;
        case 0x6B: ui16a = S.Pop2(); S.Push4(StringToPtr(ui16a)); break;
        case 0x6C:
        {
            si8a = (int8)Cs.ReadByte(); ui8a = Cs.ReadByte();
            ui16a = S.Access2(P->Bp + si8a);
            switch (ui8a)
            {
            case 1: ui16b = DuplicateString(ui16a); break;
            case 2: { FUCList* L = new FUCList(2); if (FUCList* Src = GetList(ui16a)) for (uint32 i = 0; i < Src->GetSize(); ++i) L->AppendUInt16(DuplicateString(Src->GetStringIndex(i))); ui16b = AssignList(L); break; }
            case 3: { FUCList* Src = GetList(ui16a); if (!Src) { ui16b = 0; break; } FUCList* L = new FUCList(Src->GetElementSize()); L->CopyList(*Src); ui16b = AssignList(L); break; }
            default: ui16b = 0; Error = true; break;
            }
            S.Assign2(P->Bp + si8a, ui16b);
            P->FreeOnTerminate(ui16b, ui8a);
            break;
        }
        case 0x6D: S.Push4(P->Result); TRACE("push process result %08X", P->Result); break;
        case 0x6E: si8a = Cs.ReadSByte(); S.AddSP(-si8a); TRACE("add sp %d", -si8a); break;
        case 0x6F: si8a = Cs.ReadSByte(); S.Push4(StackToPtr(P->Pid, (uint16)(S.GetSP() - si8a))); break;
        case 0x70:
        {
            si16a = Cs.ReadSByte(); const uint32 ScriptSize = Cs.ReadByte(); const uint32 SearchType = Cs.ReadByte();
            ui16a = S.Pop2(); ui16b = S.Pop2();
            if (ScriptSize > 0x20) { Error = true; break; }
            uint8 Script[0x20]; S.Pop(Script, ScriptSize);
            uint32 StackSize = 0;
            FUCList* ItemList = new FUCList(2);
            switch (SearchType)
            {
            case 2: case 3:
            {
                StackSize = 0x3A;
                const FCruItem* It = GCruWorld->GetItem(ui16a);
                const uint16 Range = ui16b * 2;
                if (It) GCruWorld->AreaSearch(*ItemList, Script, ScriptSize, Range, It->X, It->Y);
                else UE_LOG(LogCru, Warning, TEXT("usecode: area search around missing item %u"), ui16a);
                break;
            }
            case 4: case 5:
                StackSize = SearchType == 5 ? 0x2C : 0x2A;
                GCruWorld->ContainerSearch(*ItemList, Script, ScriptSize, ui16b, SearchType == 5);
                break;
            case 6:
            {
                StackSize = 0x43;
                const bool bAbove = ui16a != 0xFFFF, bBelow = ui16b != 0xFFFF;
                const FCruItem* It = GCruWorld->GetItem(bBelow ? ui16b : ui16a);
                if (It) GCruWorld->SurfaceSearch(*ItemList, Script, ScriptSize, *It, bAbove, bBelow, false);
                break;
            }
            default: UE_LOG(LogCru, Warning, TEXT("usecode: unhandled search type %u"), SearchType); Error = true; break;
            }
            if (Error) { delete ItemList; break; }
            S.Push0(StackSize - ScriptSize - 8);
            S.Push(Script, ScriptSize);
            S.Push2((uint16)ScriptSize);
            S.Push2((uint16)si16a);
            S.Push2(0);
            S.Push2(AssignList(ItemList));
            TRACE("loop [BP%+d] type %d: %d items", si16a, SearchType, ItemList->GetSize());
        }
        // fall through
        case 0x73:
        {
            const uint32 Sp = S.GetSP();
            const uint16 ListId = S.Access2(Sp);
            FUCList* ItemList = GetList(ListId);
            uint16 Index = S.Access2(Sp + 2);
            si16a = (int16)S.Access2(Sp + 4);
            if (!ItemList) { Error = true; break; }
            bool bValid = false;
            while (Index < ItemList->GetSize())
            {
                S.Assign(P->Bp + si16a, (*ItemList)[Index], 2);
                if (GCruWorld->GetItem(S.Access2(P->Bp + si16a))) { bValid = true; break; }
                ++Index;
            }
            if (!bValid) { S.Push2(0); FreeList(ListId); }
            else { S.Push2(1); S.Assign2(Sp + 2, Index + 1); }
            if (Opcode == 0x73) TRACE("loopnext -> %d", bValid);
            break;
        }
        case 0x74: ui8a = Cs.ReadByte(); S.Push1(ui8a); break;
        case 0x75: case 0x76:
        {
            si8a = (int8)Cs.ReadByte(); ui32a = Cs.ReadByte(); si16a = (int16)Cs.ReadU16();
            ui16a = S.Access2(S.GetSP()); ui16b = S.Access2(S.GetSP() + 2);
            FUCList* L = GetList(ui16b);
            if (!L) { Error = true; break; }
            if (ui16a == 0xFFFF) ui16a = 0; else ++ui16a;
            if (ui16a >= L->GetSize())
            {
                if (Opcode == 0x75) FreeList(ui16b); else FreeStringList(ui16b);
                S.AddSP(4);
                Cs.Seek(Cs.Pos + si16a);
            }
            else
            {
                S.Assign2(S.GetSP(), ui16a);
                S.Assign(P->Bp + si8a, (*L)[ui16a], ui32a);
            }
            TRACE("foreach");
            break;
        }
        case 0x77: P->ItemNum = S.Pop2(); P->Type = S.Pop2(); TRACE("set info item %d type %d", P->ItemNum, P->Type); break;
        case 0x78:
            if (GCruKernel->GetNumProcesses(P->ItemNum, P->Type) > 1) { P->TerminateDeferred(); TRACE("process exclude (terminating)"); }
            else TRACE("process exclude");
            break;
        case 0x79: ui16a = Cs.ReadU16(); S.Push4(GlobalToPtr(ui16a)); break;
        case 0x7A: UE_LOG(LogCru, Warning, TEXT("usecode: end-of-function opcode reached in class %d"), P->ClassId); Error = true; break;
        default: UE_LOG(LogCru, Warning, TEXT("usecode: unhandled opcode %02X in class %d at %04X"), Opcode, P->ClassId, OpIp); Error = true; break;
        }

        if (!Error) P->Ip = (uint16)Cs.Pos;
        if ((P->Flags & FProcess::PROC_SUSPENDED) != 0 && !GoUntilCede) Cede = true;
    }

    if (Error)
    {
        UE_LOG(LogCru, Warning, TEXT("usecode: process %d (class %s) error at %04X:%04X, item %d; killing"), P->Pid, *Usecode->GetClassName(P->ClassId), P->ClassId, P->Ip, P->ItemNum);
        P->TerminateDeferred();
    }
}
