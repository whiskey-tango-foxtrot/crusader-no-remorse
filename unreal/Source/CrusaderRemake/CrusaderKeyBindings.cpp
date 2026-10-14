#include "CrusaderKeyBindings.h"
#include "SilencerPawn.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCruKeys, Display, All);

FCruKeyBindings& FCruKeyBindings::Get()
{
    static FCruKeyBindings* B = nullptr;
    if (!B) { B = new FCruKeyBindings(); B->Load(); }
    return *B;
}

FCruKeyBindings::FCruKeyBindings()
{
    Add(TEXT("MoveUp"), TEXT("Move up"), {EKeys::W, EKeys::Up});
    Add(TEXT("MoveDown"), TEXT("Move down"), {EKeys::S, EKeys::Down});
    Add(TEXT("MoveLeft"), TEXT("Move left"), {EKeys::A, EKeys::Left});
    Add(TEXT("MoveRight"), TEXT("Move right"), {EKeys::D, EKeys::Right});
    Add(TEXT("Run"), TEXT("Run (hold)"), {EKeys::LeftShift});
    Add(TEXT("Fire"), TEXT("Fire (first click draws the weapon)"), {EKeys::LeftMouseButton});
    Add(TEXT("Use"), TEXT("Use / talk"), {EKeys::E, EKeys::Enter, EKeys::SpaceBar});
    Add(TEXT("Combat"), TEXT("Draw / holster weapon"), {EKeys::RightMouseButton});
    Add(TEXT("Kneel"), TEXT("Kneel"), {EKeys::C});
    Add(TEXT("Jump"), TEXT("Jump / roll / crouch (hold + direction)"), {EKeys::LeftControl});
    Add(TEXT("Step"), TEXT("Sidestep / advance / retreat (hold + direction)"), {EKeys::LeftAlt});
    Add(TEXT("NextWeapon"), TEXT("Next weapon"), {EKeys::Tab, EKeys::Q});
    Add(TEXT("PrevWeapon"), TEXT("Previous weapon"), {EKeys::Z});
    Add(TEXT("UseItem"), TEXT("Use inventory item"), {EKeys::U});
    Add(TEXT("NextItem"), TEXT("Next inventory item"), {EKeys::I});
    Add(TEXT("ZoomIn"), TEXT("Zoom in"), {EKeys::Equals, EKeys::Add});
    Add(TEXT("ZoomOut"), TEXT("Zoom out"), {EKeys::Hyphen, EKeys::Subtract});
    Add(TEXT("Save"), TEXT("Quick save"), {EKeys::F5});
    Add(TEXT("Load"), TEXT("Quick load"), {EKeys::F9});
    Add(TEXT("Bindings"), TEXT("Key bindings menu"), {EKeys::F10});
    Add(TEXT("Help"), TEXT("Toggle key help overlay"), {EKeys::F1});
}

void FCruKeyBindings::Add(const TCHAR* Action, const TCHAR* Label, std::initializer_list<FKey> Keys)
{
    FEntry E; E.Action = Action; E.Label = Label; E.Keys = Keys; E.Defaults = Keys;
    Entries.Add(E);
}

FCruKeyBindings::FEntry* FCruKeyBindings::Find(FName Action)
{
    for (FEntry& E : Entries) if (E.Action == Action) return &E;
    return nullptr;
}

const TArray<FKey>& FCruKeyBindings::Keys(FName Action) const
{
    static const TArray<FKey> None;
    for (const FEntry& E : Entries) if (E.Action == Action) return E.Keys;
    return None;
}

void FCruKeyBindings::ResetDefaults() { for (FEntry& E : Entries) E.Keys = E.Defaults; }

FString FCruKeyBindings::FilePath() { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("crusader_keys.json"); }

bool FCruKeyBindings::Load()
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FilePath())) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return false;
    for (FEntry& E : Entries)
    {
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (!Root->TryGetArrayField(E.Action.ToString(), Arr)) continue;
        E.Keys.Reset();
        for (const TSharedPtr<FJsonValue>& V : *Arr)
        {
            const FKey K(*V->AsString());
            if (K.IsValid()) E.Keys.Add(K);
        }
    }
    UE_LOG(LogCruKeys, Display, TEXT("key bindings loaded from %s"), *FilePath());
    return true;
}

bool FCruKeyBindings::Save() const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    for (const FEntry& E : Entries)
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        for (const FKey& K : E.Keys) Arr.Add(MakeShared<FJsonValueString>(K.ToString()));
        Root->SetArrayField(E.Action.ToString(), Arr);
    }
    FString Out;
    FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
    return FFileHelper::SaveStringToFile(Out, *FilePath());
}

FString FCruKeyBindings::KeyName(const FKey& K) { return K.GetDisplayName(false).ToString(); }

FString FCruKeyBindings::Describe(const FEntry& E) const
{
    FString S;
    for (const FKey& K : E.Keys) { if (!S.IsEmpty()) S += TEXT(", "); S += KeyName(K); }
    return S.IsEmpty() ? TEXT("(none)") : S;
}

static void RebuildPawnBindings(UWorld* World)
{
    if (!World) return;
    for (TActorIterator<ASilencerPawn> It(World); It; ++It) It->RebuildBindings();
}

// Silencer.Binds: list the current key bindings
static FAutoConsoleCommandWithWorldAndArgs GSilencerBindsCmd(
    TEXT("Silencer.Binds"), TEXT("List the key bindings"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
    {
        for (const FCruKeyBindings::FEntry& E : FCruKeyBindings::Get().Entries)
            UE_LOG(LogCruKeys, Display, TEXT("%-12s %-22s %s"), *E.Action.ToString(), *E.Label, *FCruKeyBindings::Get().Describe(E));
    }));

// Silencer.Bind ACTION KEY [KEY...]: replace an action's keys (key names as in FKey, e.g. SpaceBar, LeftMouseButton, F)
static FAutoConsoleCommandWithWorldAndArgs GSilencerBindCmd(
    TEXT("Silencer.Bind"), TEXT("Silencer.Bind ACTION KEY [KEY...]: rebind an action (no keys = clear)"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num() < 1) return;
        FCruKeyBindings& B = FCruKeyBindings::Get();
        FCruKeyBindings::FEntry* E = B.Find(FName(*Args[0]));
        if (!E) { UE_LOG(LogCruKeys, Warning, TEXT("unknown action %s"), *Args[0]); return; }
        E->Keys.Reset();
        for (int32 i = 1; i < Args.Num(); ++i)
        {
            const FKey K(*Args[i]);
            if (K.IsValid()) E->Keys.Add(K); else UE_LOG(LogCruKeys, Warning, TEXT("unknown key %s"), *Args[i]);
        }
        B.Save();
        RebuildPawnBindings(World);
        UE_LOG(LogCruKeys, Display, TEXT("%s = %s"), *E->Action.ToString(), *B.Describe(*E));
    }));

static FAutoConsoleCommandWithWorldAndArgs GSilencerResetBindsCmd(
    TEXT("Silencer.ResetBinds"), TEXT("Restore the default key bindings"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
    {
        FCruKeyBindings::Get().ResetDefaults();
        FCruKeyBindings::Get().Save();
        RebuildPawnBindings(World);
    }));
