#include "Render/LookingGlassViewportClient.h"

#include "Render/LookingGlassRendering.h"
#include "Game/LookingGlassCapture.h"
#include "Misc/LookingGlassLog.h"
#include "Misc/LookingGlassStats.h"
#include "Misc/LookingGlassHelpers.h"
#include "ILookingGlassRuntime.h"
#include "Game/LookingGlassSceneCaptureComponent2D.h"

#include "CanvasTypes.h"
#include "ClearQuad.h"

#include "CanvasItem.h"
#include "ImageUtils.h"

#include "Runtime/Launch/Resources/Version.h"

#include "Misc/FileHelper.h"
#include "GameFramework/PlayerController.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
#include "GameFramework/PlayerInput.h"
#endif
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "ScreenRendering.h"
#include "RendererInterface.h"
#include "Modules/ModuleManager.h"
#include "Engine/Console.h"

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "LevelEditor.h"
#include "EditorSupportDelegates.h"
#endif // WITH_EDITOR

#if ENGINE_MAJOR_VERSION < 5 || ENGINE_MINOR_VERSION < 1
#include "ImageWriteTask.h"
#include "IImageWrapperModule.h"
#endif

#include "RHIStaticStates.h"
#include "CommonRenderResources.h" // for GFilterVertexDeclaration

#include "LookingGlassBridge.h"



static FName LevelEditorModuleName(TEXT("LevelEditor"));

FOnLookingGlassFrameReady FLookingGlassViewportClient::OnLookingGlassFrameReady;


void FLookingGlassScreenshotRequest::RequestScreenshot(const FString & InFilename, bool bAddFilenameSuffix, FLookingGlassScreenshotRequest::FQuiltSettings InQuiltSettings)
{
	FString GeneratedFilename = InFilename;
	CreateViewportScreenShotFilename(GeneratedFilename);
	QuiltSettings = InQuiltSettings;
	FString Extension = "png";
	const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
	if (LookingGlassSettings->LookingGlassScreenshotQuiltSettings.UseJPG)
	{
		Extension = "jpg";
	}

	// Compose the screenshot's filename
	if (bAddFilenameSuffix)
	{
		const bool bRemovePath = false;
		GeneratedFilename = FPaths::GetBaseFilename(GeneratedFilename, bRemovePath);
		if (QuiltSettings.NumColumns > 0)
		{
			// Make a filename suffix in a way similar to what Unity plugin does
			FString Suffix = FString::Printf(TEXT("_qs%dx%da%.2f"), QuiltSettings.NumColumns, QuiltSettings.NumRows, QuiltSettings.Aspect);
			FString GoodFilename;

			static int32 LastScreenshotIndex = -1;

			// We're inserting numeric suffix before the quilt settings, so let's use custom version of the GenerateNextBitmapFilename()
			for (int32 Index = LastScreenshotIndex + 1; Index < 10000; Index++)
			{
				GoodFilename = FString::Printf(TEXT("%s%05i%s.%s"), *GeneratedFilename, Index, *Suffix, *Extension );
				if (IFileManager::Get().FileExists(*GoodFilename) == false)
				{
					LastScreenshotIndex = Index;
					break;
				}
			}
			Filename = GoodFilename;
		}
		else
		{
			// This will add numeric suffix to the file name
			FFileHelper::GenerateNextBitmapFilename(GeneratedFilename, Extension, Filename);
		}
	}
	else
	{
		// Use exact provided file name, just add folder and extension
		Filename = GeneratedFilename;
		if (FPaths::GetExtension(Filename).Len() == 0)
		{
			Filename += TEXT(".png");
		}
	}
}

void FLookingGlassScreenshotRequest::CreateViewportScreenShotFilename(FString& InOutFilename)
{
	FString TypeName;

	TypeName = InOutFilename.IsEmpty() ? TEXT("Screenshot") : InOutFilename;
	check(!TypeName.IsEmpty());

	//default to using the path that is given
	InOutFilename = TypeName;
	if (!TypeName.Contains(TEXT("/")))
	{
		InOutFilename = GetDefault<UEngine>()->GameScreenshotSaveDirectory.Path / TypeName;
	}
}

FLookingGlassViewportClient::FLookingGlassViewportClient()
	: bIgnoreInput(false)
	, CurrentMouseCursor(EMouseCursor::Default)
	, StaticQuiltRT(nullptr)
	, LastRenderedComponent(nullptr)
	, LastViewportUpdateTime(0)
	, bLastModeWas2D(false)
	, Viewport(nullptr)
{
#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded(LevelEditorModuleName))
	{
		// Need to capture 2 "redraw viewport" delegates. The first one reacts on property changes, the second one - to moving objects in level editor viewport.
		FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorModuleName);
		LevelEditor.OnRedrawLevelEditingViewports().AddRaw(this, &FLookingGlassViewportClient::OnRedrawViewport);
		FEditorSupportDelegates::RedrawAllViewports.AddRaw(this, &FLookingGlassViewportClient::OnRedrawAllViewports);
	}
#endif
}

FLookingGlassViewportClient::~FLookingGlassViewportClient()
{
#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded(LevelEditorModuleName))
	{
		FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorModuleName);
		LevelEditor.OnRedrawLevelEditingViewports().RemoveAll(this);
		FEditorSupportDelegates::RedrawAllViewports.RemoveAll(this);
	}
