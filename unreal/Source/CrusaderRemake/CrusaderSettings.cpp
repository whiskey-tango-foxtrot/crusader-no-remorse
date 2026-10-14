#include "CrusaderSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FCruSettings& FCruSettings::Get()
{
    static FCruSettings* S = nullptr;
    if (!S) { S = new FCruSettings(); S->Load(); }
    return *S;
}

FString FCruSettings::FilePath() { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("crusader_settings.json"); }

bool FCruSettings::Load()
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FilePath())) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return false;
    MusicVolume = FMath::Clamp((int32)Root->GetNumberField(TEXT("music")), 0, 10);
    SfxVolume = FMath::Clamp((int32)Root->GetNumberField(TEXT("sfx")), 0, 10);
    bShowKeyHelp = Root->HasField(TEXT("keyhelp")) ? Root->GetBoolField(TEXT("keyhelp")) : true;
    return true;
}

bool FCruSettings::Save() const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("music"), MusicVolume);
    Root->SetNumberField(TEXT("sfx"), SfxVolume);
    Root->SetBoolField(TEXT("keyhelp"), bShowKeyHelp);
    FString Out;
    FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
    return FFileHelper::SaveStringToFile(Out, *FilePath());
}
