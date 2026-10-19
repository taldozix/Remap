using UnrealBuildTool;

public class VilaAurora : ModuleRules
{
	public VilaAurora(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true;  // inclui nlohmann::json (via VilaCore)

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"VilaCore",
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "ApplicationCore",
			"NavigationSystem", "AIModule",
			"AudioMixer", "AudioExtensions",
			"MeshDescription", "StaticMeshDescription",
			"PhysicsCore", "RenderCore", "RHI",
			"DeveloperSettings", "ImageCore",
		});
	}
}
