#include "CruInventory.h"
#include "CruWorld.h"
#include "CruEngine.h"
#include "CruCombat.h"
#include "Misc/FileHelper.h"

static TArray<uint8> GEquipTypes;

bool CruInventory::LoadEquipTypes(const FString& Path)
{
    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *Path)) { UE_LOG(LogCru, Warning, TEXT("inventory: cannot read %s"), *Path); return false; }
    const int32 N = Data.Num() / 9;
    GEquipTypes.SetNum(N);
    int32 Typed = 0;
    for (int32 i = 0; i < N; ++i) { GEquipTypes[i] = (Data[i * 9 + 2] >> 1) & 0xF; if (GEquipTypes[i]) ++Typed; }
    UE_LOG(LogCru, Display, TEXT("inventory: equip types for %d shapes (%d non-zero)"), N, Typed);
    return true;
}

uint8 CruInventory::GetEquipType(uint32 Shape) { return GEquipTypes.IsValidIndex(Shape) ? GEquipTypes[Shape] : 0; }

// ---- containers (ScummVM world/container.cpp, world/item.cpp) ----

bool CruInventory::IsContainer(const FCruItem& It) { return It.Id < 256 || GCruWorld->Info(It).Family == Cru::SF_CONTAINER; }

// Container::CanAddItem (container.cpp:80); Crusader never checks weight / volume (MainActor::CanAddItem returns true)
bool CruInventory::CanAddItem(const FCruItem& C, const FCruItem& It)
{
    if (It.Parent == C.Id) return true;
    if (It.Id < 256) return false;   // actors don't fit in containers
    if (IsContainer(It))
        for (const FCruItem* P = &C; P; P = P->Parent ? GCruWorld->GetItem(P->Parent) : nullptr)
            if (P == &It) return false;   // "watch for snake eating itself"
    return true;
}

void CruInventory::RemoveFromContainer(FCruItem& It)
{
    if (FCruItem* P = It.Parent ? GCruWorld->GetItem(It.Parent) : nullptr) P->Contents.Remove(It.Id);
}

void CruInventory::SetFlagRecursively(FCruItem& It, uint16 Mask)
{
    It.Flags |= Mask;
    for (ObjId Id : It.Contents) if (FCruItem* C = GCruWorld->GetItem(Id)) SetFlagRecursively(*C, Mask);
}

// Item::moveToContainer (item.cpp:273)
bool CruInventory::MoveToContainer(FCruItem& It, FCruItem& C)
{
    bool bEtherealSame = false;
    if (C.Id == It.Parent) { if (It.Flags & Cru::FLG_ETHEREAL) bEtherealSame = true; else return true; }
    if (!CanAddItem(C, It)) return false;
    if (It.Flags & Cru::FLG_ETHEREAL) GCruWorld->EtherealRemove(It.Id);
    if (It.Flags & (Cru::FLG_CONTAINED | Cru::FLG_EQUIPPED))
    {
        if (It.Parent) { if (!(It.Flags & Cru::FLG_ETHEREAL)) RemoveFromContainer(It); }   // ethereal items were already removed
        else UE_LOG(LogCru, Warning, TEXT("inventory: item %d contained/equipped but has no parent"), It.Id);
        It.Parent = 0;
    }
    else if (It.ExtFlags & Cru::EXT_INCURMAP) GCruWorld->RemoveItemFromMap(It);
    It.Flags &= ~(Cru::FLG_CONTAINED | Cru::FLG_EQUIPPED | Cru::FLG_ETHEREAL);
    if (!bEtherealSame) { It.X = 0; It.Y = 0; }
    It.Z = 0;
    C.Contents.AddUnique(It.Id);
    It.Parent = C.Id;
    It.Flags |= Cru::FLG_CONTAINED;
    if (const FCruItem* Root = GetTopItem(It)) if (Root->Id == Cru::kMainActorId) SetFlagRecursively(It, Cru::FLG_OWNED);
    It.ExtFlags |= Cru::EXT_LERP_NOPREV;
    GCruWorld->Notify(It, ECruItemChange::Moved);   // the renderer hides contained items
    GCruWorld->CallUsecodeEvent(It, Cru::EV_JUSTMOVED);
    const bool bDestFast = (C.Flags & Cru::FLG_GUMP_OPEN) != 0;   // a container is fast when its gump is open
    if (!bDestFast && (It.Flags & Cru::FLG_FASTAREA)) GCruWorld->LeaveFastArea(It);
    else if (bDestFast && !(It.Flags & Cru::FLG_FASTAREA)) GCruWorld->EnterFastArea(It);
    return true;
}

