using UnrealBuildTool;

// Núcleo de simulação em C++ puro (sem UObject). O mesmo código compila fora do Unreal
// (unreal/Tests/VilaCore, CMake + g++) e é coberto pelos 283 testes portados do Godot.
public class VilaCore : ModuleRules
{
	public VilaCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;            // os .cpp têm helpers estáticos com nomes repetidos
		bEnableExceptions = true;     // nlohmann::json usa exceções para JSON inválido
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDefinitions.Add("_CRT_SECURE_NO_WARNINGS=1");
	}
}