#endif
	// Stop rendering, hide window
	FLookingGlassBridge& Bridge = ILookingGlassRuntime::Get().GetBridge();
	if (Bridge.bInitialized)
	{
		Bridge.StopRendering();
	}
}

#if WITH_EDITOR
void FLookingGlassViewportClient::OnRedrawAllViewports()
{
	LastViewportUpdateTime = FPlatformTime::Seconds();
}

void FLookingGlassViewportClient::OnRedrawViewport(bool bInvalidateHitProxies)
{
	LastViewportUpdateTime = FPlatformTime::Seconds();
}
#endif // WITH_EDITOR

// A helper function which is copying one texture (render targer) to another one.
// Taken from FGameplayMediaEncoder::CopyTexture() (exact copy of the code).
static void CopyTexture(const FTextureRHIRef& SourceTexture, FTextureRHIRef& DestinationTexture)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (SourceTexture->GetFormat() == DestinationTexture->GetFormat() && SourceTexture->GetSizeXY() == DestinationTexture->GetSizeXY())
	{
		TransitionAndCopyTexture(RHICmdList, SourceTexture, DestinationTexture, {});
	}
	else // Texture format mismatch, use a shader to do the copy.
	{
		IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

		// #todo-renderpasses there's no explicit resolve here? Do we need one?
		FRHIRenderPassInfo RPInfo(DestinationTexture, ERenderTargetActions::Load_Store);

		RHICmdList.Transition(FRHITransitionInfo(DestinationTexture, ERHIAccess::Unknown, ERHIAccess::RTV));
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyBackbuffer"));

		{
			RHICmdList.SetViewport(0, 0, 0.0f, (float)DestinationTexture->GetSizeX(), (float)DestinationTexture->GetSizeY(), 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			// New engine version...
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			const bool bSameSize = (DestinationTexture->GetDesc().Extent == SourceTexture->GetDesc().Extent);
			FRHISamplerState* PixelSampler = bSameSize ? TStaticSamplerState<SF_Point>::GetRHI() : TStaticSamplerState<SF_Bilinear>::GetRHI();

			SetShaderParametersLegacyPS(RHICmdList, PixelShader, PixelSampler, SourceTexture);

			RendererModule->DrawRectangle(RHICmdList, 0, 0,                // Dest X, Y
				(float)DestinationTexture->GetSizeX(),  // Dest Width
				(float)DestinationTexture->GetSizeY(),  // Dest Height
				0, 0,                            // Source U, V
				1, 1,                            // Source USize, VSize
				DestinationTexture->GetSizeXY(), // Target buffer size
				FIntPoint(1, 1),                 // Source texture size
				VertexShader, EDRF_Default);
		}

		RHICmdList.EndRenderPass();
		RHICmdList.Transition(FRHITransitionInfo(DestinationTexture, ERHIAccess::RTV, ERHIAccess::SRVMask));
	}
}

