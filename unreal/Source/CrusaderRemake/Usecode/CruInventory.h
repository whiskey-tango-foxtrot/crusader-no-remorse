// Crusader inventory: containers (ScummVM Container / Item::moveToContainer), MainActor::addItemCru, equipment
// slots (Actor::getEquip / setEquip), weapon cycling and the avatar's energy / battery. Ported from ScummVM ultima8 (GPL).
#pragma once
#include "CruCore.h"

enum class ECruBattery : uint8 { None = 0, Chemical = 1, Fission = 2, Fusion = 3 };

namespace CruInventory
{
    // ShapeInfo::_equipType for Crusader: TYPEFLAG.DAT byte 2 bits 1..4 (gfx/type_flags.cpp)
    bool LoadEquipTypes(const FString& TypeflagDat);
    uint8 GetEquipType(uint32 Shape);

    // ---- containers ----
    bool IsContainer(const FCruItem& It);                          // actors and SF_CONTAINER shapes (ScummVM Container subclass)
    bool CanAddItem(const FCruItem& Container, const FCruItem& Item);
    bool MoveToContainer(FCruItem& Item, FCruItem& Container);     // Item::moveToContainer
    void RemoveFromContainer(FCruItem& Item);                      // Container::removeItem on the parent (Parent is left for the caller)
    void RemoveContents(FCruItem& Container);                      // Container::removeContents (to the parent container or the floor)
    void DestroyContents(FCruItem& Container);                     // Container::destroyContents
    FCruItem* GetFirstItemWithShape(const FCruItem& Container, uint32 Shape, bool bRecurse);
    void GetItemsWithShapeFamily(const FCruItem& Container, uint8 Family, bool bRecurse, TArray<FCruItem*>& Out);
    const FCruItem* GetTopItem(const FCruItem& It);                // root container, or the item itself
    FIntVector GetLocationAbsolute(const FCruItem& It);
    void SetFlagRecursively(FCruItem& It, uint16 Mask);

    // ---- equipment (Actor::getEquip / setEquip) ----
    ObjId GetEquip(const FCruItem& Actor, uint32 Type);
    bool SetEquip(FCruItem& Actor, FCruItem& Item);

    // ---- avatar ----
    int16 AddItemCru(FCruItem& Item, bool bShowToast);             // MainActor::addItemCru
    int32 GetMaxEnergy();                                          // MainActor::getMaxEnergy
    void NextWeapon(FCruItem& Avatar, int32 Delta);                // MainActor::nextWeapon (Delta < 0: previous)
    void NextInvItem(FCruItem& Avatar);                            // MainActor::nextInvItem
    int32 GetNumberOfCredits();                                    // MainActor::I_getNumberOfCredits
    FString Summary(const FCruItem& Avatar);                       // HUD / log line: weapons with clips, ammo, items, credits, keycards
    FString ItemName(const FCruItem& It);
}