// Container::removeContents (container.cpp:170)
void CruInventory::RemoveContents(FCruItem& C)
{
    FCruItem* ParentCon = C.Parent ? GCruWorld->GetItem(C.Parent) : nullptr;
    TArray<ObjId> Items = C.Contents;
    for (ObjId Id : Items)
    {
        FCruItem* It = GCruWorld->GetItem(Id);
        if (!It) { C.Contents.Remove(Id); continue; }
        if (ParentCon) MoveToContainer(*It, *ParentCon);
        else GCruWorld->Move(*It, C.X, C.Y, C.Z);
    }
    C.Contents.Reset();
}

// Container::destroyContents (container.cpp:190)
void CruInventory::DestroyContents(FCruItem& C)
{
    TArray<ObjId> Items = C.Contents;
    for (ObjId Id : Items)
    {
        FCruItem* It = GCruWorld->GetItem(Id);
        if (!It) continue;
        if (IsContainer(*It)) DestroyContents(*It);
        GCruWorld->DestroyItem(*It);
    }
    C.Contents.Reset();
}

FCruItem* CruInventory::GetFirstItemWithShape(const FCruItem& C, uint32 Shape, bool bRecurse)
{
    for (ObjId Id : C.Contents)
    {
        FCruItem* It = GCruWorld->GetItem(Id);
        if (!It) continue;
        if (It->Shape == Shape) return It;
        if (bRecurse && IsContainer(*It)) if (FCruItem* R = GetFirstItemWithShape(*It, Shape, true)) return R;
    }
    return nullptr;
}

void CruInventory::GetItemsWithShapeFamily(const FCruItem& C, uint8 Family, bool bRecurse, TArray<FCruItem*>& Out)
{
    for (ObjId Id : C.Contents)
    {
        FCruItem* It = GCruWorld->GetItem(Id);
        if (!It) continue;
        if (GCruWorld->Info(*It).Family == Family) Out.Add(It);
        if (bRecurse && IsContainer(*It)) GetItemsWithShapeFamily(*It, Family, true, Out);
    }
}

const FCruItem* CruInventory::GetTopItem(const FCruItem& It)
{
    const FCruItem* Top = &It;
    for (int32 Guard = 0; Top->Parent && Guard < 64; ++Guard) { const FCruItem* P = GCruWorld->GetItem(Top->Parent); if (!P) break; Top = P; }
    return Top;
}

FIntVector CruInventory::GetLocationAbsolute(const FCruItem& It)
{
    const FCruItem* Top = GetTopItem(It);
    return FIntVector(Top->X, Top->Y, Top->Z);
}

// ---- equipment (actor.cpp:396-435) ----

ObjId CruInventory::GetEquip(const FCruItem& A, uint32 Type)
{
    for (ObjId Id : A.Contents)
        if (const FCruItem* It = GCruWorld->GetItem(Id))
            if ((It->Flags & Cru::FLG_EQUIPPED) && GetEquipType(It->Shape) == Type) return It->Id;
    return 0;
}

bool CruInventory::SetEquip(FCruItem& A, FCruItem& It)
{
    const uint32 EquipType = GetEquipType(It.Shape);
    if (EquipType == 0) return false;
    for (ObjId Id : A.Contents)
    {
        if (Id == It.Id) continue;
        if (const FCruItem* O = GCruWorld->GetItem(Id)) if (GetEquipType(O->Shape) == EquipType) return false;   // slot taken
    }
    if (!MoveToContainer(It, A)) return false;
    It.Flags &= ~Cru::FLG_CONTAINED;
    It.Flags |= Cru::FLG_EQUIPPED;
    It.Z = EquipType;
    return true;
}

// ---- avatar ----

