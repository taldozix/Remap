using UnrealBuildTool;

public class VilaAuroraEditorTarget : TargetRules
{
	public VilaAuroraEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "VilaCore", "VilaAurora" });
	}
}