void FLookingGlassViewportClient::Draw(FViewport* InViewport, FCanvas* InCanvas)
{
	check(IsInGameThread());

	SCOPE_CYCLE_COUNTER(STAT_Draw_GameThread);

	const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
	const FLookingGlassRenderingSettings& RenderingSettings = LookingGlassSettings->LookingGlassRenderingSettings;
	TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> LookingGlassCaptureComponent = LookingGlass::GetGameLookingGlassCaptureComponent();

	// Find if we're recording the video, to override rendering mode
	bool bIsRecordingMovie = (LookingGlass::GetMovieSceneCapture() != nullptr) || OnLookingGlassFrameReady.IsBound();
#if WITH_EDITOR
	// Force realtime when sequencer is open. Actually, should update viewport only when time was changed (ISequencer::OnGlobalTimeChanged event).
	bool bIsSequencerOpen = ILookingGlassRuntime::Get().HasActiveSequencers();
#endif

	// Clear entire canvas
	InCanvas->Clear(FLinearColor::Black);

	if (!LookingGlassCaptureComponent.IsValid())
	{
		InCanvas->Clear(FLinearColor::Blue);
		return;
	}

	// Create render QuiltRT if not exists
	bool bRenderOnDevice = ILookingGlassRuntime::Get().IsRenderingOnDevice();
	FLookingGlassBridge& Bridge = ILookingGlassRuntime::Get().GetBridge();
	if (!Bridge.bInitialized)
	{
		bRenderOnDevice = false;
	}

	UTextureRenderTarget2D* QuiltRT = GetQuiltRT(LookingGlassCaptureComponent);

	if (LookingGlassCaptureComponent->GetRenderingConfigs().Configs.Num() == 0)
	{
		ensureMsgf(false, TEXT("There is no rendering configs"));
		InCanvas->Clear(FLinearColor::Green);
		return;
	}

	bool bShow2D = RenderingSettings.bRender2D;
	ELookingGlassPerformanceMode PerfMode = ELookingGlassPerformanceMode::Realtime;

	bool bPendingQuiltScreenshot = LookingGlassQuiltScreenshotRequest.IsValid();
	if (bPendingQuiltScreenshot || bIsRecordingMovie)
	{
		// When hologram screenshot is pending, disable 2D mode
		bShow2D = false;
	}

#if WITH_EDITOR
	const FLookingGlassEditorSettings& EditorSettings = LookingGlassSettings->LookingGlassEditorSettings;
	PerfMode = EditorSettings.PerformanceMode;

	if (PerfMode == ELookingGlassPerformanceMode::RealtimeAdaptive)
	{
		// Recognize if user is changing anything in the scene. Render a simplified view if yes.
//		bShow2D = FSlateApplication::Get().HasAnyMouseCaptor() || FSlateApplication::Get().IsDragDropping() || GUnrealEd->IsUserInteracting();
		// GUnrealEd is null when an editor binary runs in -game mode
		if (GUnrealEd != nullptr)
		{
			bShow2D |= GUnrealEd->IsUserInteracting();
		}
	}
#endif // WITH_EDITOR

	// Process Screenshot 2Ds before offset Tiling scene capture
	ProcessScreenshot2D(LookingGlassCaptureComponent);

	// If we render in 2D mode, just render one full view and return. Note: we do not use 'bShouldRender' logic
	// here, because we don't have a cached RenderTarget to preserve previously rendered image between frames,
	// as we're using for quilt renders. In a case we won't draw anything, the picture on the screen will be black.
	if (bShow2D)
	{
		// Render a single picture to render target
		LookingGlassCaptureComponent->Render2DView();

		FTextureRenderTargetResource* RenderTarget = LookingGlassCaptureComponent->GetTextureTarget2DRendering()->GameThread_GetRenderTargetResource();
		if (bRenderOnDevice)
		{
			// Copy render target to QuiltRT, as it has compatible with Bridge texture format
			FTextureRenderTargetResource* QuiltRenderTarget = QuiltRT->GameThread_GetRenderTargetResource();
			ENQUEUE_RENDER_COMMAND(Render2DToDevice)(
				[RenderTarget, QuiltRenderTarget](FRHICommandListImmediate& RHICmdList)
				{
					FTextureRHIRef TargetRT = QuiltRenderTarget->GetRenderTargetTexture();
					CopyTexture(RenderTarget->GetRenderTargetTexture(), TargetRT);
				}
			);
			// Now, visualize the QuiltRT on device
			VisualizeRenderTarget(InViewport, QuiltRT, true, FIntPoint(1, 1), LookingGlassCaptureComponent->GetAspectRatio());
		}
		else
		{
			// Copy rendered picture to viewport
			FTextureRHIRef ViewportRT = InViewport->GetRenderTargetTexture();
			ENQUEUE_RENDER_COMMAND(Render2DToViewport)(
				[RenderTarget, InViewport](FRHICommandListImmediate& RHICmdList)
				{
					FTextureRHIRef ViewportRT = InViewport->GetRenderTargetTexture();
					CopyTexture(RenderTarget->GetRenderTargetTexture(), ViewportRT);
				}
			);
		}

		bLastModeWas2D = true;
		return;
	}

#if WITH_EDITOR
	// Logic for realtime/non-realtime rendering
	bool bShouldRender = false;
	if (LookingGlassCaptureComponent->GetOverrideQuiltTexture2D() != nullptr)
	{
		// We won't display the actual picture, as there's an override - don't render anything
		bShouldRender = false;
	}
	else if (PerfMode == ELookingGlassPerformanceMode::Realtime || PerfMode == ELookingGlassPerformanceMode::RealtimeAdaptive ||
		bIsRecordingMovie || bIsSequencerOpen || bPendingQuiltScreenshot)
	{
		// Forced realtime mode, always render
		bShouldRender = true;
	}
	else if (PerfMode == ELookingGlassPerformanceMode::NonRealtime)
	{
		// Always re-render the hologram in non-realtime mode when:
		// - not in editor
		// - viewport has been just created - ensured by setting LastRenderedComponent to null in constructor
		// - rendering component has been switched to another one (LastRenderedComponent)
		// - recording a movie
		// - when rendering mode (actually, only 2D -> non-2D) changed (bLastModeWas2D)
		if (bLastModeWas2D)
		{
			// Switching from 2D to hologram: we should re-render Quilt when in non-realtime mode
			bShouldRender = true;
			bLastModeWas2D = false;
		}
		if (LookingGlassCaptureComponent.Get() != LastRenderedComponent)
		{
			// Capture component has been changed. This will also happen when rendering a very first frame
			bShouldRender = true;
			LastRenderedComponent = LookingGlassCaptureComponent.Get();
		}
		if (LastViewportUpdateTime > 0 && (FPlatformTime::Seconds() > LastViewportUpdateTime + EditorSettings.NonRealtimeUpdateDelay))
		{
			// Enough time passed since the last editor viewport update, i.e. no scene updates has been made, so - redraw the hologram
			bShouldRender = true;
			// Indicate that no update is required on the next frame (unless something will be changed)
			LastViewportUpdateTime = -1;
		}
	}
#else
	const bool bShouldRender = true;
#endif // WITH_EDITOR

	// Render scene to quilt. Update only when bShouldRender is true. If it is false, then previously rendered picture will be reused.
	if (bShouldRender)
	{
		// Render the actual scene to quilt texture
		RenderToQuilt(LookingGlassCaptureComponent.Get(), QuiltRT);
	}

	// Synchronize game and rendering thread
	FlushRenderingCommands();

	// Pass composed quilt to target: either device or debug window
	FIntPoint Tiles(1, 1);
	if (!RenderingSettings.QuiltMode && !bShow2D)
	{
		// Show quilt on device by disabling any tiling
		const FLookingGlassTilingQuality& TilingValues = LookingGlassCaptureComponent->GetTilingValues();
		Tiles.X = TilingValues.TilesX;
		Tiles.Y = TilingValues.TilesY;
	}
	VisualizeRenderTarget(InViewport, QuiltRT, bRenderOnDevice, Tiles, LookingGlassCaptureComponent->GetAspectRatio());

	if (OnLookingGlassFrameReady.IsBound())
	{
		ProcessQuiltForMovie(QuiltRT);
	}
	else
	{
		ProcessScreenshotQuilt(QuiltRT);
	}
}