int32 CruInventory::GetMaxEnergy()
{
    switch (GCruEngine ? GCruEngine->BatteryType : ECruBattery::None)
    {
    case ECruBattery::Chemical: return 0x9c4;   // docs say 2500, code says otherwise.. (ScummVM main_actor.cpp:681)
    case ECruBattery::Fission: return 5000;
    case ECruBattery::Fusion: return 10000;
    default: return 0;
    }
}

int32 CruInventory::GetNumberOfCredits()
{
    const FCruItem* Av = GCruWorld->GetItem(Cru::kMainActorId);
    const FCruItem* Credits = Av ? GetFirstItemWithShape(*Av, 0x4ED, true) : nullptr;
    return Credits ? Credits->Quality : 0;
}

FString CruInventory::ItemName(const FCruItem& It)
{
    if (const FCruWeaponInfo* Wi = CruGetWeaponInfo(It.Shape)) return Wi->Name;
    FString N = GCruEngine ? GCruEngine->Usecode.GetClassName(It.Shape) : FString();
    if (N.IsEmpty() && GCruWorld->Info(It).Family == Cru::SF_CRUAMMO)
    {
        // ammo shapes have no usecode class: name them after the (first) weapon that uses them
        static const uint32 WeaponShapes[] = { 0x32E, 0x32F, 0x330, 0x38C, 0x332, 0x333, 0x334, 0x38E, 0x388, 0x38A, 0x38D, 0x38B, 0x386 };
        for (uint32 W : WeaponShapes) if (const FCruWeaponInfo* Wi = CruGetWeaponInfo(W)) if (Wi->AmmoShape == It.Shape) return FString::Printf(TEXT("%s ammo"), Wi->Name);
    }
    return N.IsEmpty() ? FString::Printf(TEXT("shape%d"), It.Shape) : N;
}

static void SetQualityAndCombine(FCruItem& It, uint16 Q) { It.Quality = Q; GCruWorld->CallUsecodeEvent(It, Cru::EV_COMBINE); }

static void LogPickup(const FCruItem& It, bool bShowToast, const TCHAR* What)
{
    UE_LOG(LogCru, Display, TEXT("inventory: %s %s (shape %d q %d)%s"), What, *CruInventory::ItemName(It), It.Shape, It.Quality, bShowToast ? TEXT(" [toast]") : TEXT(""));
    // CruPickupAreaGump::addPickup: credits, weapons and ammo show their count, the rest only when picked up again
    if (bShowToast && GCruEngine) GCruEngine->AddPickup(It, FCString::Strnicmp(What, TEXT("credits"), 7) == 0 || FCString::Strnicmp(What, TEXT("weapon"), 6) == 0 || FCString::Strnicmp(What, TEXT("ammo"), 4) == 0);
}

