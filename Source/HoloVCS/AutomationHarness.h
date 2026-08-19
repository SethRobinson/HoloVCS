//  ***************************************************************
//  AutomationHarness - file-based control channel for scripts and agents
//  -------------------------------------------------------------
//  Lets a script or tool drive the running game WITHOUT window focus:
//  it drops single-line commands into <ProjectSavedDir>/Automation/commands.txt and the
//  game executes them each tick, appending results to <ProjectSavedDir>/Automation/ai_log.txt.
//  Screenshots use the engine's own capture (FScreenshotRequest), so they work
//  while the window is behind other apps.  See AGENTS.md for the command list.
//  ***************************************************************

#pragma once
#include "Shared/UnrealMisc.h"

class LibretroManager;

class AutomationHarness
{
public:

	void Init(LibretroManager* pManager);
	void Update(); //call once per game tick, before any early-outs (works while paused)

private:

	void ProcessCommandLine(const FString& line);
	void AppendToLog(const FString& msg);
	bool ButtonIdFromName(const FString& name, int& idOut);

	LibretroManager* m_pManager = NULL;
	FString m_autoDir;
	FString m_commandFile;
	FString m_logFile;

	//video capture state (per-frame engine screenshots)
	int m_videoFramesLeft = 0;
	int m_videoFrameIndex = 0;
	FString m_videoDir;
};
