using UnrealBuildTool;
public class CrusaderRemake : ModuleRules
{
    public CrusaderRemake(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Paper2D", "EnhancedInput", "Json", "JsonUtilities", "MediaAssets", "AudioMixer", "Slate", "SlateCore", "ApplicationCore" });
    }
}
