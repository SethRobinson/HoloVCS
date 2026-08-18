// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

//Separate game target for the Looking Glass hardware build (HoloVCS.uproject).  The flat and LKG
//uprojects share this source dir; with a single "HoloVCS" game target they also shared
//Intermediate/Binaries output, so whichever flavor linked last got staged by BOTH test-build bats
//(the plugin-enabled monolithic exe is a different binary than the flat one).  A distinct target
//name gives the hardware flavor its own output paths and kills that collision.
public class HoloVCSLKGTarget : TargetRules
{
    public HoloVCSLKGTarget(TargetInfo Target) : base(Target)
    {
        bOverrideBuildEnvironment = true;
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7; //UE 5.8 wants V7, keeps the "Target Upgrade Required" popup away
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        //the following arguements should only be set for the windows build
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            AdditionalCompilerArguments += "/wd4702 "; //ignore the warning about unused code, I use early return; during debugging, I don't want this warning
            AdditionalCompilerArguments += "/D _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS=1 ";
            AdditionalCompilerArguments += "/D _CRT_SECURE_NO_WARNINGS=1 ";
            AdditionalCompilerArguments += "/D MSVC=1 ";
            AdditionalCompilerArguments += "/D _M_IX86_FP=2 ";
        }

        ExtraModuleNames.AddRange(new string[] { "HoloVCS" });
    }
}