// MainActor::addItemCru (ScummVM world/actors/main_actor.cpp:127-325, original World_GiveItemToAvatar 13e8:0330)
int16 CruInventory::AddItemCru(FCruItem& Item, bool bShowToast)
{
    FCruItem* Av = GCruWorld->GetItem(Cru::kMainActorId);
    if (!Av || !Item.Shape) return 0;
    const uint32 ShapeNo = Item.Shape;
    const FIntVector Pt(Av->X, Av->Y, Av->Z);

    if (ShapeNo == 0x4ED)   // credits (main_actor.cpp:141)
    {
        if (FCruItem* Credits = GetFirstItemWithShape(*Av, ShapeNo, true))
        {
            uint32 NewQ = (uint32)Credits->Quality + Item.Quality;
            if (NewQ > 64000) NewQ = 64000;
            SetQualityAndCombine(*Credits, (uint16)NewQ);
            LogPickup(Item, bShowToast, TEXT("credits +"));
            GCruWorld->DestroyItem(Item);
        }
        else
        {
            GCruWorld->SetFrame(Item, 0);
            MoveToContainer(Item, *Av);
            if (!GCruEngine->ActiveInvItem) GCruEngine->ActiveInvItem = Item.Id;
            LogPickup(Item, bShowToast, TEXT("credits"));
        }
        return 1;
    }

    switch (GCruWorld->Info(Item).Family)
    {
    case Cru::SF_CRUWEAPON:   // main_actor.cpp:165
    {
        if (!GetFirstItemWithShape(*Av, ShapeNo, true))
        {
            // New weapon. Add it with a full clip (or quality 0 + combine for energy weapons).
            const FCruWeaponInfo* Wi = CruGetWeaponInfo(ShapeNo);
            if (!Wi || Wi->AmmoType == 0) SetQualityAndCombine(Item, 0);
            else Item.Quality = Wi->ClipSize;
            Item.X = Pt.X; Item.Y = Pt.Y; Item.Z = Pt.Z;
            MoveToContainer(Item, *Av);
            if (!Av->ActiveWeapon) Av->ActiveWeapon = Item.Id;
            LogPickup(Item, bShowToast, TEXT("weapon"));
            return 1;   // (ScummVM breaks and returns 0 here, the original returns 1; the item is absorbed either way)
        }
        break;   // already have this weapon: nothing happens, the item stays where it is
    }
    case Cru::SF_CRUAMMO:   // main_actor.cpp:186
    {
        FCruItem* Ammo = GetFirstItemWithShape(*Av, ShapeNo, true);
        if (!Ammo)
        {
            SetQualityAndCombine(Item, 1);
            MoveToContainer(Item, *Av);
            LogPickup(Item, bShowToast, TEXT("ammo"));
            return 1;
        }
        if (Ammo->Quality < 0x14)   // at most 20 clips of each ammo type
        {
            SetQualityAndCombine(*Ammo, Ammo->Quality + 1);
            LogPickup(Item, bShowToast, TEXT("ammo +1"));
            GCruWorld->DestroyItem(Item);
            return 1;
        }
        break;
    }
    case Cru::SF_CRUBOMB:      // main_actor.cpp:210
    case Cru::SF_CRUINVITEM:
    {
        if (ShapeNo == 0x111)   // keycard: quality low byte = bit number
        {
            const int32 Bit = Item.Quality & 0xFF;
            if (Bit <= 31) GCruEngine->Keycards |= (1u << Bit);
            LogPickup(Item, bShowToast, TEXT("keycard"));
            GCruWorld->DestroyItem(Item);
            return 1;
        }
        if (ShapeNo == 0x3A2 || ShapeNo == 0x3A3 || ShapeNo == 0x3A4)   // batteries (chemical / fusion / fission)
        {
            LogPickup(Item, bShowToast, TEXT("battery"));
            GCruWorld->DestroyItem(Item);
            int32 PlusEnergy = 0;
            const ECruBattery Old = GCruEngine->BatteryType;
            if (ShapeNo == 0x3A2) { if (Old == ECruBattery::None) GCruEngine->BatteryType = ECruBattery::Chemical; else PlusEnergy = 0x9c4; }
            else if (ShapeNo == 0x3A4) { if (Old < ECruBattery::Fission) GCruEngine->BatteryType = ECruBattery::Fission; else PlusEnergy = 5000; }
            else if (ShapeNo == 0x3A3) { if (Old < ECruBattery::Fusion) GCruEngine->BatteryType = ECruBattery::Fusion; else PlusEnergy = 10000; }
            if (PlusEnergy) Av->Mana = FMath::Min(Av->Mana + PlusEnergy, GetMaxEnergy());
            return 1;
        }
        FCruItem* Existing = GetFirstItemWithShape(*Av, ShapeNo, true);
        if (!Existing)
        {
            if (ShapeNo == 0x52E || ShapeNo == 0x52F || ShapeNo == 0x530)   // shields: ionic / plasma / graviton generator
            {
                const uint16 ShieldType = ShapeNo == 0x52F ? 2 : ShapeNo == 0x530 ? 3 : 1;
                if (GCruEngine->ShieldType < ShieldType) GCruEngine->ShieldType = ShieldType;
                LogPickup(Item, bShowToast, TEXT("shield"));
                GCruWorld->DestroyItem(Item);
                return 1;
            }
            GCruWorld->SetFrame(Item, 0);
            SetQualityAndCombine(Item, 1);
            MoveToContainer(Item, *Av);
            if (!GCruEngine->ActiveInvItem) GCruEngine->ActiveInvItem = Item.Id;
            LogPickup(Item, bShowToast, TEXT("item"));
            return 1;
        }
        // Already have this item..
        if (ShapeNo == 0x52E || ShapeNo == 0x52F || ShapeNo == 0x530) { GCruWorld->DestroyItem(Item); return 1; }   // shields: destroy the new one
        const uint16 Max = ShapeNo == 0x560 ? 0x14 : 10;   // spider mines stack to 20, everything else to 10
        if (Existing->Quality < Max)
        {
            SetQualityAndCombine(*Existing, Existing->Quality + 1);
            Item.Quality = 1;   // count of picked up item is always 1
            LogPickup(Item, bShowToast, TEXT("item +1"));
            GCruWorld->DestroyItem(Item);
            return 1;
        }
        break;
    }
    default: break;
    }
    UE_LOG(LogCru, Display, TEXT("inventory: not taken: %s (shape %d q %d)"), *ItemName(Item), Item.Shape, Item.Quality);
    return 0;
}

