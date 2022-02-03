// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class HoloVCSTarget : TargetRules
{
	public HoloVCSTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		ProjectDefinitions.Add("WINVER=0x0A00");
		ProjectDefinitions.Add("_WIN32_WINNT=0x0A00");

		ExtraModuleNames.AddRange( new string[] { "HoloVCS" } );

	}
}