void FLookingGlassViewportClient::VisualizeRenderTarget(FViewport* InViewport, UTextureRenderTarget2D* QuiltRT, bool bRenderOnDevice, const FIntPoint& Tiles, float Aspect)
{
	// Pass composed quilt to target: either device or debug window
	FTextureRenderTargetResource* RenderTarget = QuiltRT->GameThread_GetRenderTargetResource();
	if (!RenderTarget->GetTexture2DRHI())
	{
		return;
	}

	if (bRenderOnDevice)
	{
		// Prepare Bridge if needed
		FLookingGlassBridge& Bridge = ILookingGlassRuntime::Get().GetBridge();
#if 1
		void* RTNativeHandle = RenderTarget->GetTexture2DRHI()->GetNativeResource();
		if (!Bridge.IsRendering())
		{
			Bridge.StartRendering();
		}
		// Then render
		Bridge.DrawTexture(RTNativeHandle, Tiles.X, Tiles.Y, Aspect);
#else
		// Do the sync with device in rendering thread. For some reason, at least with Bridge 2.4.9 it hangs
		// in Bridge API.
		if (!Bridge.IsRendering())
		{
			Bridge.StartRendering();
		}
		// Then render
		ENQUEUE_RENDER_COMMAND(CopyQuiltRTToBridge)(
			[&Bridge, RenderTarget, Tiles](FRHICommandListImmediate& RHICmdList)
			{
				void* RTNativeHandle = RenderTarget->GetTexture2DRHI()->GetNativeResource();
				Bridge.DrawTexture(RTNativeHandle, Tiles.X, Tiles.Y, Aspect);
			}
		);
#endif
	}
	else
	{
		// Copy QuiltRT to viewport. Can't render things directly there, because of some texture type
		// incompatibilities - the viewport's RT is not URenderTarget or any other types used here.
		FTextureRHIRef ViewportRT = InViewport->GetRenderTargetTexture();

		ENQUEUE_RENDER_COMMAND(CopyQuiltRTToViewport)(
			[RenderTarget, InViewport](FRHICommandListImmediate& RHICmdList)
			{
				FTextureRHIRef ViewportRT = InViewport->GetRenderTargetTexture();
				CopyTexture(RenderTarget->GetRenderTargetTexture(), ViewportRT);
			}
		);
	}
}

void FLookingGlassViewportClient::RenderToQuilt(ULookingGlassSceneCaptureComponent2D* CaptureComponent, UTextureRenderTarget2D* InQuiltRT)
{
	// Render to multiple render targets
	CaptureComponent->RenderViews();

	// Copy data from multiple render targets into a single quilt image
	uint32 CurrentViewIndex = 0;
	for (const FLookingGlassRenderingConfig& RenderingConfig : CaptureComponent->GetRenderingConfigs().Configs)
	{
		UTextureRenderTarget2D* RenderTarget = RenderingConfig.GetRenderTarget();
		if (RenderTarget == nullptr || RenderTarget->GetResource() == nullptr)
		{
			UE_LOG(LookingGlassLogRender, Error, TEXT("RenderTarget is null"));

			return;
		}

		for (int32 ViewIndex = 0; ViewIndex < RenderingConfig.GetViewInfoArr().Num(); ++ViewIndex)
		{
			LookingGlass::FCopyToQuiltRenderContext RenderContext =
			{
				InQuiltRT->GameThread_GetRenderTargetResource(),
				CaptureComponent->GetTilingValues(),
				RenderTarget->GetResource(),
				CurrentViewIndex,
				ViewIndex,
				RenderingConfig.GetViewInfoArr().Num(),
				RenderingConfig.GetViewRows(),
				RenderingConfig.GetViewColumns(),
				RenderingConfig.GetViewInfoArr()[ViewIndex]
			};

			ENQUEUE_RENDER_COMMAND(CopyToQuiltCommand)(
				[RenderContext, CurrentViewIndex](FRHICommandListImmediate& RHICmdList)
				{
					SCOPE_CYCLE_COUNTER(STAT_CopyToQuiltShader_RenderThread);

					LookingGlass::CopyToQuiltShader_RenderThread(RHICmdList, RenderContext);
				});

			CurrentViewIndex++;
		}
	}
}

