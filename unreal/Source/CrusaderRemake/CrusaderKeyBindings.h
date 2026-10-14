#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

// Rebindable keys (the original game had fixed keys). One entry per game action with its current and default keys;
// persisted as Saved/SaveGames/crusader_keys.json. The pawn rebuilds its Enhanced Input mapping context from this table.
struct CRUSADERREMAKE_API FCruKeyBindings
{
    struct FEntry { FName Action; FString Label; TArray<FKey> Keys; TArray<FKey> Defaults; };
    TArray<FEntry> Entries;

    static FCruKeyBindings& Get();
    FEntry* Find(FName Action);
    const TArray<FKey>& Keys(FName Action) const;
    void ResetDefaults();
    bool Load();
    bool Save() const;
    static FString FilePath();
    FString Describe(const FEntry& E) const;           // "W, Up"
    static FString KeyName(const FKey& K);              // short display name
private:
    FCruKeyBindings();
    void Add(const TCHAR* Action, const TCHAR* Label, std::initializer_list<FKey> Keys);
};