// MainActor::nextWeapon / getIdOfNextItemInList (main_actor.cpp:704-735)
void CruInventory::NextWeapon(FCruItem& Av, int32 Delta)
{
    TArray<FCruItem*> Weapons;
    GetItemsWithShapeFamily(Av, Cru::SF_CRUWEAPON, true, Weapons);
    const int32 N = Weapons.Num();
    if (N == 0) { Av.ActiveWeapon = 0; return; }
    if (N == 1) { Av.ActiveWeapon = Weapons[0]->Id; return; }
    int32 i = N;   // not found -> wraps to the first
    for (int32 k = 0; k < N; ++k) if (Weapons[k]->Id == Av.ActiveWeapon) { i = k; break; }
    if (i == N) Av.ActiveWeapon = Weapons[0]->Id;
    else Av.ActiveWeapon = Weapons[(i + (Delta < 0 ? N - 1 : 1)) % N]->Id;
}

void CruInventory::NextInvItem(FCruItem& Av)
{
    TArray<FCruItem*> Items;
    GetItemsWithShapeFamily(Av, Cru::SF_CRUINVITEM, true, Items);
    GetItemsWithShapeFamily(Av, Cru::SF_CRUBOMB, true, Items);
    if (FCruItem* Credits = GetFirstItemWithShape(Av, 0x4ED, true)) Items.Add(Credits);
    const int32 N = Items.Num();
    if (N == 0) { GCruEngine->ActiveInvItem = 0; return; }
    int32 i = N;
    for (int32 k = 0; k < N; ++k) if (Items[k]->Id == GCruEngine->ActiveInvItem) { i = k + 1; break; }
    GCruEngine->ActiveInvItem = Items[i % N]->Id;
}

FString CruInventory::Summary(const FCruItem& Av)
{
    FString Weapons, Ammo, Items;
    for (ObjId Id : Av.Contents)
    {
        const FCruItem* It = GCruWorld->GetItem(Id);
        if (!It) continue;
        const uint8 Fam = GCruWorld->Info(*It).Family;
        if (Fam == Cru::SF_CRUWEAPON)
        {
            const FCruWeaponInfo* Wi = CruGetWeaponInfo(It->Shape);
            Weapons += FString::Printf(TEXT("%s%s%s "), It->Id == Av.ActiveWeapon ? TEXT("*") : TEXT(""), *ItemName(*It),
                (Wi && Wi->AmmoShape) ? *FString::Printf(TEXT(" %d/%d"), It->Quality, Wi->ClipSize) : TEXT(""));
        }
        else if (Fam == Cru::SF_CRUAMMO) Ammo += FString::Printf(TEXT("%s x%d "), *ItemName(*It), It->Quality);
        else if (It->Shape != 0x4ED) Items += FString::Printf(TEXT("%s x%d "), *ItemName(*It), It->Quality);
    }
    FString Keys;
    for (int32 B = 0; B < 32; ++B) if (GCruEngine && (GCruEngine->Keycards & (1u << B))) Keys += FString::Printf(TEXT("%d "), B);
    return FString::Printf(TEXT("Weapons: %s| Ammo: %s| Items: %s| Credits %d | Keycards: %s| Shield %d"),
        Weapons.IsEmpty() ? TEXT("- ") : *Weapons, Ammo.IsEmpty() ? TEXT("- ") : *Ammo, Items.IsEmpty() ? TEXT("- ") : *Items,
        GetNumberOfCredits(), Keys.IsEmpty() ? TEXT("- ") : *Keys, GCruEngine ? GCruEngine->ShieldType : 0);
}