bool FLookingGlassViewportClient::InputKey(const FInputKeyEventArgs& EventArgs) 
{
	FViewport* InViewport = EventArgs.Viewport;
	int32 ControllerId = EventArgs.ControllerId;
	FKey Key = EventArgs.Key;
	EInputEvent EventType = EventArgs.Event;
	float AmountDepressed = EventArgs.AmountDepressed;
	bool bGamepad = EventArgs.IsGamepad();

	ILookingGlassRuntime::Get().OnLookingGlassInputKeyDelegate().Broadcast(InViewport, ControllerId, Key, EventType, AmountDepressed, bGamepad);

	auto LookingGlassSettings = GetDefault<ULookingGlassSettings>();

	// Process special input first
	if (Key == EKeys::Escape && EventType == EInputEvent::IE_Pressed)
	{
		ILookingGlassRuntime::Get().StopPlayer();
	}

	if (LookingGlassSettings->LookingGlassScreenshotQuiltSettings.InputKey == Key && EventType == EInputEvent::IE_Pressed)
	{
		PrepareScreenshotQuilt(LookingGlassSettings->LookingGlassScreenshotQuiltSettings.FileName, true);
	}

	if (LookingGlassSettings->LookingGlassScreenshot2DSettings.InputKey == Key && EventType == EInputEvent::IE_Pressed)
	{
		PrepareScreenshot2D(LookingGlassSettings->LookingGlassScreenshot2DSettings.FileName, true);
	}

	if (IgnoreInput())
	{
		return false;
	}

	bool bResult = false;

	// Make sure we are playing in separate window
	if (ILookingGlassRuntime::Get().GetCurrentLookingGlassModeType() == ELookingGlassModeType::PlayMode_InSeparateWindow)
	{
		// Make sure we are in game play mode
		if (GEngine->GameViewport != nullptr)
		{
			ULocalPlayer* FirstLocalPlayerFromController = GEngine->GameViewport->GetWorld()->GetFirstLocalPlayerFromController();

			UE_LOG(LookingGlassLogInput, Verbose, TEXT(">> InputKey %s, FirstLocalPlayerFromController %p, ControllerId %d"), *Key.ToString(), FirstLocalPlayerFromController, ControllerId);

			if (FirstLocalPlayerFromController && FirstLocalPlayerFromController->PlayerController)
			{
				bResult = FirstLocalPlayerFromController->PlayerController->InputKey(EventArgs);
			}

			// A gameviewport is always considered to have responded to a mouse buttons to avoid throttling
			if (!bResult && Key.IsMouseButton())
			{
				bResult = true;
			}
		}
	}

	return bResult;
}

bool FLookingGlassViewportClient::InputAxis(const FInputKeyEventArgs& EventArgs)
{
	if (IgnoreInput())
	{
		return false;
	}
	if (GWorld == nullptr || GEngine == nullptr || GEngine->GameViewport == nullptr || &GEngine->GameViewport->Viewport == nullptr)
	{
		return false;
	}

	FViewport* InViewport = EventArgs.Viewport;
	int32 ControllerId = EventArgs.ControllerId;
	FKey Key = EventArgs.Key;
	float Delta = EventArgs.AmountDepressed;
	float DeltaTime = EventArgs.DeltaTime;
	int32 NumSamples = EventArgs.NumSamples;
	bool bGamepad = EventArgs.IsGamepad();
	bool bResult = false;

	// Don't allow mouse/joystick input axes while in PIE and the console has forced the cursor to be visible
	if (!(GEngine->GameViewport->Viewport->IsSlateViewport() && GEngine->GameViewport->Viewport->IsPlayInEditorViewport()) ||
		GEngine->GameViewport->ViewportConsole == NULL || !GEngine->GameViewport->ViewportConsole->ConsoleActive())
	{
		FInputDeviceId DeviceId = FInputDeviceId::CreateFromInternalId(ControllerId);

		// route to subsystems that care
		if (GEngine->GameViewport->ViewportConsole != NULL)
		{
			bResult = GEngine->GameViewport->ViewportConsole->InputAxis(DeviceId, Key, Delta, DeltaTime, NumSamples, bGamepad);
		}

		if (!bResult)
		{
			ULocalPlayer* const TargetPlayer = GEngine->GetLocalPlayerFromControllerId(GEngine->GameViewport, ControllerId);
			if (TargetPlayer && TargetPlayer->PlayerController)
			{
				FInputKeyEventArgs AxisEventArgs(
					EventArgs.Viewport,
					EventArgs.InputDevice,
					EventArgs.Key,
					Delta,
					DeltaTime,
					NumSamples,
					EventArgs.EventTimestamp
				);

				bResult = TargetPlayer->PlayerController->InputKey(AxisEventArgs);
			}
		}
	}

	return bResult;
}

bool FLookingGlassViewportClient::InputChar(FViewport * InViewport, int32 ControllerId, TCHAR Character)
{
	return false;
}

void FLookingGlassViewportClient::RedrawRequested(FViewport * InViewport)
{
	Viewport->Draw();
}

bool FLookingGlassViewportClient::GetRenderTargetScreenShot(TWeakObjectPtr<UTextureRenderTarget2D> TextureRenderTarget2D, TArray<FColor>& Bitmap, const FIntRect& ViewRect)
{
	// Read the contents of the viewport into an array.
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false); // This is super important to disable this!

	bool bIsSuccess = false;
	FTextureRenderTargetResource* RenderTarget = TextureRenderTarget2D->GameThread_GetRenderTargetResource();
	if (RenderTarget->ReadPixels(Bitmap, ReadSurfaceDataFlags, ViewRect))
	{
		check(Bitmap.Num() == ViewRect.Area() || (Bitmap.Num() == TextureRenderTarget2D->SizeX * TextureRenderTarget2D->SizeY));
		for (FColor& color : Bitmap)
		{
			color.A = 255;
		}

		bIsSuccess = true;
	}

	return bIsSuccess;
}

