using UnrealBuildTool;
public class CrusaderRemakeEditorTarget : TargetRules
{
    public CrusaderRemakeEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("CrusaderRemake");
    }
}
