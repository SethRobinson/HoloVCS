// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class HoloVCSEditorTarget : TargetRules
{
	public HoloVCSEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
        //bUseLoggingInShipping = true;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        DefaultBuildSettings = BuildSettingsVersion.V7; //UE 5.8 wants V7, keeps the "Target Upgrade Required" popup away

        ExtraModuleNames.AddRange( new string[] { "HoloVCS" } );
        bOverrideBuildEnvironment = true;

        //the following arguements should only be set for the windows build
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {

            AdditionalCompilerArguments += "/wd4702 "; //ignore the warning about unused code, I use early return; during debugging, I don't want this warning
            AdditionalCompilerArguments += "/D _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS=1 ";
            AdditionalCompilerArguments += "/D _CRT_SECURE_NO_WARNINGS=1 ";
            AdditionalCompilerArguments += "/D MSVC=1 ";
            AdditionalCompilerArguments += "/D _M_IX86_FP=2 ";
        }
    }
}