//todo: unused function
static void ClipScreenshot(FIntVector& Size, FIntRect& SourceRect, TArray<FColor>& Bitmap)
{
	// Clip the bitmap to just the capture region if valid
	if (!SourceRect.IsEmpty())
	{
		FColor* const Data = Bitmap.GetData();
		const int32 OldWidth = Size.X;
		const int32 OldHeight = Size.Y;
		const int32 NewWidth = SourceRect.Width();
		const int32 NewHeight = SourceRect.Height();
		const int32 CaptureTopRow = SourceRect.Min.Y;
		const int32 CaptureLeftColumn = SourceRect.Min.X;

		for (int32 Row = 0; Row < NewHeight; Row++)
		{
			FMemory::Memmove(Data + Row * NewWidth, Data + (Row + CaptureTopRow) * OldWidth + CaptureLeftColumn, NewWidth * sizeof(*Data));
		}

		Bitmap.RemoveAt(NewWidth * NewHeight, OldWidth * OldHeight - NewWidth * NewHeight, EAllowShrinking::Yes);
		Size = FIntVector(NewWidth, NewHeight, 0);
	}
}

void FLookingGlassViewportClient::ProcessScreenshotQuilt(UTextureRenderTarget2D* InQuiltRT)
{
	if (LookingGlassQuiltScreenshotRequest.IsValid())
	{
		if (LookingGlassQuiltScreenshotRequest->GetFilename().IsEmpty())
		{
			LookingGlassQuiltScreenshotRequest.Reset();
			return;
		}

		TArray<FColor> Bitmap;
		bool bScreenshotSuccessful = GetRenderTargetScreenShot(InQuiltRT, Bitmap);
		if ( bScreenshotSuccessful )
		{
			FIntVector Size( InQuiltRT->SizeX, InQuiltRT->SizeY, 0 );
			const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
			SaveScreenShot(Bitmap, Size, LookingGlassQuiltScreenshotRequest->GetFilename(), &LookingGlassSettings->LookingGlassScreenshotQuiltSettings);
		}

		// Notify about completion
		LookingGlassQuiltScreenshotRequest->ExecCallback();

		LookingGlassQuiltScreenshotRequest.Reset();
		OnScreenshotQuiltRequestProcessed().Broadcast();
	}
}

void FLookingGlassViewportClient::ProcessQuiltForMovie(UTextureRenderTarget2D* InQuiltRT)
{
	if (OnLookingGlassFrameReady.IsBound())
	{
		TArray<FColor> Bitmap;
		bool bScreenshotSuccessful = GetRenderTargetScreenShot(InQuiltRT, Bitmap);
		if (bScreenshotSuccessful)
		{
			OnLookingGlassFrameReady.Broadcast(Bitmap, InQuiltRT->SizeX, InQuiltRT->SizeY);
		}
	}
}

void FLookingGlassViewportClient::ProcessScreenshot2D(TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> LookingGlassCaptureComponent)
{
	if (LookingGlassScreenshot2DRequest.IsValid())
	{
		FString ScreenShotName = LookingGlassScreenshot2DRequest->GetFilename();

		if (ScreenShotName.IsEmpty())
		{
			LookingGlassScreenshot2DRequest.Reset();
			return;
		}

		int32 ScreenshotResolutionX = GetDefault<ULookingGlassSettings>()->LookingGlassScreenshot2DSettings.Resolution.X;
		int32 ScreenshotResolutionY = GetDefault<ULookingGlassSettings>()->LookingGlassScreenshot2DSettings.Resolution.Y;

		if (ScreenshotResolutionX <= 0 || ScreenshotResolutionY <= 0)
		{
			return;
		}

		// Render picture
		LookingGlassCaptureComponent->Render2DView(ScreenshotResolutionX, ScreenshotResolutionY);
		// grab the render target where picture was rendered
		UTextureRenderTarget2D* RenderTarget = LookingGlassCaptureComponent->GetTextureTarget2DRendering();

		TArray<FColor> Bitmap;
		bool bScreenshotSuccessful = GetRenderTargetScreenShot(RenderTarget, Bitmap);

		if (bScreenshotSuccessful)
		{
			FIntVector Size( RenderTarget->SizeX, RenderTarget->SizeY, 0 );
			const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
			SaveScreenShot(Bitmap, Size, LookingGlassScreenshot2DRequest->GetFilename(), &LookingGlassSettings->LookingGlassScreenshot2DSettings);
		}

		LookingGlassScreenshot2DRequest.Reset();
		OnScreenshot2DRequestProcessed().Broadcast();
	}
}

