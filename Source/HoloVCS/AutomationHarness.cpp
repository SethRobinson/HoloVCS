#include "AutomationHarness.h"
#include "LibretroManager.h"
#include "LibretroManagerActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ImageUtils.h"

void AutomationHarness::Init(LibretroManager* pManager)
{
	m_pManager = pManager;

	m_autoDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation"));
	m_commandFile = m_autoDir / TEXT("commands.txt");
	m_logFile = m_autoDir / TEXT("ai_log.txt");

	IFileManager::Get().MakeDirectory(*m_autoDir, true);
	IFileManager::Get().Delete(*m_commandFile); //stale commands from a previous run must not fire

	AppendToLog(FString::Printf(TEXT("harness ready (pid %u)"), FPlatformProcess::GetCurrentProcessId()));
	LogMsg("AutomationHarness ready, watching %s", TCHAR_TO_UTF8(*m_commandFile));
}

void AutomationHarness::AppendToLog(const FString& msg)
{
	FString line = FDateTime::Now().ToString(TEXT("%H:%M:%S")) + TEXT(" ") + msg + TEXT("\n");
	FFileHelper::SaveStringToFile(line, *m_logFile, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

bool AutomationHarness::ButtonIdFromName(const FString& name, int& idOut)
{
	if (name == TEXT("up")) { idOut = RETRO_DEVICE_ID_JOYPAD_UP; return true; }
	if (name == TEXT("down")) { idOut = RETRO_DEVICE_ID_JOYPAD_DOWN; return true; }
	if (name == TEXT("left")) { idOut = RETRO_DEVICE_ID_JOYPAD_LEFT; return true; }
	if (name == TEXT("right")) { idOut = RETRO_DEVICE_ID_JOYPAD_RIGHT; return true; }
	if (name == TEXT("a")) { idOut = RETRO_DEVICE_ID_JOYPAD_A; return true; }
	if (name == TEXT("b")) { idOut = RETRO_DEVICE_ID_JOYPAD_B; return true; }
	if (name == TEXT("start")) { idOut = RETRO_DEVICE_ID_JOYPAD_START; return true; }
	if (name == TEXT("select")) { idOut = RETRO_DEVICE_ID_JOYPAD_SELECT; return true; }
	if (name == TEXT("l")) { idOut = RETRO_DEVICE_ID_JOYPAD_L; return true; }
	if (name == TEXT("r")) { idOut = RETRO_DEVICE_ID_JOYPAD_R; return true; }
	return false;
}

void AutomationHarness::Update()
{
	//per-frame video capture runs even while a command file isn't present
	if (m_videoFramesLeft > 0)
	{
		FString frameFile = m_videoDir / FString::Printf(TEXT("frame_%05d.png"), m_videoFrameIndex++);
		FScreenshotRequest::RequestScreenshot(frameFile, false, false);
		m_videoFramesLeft--;
		if (m_videoFramesLeft == 0)
		{
			AppendToLog(FString::Printf(TEXT("video done: %d frames in %s"), m_videoFrameIndex, *m_videoDir));
		}
	}

	if (!IFileManager::Get().FileExists(*m_commandFile)) return;

	FString contents;
	if (!FFileHelper::LoadFileToString(contents, *m_commandFile)) return; //writer may still hold it; retry next tick

	IFileManager::Get().Delete(*m_commandFile);

	TArray<FString> lines;
	contents.ParseIntoArrayLines(lines);

	for (FString& line : lines)
	{
		line.TrimStartAndEndInline();
		if (line.IsEmpty()) continue;
		ProcessCommandLine(line);
	}
}

void AutomationHarness::ProcessCommandLine(const FString& line)
{
	TArray<FString> parts;
	line.ParseIntoArrayWS(parts);
	if (parts.Num() == 0) return;

	FString cmd = parts[0].ToLower();

	if (cmd == TEXT("press") && parts.Num() >= 2)
	{
		//press <btn>[,btn2,...] [frames] - holds emulated joypad buttons for N visible frames (default 8)
		int frames = (parts.Num() >= 3) ? FCString::Atoi(*parts[2]) : 8;
		frames = FMath::Clamp(frames, 1, 3600);

		TArray<FString> buttons;
		parts[1].ToLower().ParseIntoArray(buttons, TEXT(","));

		FString held;
		for (const FString& b : buttons)
		{
			int id;
			if (ButtonIdFromName(b, id))
			{
				m_pManager->m_autoButtonHoldFrames[id] = frames;
				held += b + TEXT(" ");
			}
			else
			{
				AppendToLog(FString::Printf(TEXT("press: unknown button '%s'"), *b));
			}
		}
		AppendToLog(FString::Printf(TEXT("press %s%d frames"), *held, frames));
	}
	else if (cmd == TEXT("shot"))
	{
		FString name = (parts.Num() >= 2) ? parts[1] : TEXT("shot");
		FString file = m_autoDir / TEXT("shots") / (name + TEXT(".png"));
		IFileManager::Get().Delete(*file); //poll-for-file-exists is the completion signal, so clear old ones
		FScreenshotRequest::RequestScreenshot(file, false, false);
		AppendToLog(FString::Printf(TEXT("shot requested: %s"), *file));
	}
	else if (cmd == TEXT("video") && parts.Num() >= 2)
	{
		int frames = FMath::Clamp(FCString::Atoi(*parts[1]), 1, 3600);
		FString name = (parts.Num() >= 3) ? parts[2] : TEXT("video");
		m_videoDir = m_autoDir / TEXT("video") / name;
		IFileManager::Get().MakeDirectory(*m_videoDir, true);
		m_videoFramesLeft = frames;
		m_videoFrameIndex = 0;
		AppendToLog(FString::Printf(TEXT("video started: %d frames -> %s"), frames, *m_videoDir));
	}
	else if (cmd == TEXT("dumplayers"))
	{
		//writes each depth layer's CPU texture buffer as a PNG - ground truth for what each plane holds
		ALibretroManagerActor* pActor = m_pManager->m_pLibretroManagedActor;
		int count = pActor ? pActor->GetLayerCount() : 0;
		int written = 0;
		for (int i = 0; i < count; i++)
		{
			LayerInfo* pLayer = pActor->GetLayer(i);
			if (!pLayer || !pLayer->m_pTextData) continue;
			FImageView img(pLayer->m_pTextData, (int32)pLayer->m_texWidth, (int32)pLayer->m_texHeight, ERawImageFormat::BGRA8);
			FString file = m_autoDir / TEXT("layers") / FString::Printf(TEXT("layer_%d.png"), i);
			if (FImageUtils::SaveImageByExtension(*file, img)) written++;
		}
		AppendToLog(FString::Printf(TEXT("dumplayers: wrote %d of %d layers"), written, count));
	}
	else if (cmd == TEXT("dump"))
	{
		m_pManager->m_bNesDumpRequested = true;
		AppendToLog(TEXT("dump requested"));
	}
	else if (cmd == TEXT("pause"))
	{
		m_pManager->SetGamePaused(true);
		AppendToLog(TEXT("paused"));
	}
	else if (cmd == TEXT("unpause"))
	{
		m_pManager->SetGamePaused(false);
		AppendToLog(TEXT("unpaused"));
	}
	else if (cmd == TEXT("help"))
	{
		//help [on|off] - no arg toggles.  Same as the ? hotkey (pauses the game while up)
		if (parts.Num() >= 2 && parts[1] == TEXT("on")) m_pManager->m_helpScreen.Show();
		else if (parts.Num() >= 2 && parts[1] == TEXT("off")) m_pManager->m_helpScreen.Hide();
		else m_pManager->m_helpScreen.Toggle();
		AppendToLog(FString::Printf(TEXT("help now %s"), m_pManager->m_helpScreen.IsVisible() ? TEXT("on") : TEXT("off")));
	}
	else if (cmd == TEXT("savestate"))
	{
		m_pManager->SaveStateToFile();
		AppendToLog(TEXT("savestate done"));
	}
	else if (cmd == TEXT("loadstate"))
	{
		m_pManager->LoadStateFromFile();
		AppendToLog(TEXT("loadstate done"));
	}
	else if (cmd == TEXT("rom") && parts.Num() >= 2)
	{
		bool bOK = m_pManager->SwitchRomByPartialName(toString(parts[1]));
		AppendToLog(FString::Printf(TEXT("rom '%s': %s"), *parts[1], bOK ? TEXT("loaded") : TEXT("NOT FOUND")));
	}
	else if (cmd == TEXT("exec"))
	{
		//pass the rest of the line to the console (HighResShot, cvars, quit, etc)
		FString rest = line.Mid(5).TrimStartAndEnd();
		UWorld* pWorld = m_pManager->m_pLibretroManagedActor ? m_pManager->m_pLibretroManagedActor->GetWorld() : NULL;
		if (GEngine && !rest.IsEmpty())
		{
			GEngine->Exec(pWorld, *rest);
			AppendToLog(FString::Printf(TEXT("exec: %s"), *rest));
		}
	}
	else if (cmd == TEXT("quit"))
	{
		AppendToLog(TEXT("quitting"));
		if (GEngine)
		{
			GEngine->Exec(NULL, TEXT("quit"));
		}
	}
	else
	{
		AppendToLog(FString::Printf(TEXT("unknown command: %s"), *line));
	}
}
