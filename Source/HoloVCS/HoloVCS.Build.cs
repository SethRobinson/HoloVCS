using UnrealBuildTool;
using System.IO;

public class HoloVCS : ModuleRules
{
    public HoloVCS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Set CppStandardVersion.EngineDefault
        CppStandard = CppStandardVersion.Latest;
        CStandard = CStandardVersion.Latest;
        //set the C standard too

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "AudioMixer" });

        PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // PublicAdditionalLibraries.Add("Shcore.lib");

            //these seem to have no effect, but they work when used in HoloVCS*.target instead?!
            PublicDefinitions.Add("_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS");
            PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");


        }

        bEnableUndefinedIdentifierWarnings = false;
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            PublicDefinitions.Add("__ANDROID__=1"); // Define Android platform
                                                    // Add any other Android-specific definitions here
                                                    //AdditionalLibraries.Add("c++_shared");
            PublicSystemLibraries.Add("c");
            PublicSystemLibraries.Add("m");
            PublicSystemLibraries.Add("dl");
            PublicDefinitions.Add("_strdup=strdup");
        }

        // Add the directory containing the .c files to the include paths
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "nes_core_src"));

        // Add the directory containing libretro.h to the include paths
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "nes_core_src", "drivers", "libretro", "libretro-common", "include"));

        // Add the directory containing libretro_dipswitch.h to the include paths
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "nes_core_src", "drivers", "libretro"));


        // Add custom compiler definitions
        PublicDefinitions.Add("__LIBRETRO__");
        //PublicDefinitions.Add("WINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP");
        PublicDefinitions.Add("FCEU_VERSION_NUMERIC=9813");
        PublicDefinitions.Add("FRONTEND_SUPPORTS_RGB565");
        PublicDefinitions.Add("RT_STATIC_CORE=1"); // If set, we won't load core .dlls, but expect things to be statically linked instead
        PublicDefinitions.Add("__USE_LARGEFILE=0"); // Define __USE_LARGEFILE

        // Add all .c files from the specified directory to be compiled
        string NesCorePath = Path.Combine(ModuleDirectory, "nes_core_src");
        string[] SourceFiles = Directory.GetFiles(NesCorePath, "*.c", SearchOption.AllDirectories);

        foreach (string SourceFile in SourceFiles)
        {
            RuntimeDependencies.Add(SourceFile);
            ExternalDependencies.Add(SourceFile);
        }

        // Ensure all .c files are added as source files for compilation
        foreach (string SourceFile in SourceFiles)
        {
            if (Path.GetExtension(SourceFile).Equals(".c", System.StringComparison.InvariantCultureIgnoreCase))
            {
                PublicIncludePaths.Add(Path.GetDirectoryName(SourceFile));
            }
        }

    }
}