void FLookingGlassViewportClient::SaveScreenShot(const TArray<FColor>& Bitmap, const FIntVector& Size, const FString& InScreenShotName, const FLookingGlassScreenshotSettings* pScreenShotSettings)
{
	FString Extension = TEXT(".png");
	int32 Quality = 0;
	bool bUseJpg = pScreenShotSettings->UseJPG;
	if (bUseJpg)
	{
		Extension = TEXT(".jpg");
		Quality = pScreenShotSettings->JpegQuality;
	}

	FString ScreenShotName = InScreenShotName;
	if (!FPaths::GetExtension( ScreenShotName ).IsEmpty())
	{
		ScreenShotName = FPaths::GetBaseFilename(ScreenShotName, false);
		ScreenShotName += Extension;
	}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
	FImageView ImageView(Bitmap.GetData(), Size.X, Size.Y);
	FImageUtils::SaveImageByExtension(*ScreenShotName, ImageView, Quality);
#else
	// Implement image saving directly
	IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (!ensure( ImageWrapperModule ))
	{
		UE_LOG( LookingGlassLogInput, Verbose, TEXT( "Could not find image wrapper module, Screenshot aborted" ) );
		return;
	}
	EImageFormat imageFormat = EImageFormat::PNG;
	if (bUseJpg)
	{
		imageFormat = EImageFormat::JPEG;
	}

	TSharedPtr<IImageWrapper> NewImageWrapper = ImageWrapperModule->CreateImageWrapper( imageFormat );
	if (!ensureMsgf( NewImageWrapper.IsValid(), TEXT( "Unable to create an image wrapper for the desired format." ) ))
	{
		UE_LOG( LookingGlassLogInput, Verbose, TEXT( "Unable to create an image wrapper for the desired format., Screenshot aborted" ) );
		return;
	}
	NewImageWrapper->SetRaw( Bitmap.GetData(), Size.X * Size.Y * 4, Size.X, Size.Y, ERGBFormat::BGRA, 8 );

	TArray64<uint8> CompressedBitmap = NewImageWrapper->GetCompressed(Quality);
	FFileHelper::SaveArrayToFile( CompressedBitmap, *ScreenShotName );
#endif
}

EMouseCursor::Type FLookingGlassViewportClient::GetCursor(FViewport* InViewport, int32 X, int32 Y)
{
	return CurrentMouseCursor;
}

bool FLookingGlassViewportClient::IsFocused(FViewport* InViewport)
{
	return InViewport->HasFocus() || InViewport->HasMouseCapture();
}

void FLookingGlassViewportClient::LostFocus(FViewport* InViewport)
{
	CurrentMouseCursor = EMouseCursor::Default;
}

void FLookingGlassViewportClient::ReceivedFocus(FViewport* InViewport)
{
	CurrentMouseCursor = EMouseCursor::None;
}

bool FLookingGlassViewportClient::Exec(UWorld * InWorld, const TCHAR * Cmd, FOutputDevice & Ar)
{
	if (FParse::Command(&Cmd, TEXT("LookingGlass.ScreenshotQuilt")))
	{
		return HandleScreenshotQuiltCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("LookingGlass.Screenshot2D")))
	{
		return HandleScreenshot2DCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("LookingGlass.Tilling")))
	{
		return HandleTillingCommand(Cmd, Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("LookingGlass.Rendering")))
	{
		return HandleRenderingCommand(Cmd, Ar);
	}
	else
	{
		return false;
	}
}

bool FLookingGlassViewportClient::HandleScreenshotQuiltCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	if (Viewport)
	{
		FString FileName;
		bool bAddFilenameSuffix = true;
		ParseScreenshotCommand(Cmd, FileName, bAddFilenameSuffix);

		return PrepareScreenshotQuilt(FileName, bAddFilenameSuffix);
	}
	return true;
}

bool FLookingGlassViewportClient::HandleScreenshot2DCommand(const TCHAR * Cmd, FOutputDevice & Ar)
{
	if (Viewport)
	{
		FString FileName;
		bool bAddFilenameSuffix = true;
		ParseScreenshotCommand(Cmd, FileName, bAddFilenameSuffix);

		return PrepareScreenshot2D(FileName, bAddFilenameSuffix);
	}
	return true;
}

bool FLookingGlassViewportClient::HandleTillingCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	ULookingGlassSceneCaptureComponent2D* GameLookingGlassCaptureComponent = LookingGlass::GetGameLookingGlassCaptureComponent().Get();

	if (GameLookingGlassCaptureComponent == nullptr)
	{
		UE_LOG(LookingGlassLogInput, Verbose, TEXT(">> LookingGlassCaptureComponent is not valid"));
		return false;
	}

	// todo: can request the enum member of ELookingGlassQualitySettings by name here, instead of parsing every single value individually
	ELookingGlassQualitySettings TilingSettings;
	if (FParse::Command(&Cmd, TEXT("Automatic")))
	{
		TilingSettings = ELookingGlassQualitySettings::Q_Automatic;
	}
    else if (FParse::Command(&Cmd, TEXT( "Portrait")))
    {
		TilingSettings = ELookingGlassQualitySettings::Q_Portrait;
    }
	else if (FParse::Command(&Cmd, TEXT("FourK")))
	{
		TilingSettings = ELookingGlassQualitySettings::Q_FourK;
	}
	else if (FParse::Command(&Cmd, TEXT("EightK")))
	{
		TilingSettings = ELookingGlassQualitySettings::Q_EightK;
	}
	else if (FParse::Command(&Cmd, TEXT("65inch")))
	{
		TilingSettings = ELookingGlassQualitySettings::Q_65_Inch;
	}
	else if (FParse::Command(&Cmd, TEXT("EightNineLegacy")))
	{
		TilingSettings = ELookingGlassQualitySettings::Q_EightPointNineLegacy;
	}
	else
	{
		UE_LOG(LookingGlassLogInput, Verbose, TEXT("Unknown tiling settings mode %s"), Cmd);
		return false;
	}

	GameLookingGlassCaptureComponent->SetTilingProperties(TilingSettings);

	ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
	LookingGlassSettings->LookingGlassSave();

	return true;
}

