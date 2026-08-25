using UnrealBuildTool;
using System.IO;

public class HoloVCS : ModuleRules
{
    public HoloVCS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        CppStandard = CppStandardVersion.Latest;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "AudioMixer" });

        PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "EngineSettings", "RenderCore" });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            //these seem to have no effect, but they work when used in HoloVCS*.target instead?!
            PublicDefinitions.Add("_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS");
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
        }

        CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;

        //The emulator cores are separate GPL projects living in cores/, built as DLLs (see BuildCores.bat) and
        //loaded at runtime.  Keep it that way - statically linking GPL code into the UE binary is a license violation.
        //We only borrow this one header, the ABI struct the patched beetle-vb core hands us layers through.
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "..", "cores", "beetle-vb", "mednafen", "vb"));
        //Same idea, v2 of that ABI: the patched Azahar (3DS) core's depth-sliced layers (canonical
        //copy synced with the fork, see the header comment).
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "..", "cores", "holo_abi"));
    }
}
