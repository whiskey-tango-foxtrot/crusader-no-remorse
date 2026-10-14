using UnrealBuildTool;
public class CrusaderRemakeTarget : TargetRules
{
    public CrusaderRemakeTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("CrusaderRemake");
    }
}
