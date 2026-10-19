using UnrealBuildTool;

public class VilaAuroraTarget : TargetRules
{
	public VilaAuroraTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "VilaCore", "VilaAurora" });
	}
}