bool FLookingGlassViewportClient::HandleRenderingCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	bool bWasHandled = true;

	if (FParse::Command(&Cmd, TEXT("Render2D")))
	{
		if (FString(Cmd).IsNumeric())
		{
			int32 NewVal = FCString::Atoi(*FString(Cmd));
			ULookingGlassSettings* LookingGlassSettings = GetMutableDefault<ULookingGlassSettings>();
			LookingGlassSettings->LookingGlassRenderingSettings.bRender2D = !!NewVal;
			LookingGlassSettings->LookingGlassSave();
		}
	}
	else
	{
		bWasHandled = false;
	}

	return bWasHandled;
}

void FLookingGlassViewportClient::ParseScreenshotCommand(const TCHAR * Cmd, FString& InName, bool& InSuffix)
{
	FString CmdString(Cmd);
	TArray<FString> Args;
	CmdString.ParseIntoArray(Args, TEXT(" "));
	if (Args.Num() > 1)
	{
		InName = Args[0];
	}
	else
	{
		InName = CmdString;
	}

	if (FParse::Param(Cmd, TEXT("nosuffix")))
	{
		InSuffix = false;
	}
}

void FLookingGlassViewportClient::TakeQuiltScreenshot(FLookingGlassScreenshotRequest::FCallback Callback)
{
	const ULookingGlassSettings* LookingGlassSettings = GetDefault<ULookingGlassSettings>();
	PrepareScreenshotQuilt(LookingGlassSettings->LookingGlassScreenshotQuiltSettings.FileName, true, Callback);
}

bool FLookingGlassViewportClient::PrepareScreenshotQuilt(const FString& FileName, bool bAddFilenameSuffix, FLookingGlassScreenshotRequest::FCallback Callback)
{
	if (!LookingGlassQuiltScreenshotRequest.IsValid())
	{
		LookingGlassQuiltScreenshotRequest = MakeShareable(new FLookingGlassScreenshotRequest());
		FLookingGlassScreenshotRequest::FQuiltSettings QuiltSettings;

		if (bAddFilenameSuffix)
		{
			// Get the LookingGlass component to see its tiling settings
			TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> LookingGlassCaptureComponent = LookingGlass::GetGameLookingGlassCaptureComponent();
			if (LookingGlassCaptureComponent.IsValid())
			{
				const FLookingGlassTilingQuality& TilingValues = LookingGlassCaptureComponent->GetTilingValues();
				QuiltSettings.NumColumns = TilingValues.TilesX;
				QuiltSettings.NumRows = TilingValues.TilesY;
				QuiltSettings.Aspect = LookingGlassCaptureComponent->GetAspectRatio();
			}
		}
		LookingGlassQuiltScreenshotRequest->SetCompletedCallback(Callback);
		LookingGlassQuiltScreenshotRequest->RequestScreenshot(FileName, bAddFilenameSuffix, QuiltSettings);

		return true;
	}

	return false;
}

bool FLookingGlassViewportClient::PrepareScreenshot2D(const FString& FileName, bool bAddFilenameSuffix)
{
	if (!LookingGlassScreenshot2DRequest.IsValid())
	{
		LookingGlassScreenshot2DRequest = MakeShareable(new FLookingGlassScreenshotRequest());
		LookingGlassScreenshot2DRequest->RequestScreenshot(FileName, bAddFilenameSuffix);

		return true;
	}

	return false;
}

UTextureRenderTarget2D* FLookingGlassViewportClient::GetQuiltRT(TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> LookingGlassCaptureComponent)
{
	const FLookingGlassTilingQuality& TilingValues = LookingGlassCaptureComponent->GetTilingValues();

	if (StaticQuiltRT == nullptr)
	{
		StaticQuiltRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), UTextureRenderTarget2D::StaticClass());
		StaticQuiltRT->AddToRoot();

		StaticQuiltRT->ClearColor = FLinearColor::Red;
		// We should create a RT in particular pixel format, and make it shareable, in order to being able to use it in Bridge
		StaticQuiltRT->bGPUSharedFlag = true;
		StaticQuiltRT->InitCustomFormat(TilingValues.QuiltW, TilingValues.QuiltH, PF_A2B10G10R10, false);
		StaticQuiltRT->UpdateResource();
		StaticQuiltRT->UpdateResourceImmediate();
		FlushRenderingCommands();
	}

	// Resize Quilt texture
	if (TilingValues.QuiltW != StaticQuiltRT->SizeX ||
		TilingValues.QuiltH != StaticQuiltRT->SizeY)
	{
		StaticQuiltRT->ResizeTarget(TilingValues.QuiltW, TilingValues.QuiltH);
		StaticQuiltRT->UpdateResource();
		StaticQuiltRT->UpdateResourceImmediate();
		FlushRenderingCommands();
	}

	return StaticQuiltRT;
}
