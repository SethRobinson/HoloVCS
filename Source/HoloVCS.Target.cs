// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class HoloVCSTarget : TargetRules
{
    public HoloVCSTarget(TargetInfo Target) : base(Target)
    {
        bOverrideBuildEnvironment = true;
        //bUseLoggingInShipping = true;
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
        if (Target.Platform == UnrealTargetPlatform.Android)
        {
            AdditionalCompilerArguments += "-w"; //needed for android build
        }


        ExtraModuleNames.AddRange(new string[] { "HoloVCS" });

    }
}