using UnrealBuildTool;

public class LookingGlassEditor : ModuleRules
{
	public LookingGlassEditor(ReadOnlyTargetRules Target) : base(Target)
	{
#if UE_5_4_OR_LATER
		DefaultBuildSettings = BuildSettingsVersion.V5;
#else
		DefaultBuildSettings = BuildSettingsVersion.V3;
#endif

		PCHUsage = PCHUsageMode.NoPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
				"HTTP",
				"DetailCustomizations",
				"DesktopWidgets",
				"MovieSceneCapture",
				"Slate",
				"SlateCore",
                "InputCore",
                "Projects",
				"PropertyEditor",
				"UnrealEd",
				"EditorStyle",
				"MainFrame",
				"LevelEditor",
                "Settings",
#if UE_5_0_OR_LATER
				"ToolMenus",
				"ToolWidgets",
#endif
                "LookingGlassRuntime"
            }
			);
	}
}
