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
#include "RHITransition.h"

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
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/MeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformStackWalk.h"

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
#include "GlobalRenderResources.h" // for GWhiteTexture

#include "LookingGlassBridge.h"
#include "Render/LookingGlassLenticularShader.h"

// Defined in LookingGlassRuntime.cpp (lkg.SelfRender cvar)
bool LookingGlassSelfRenderEnabled();

// Debug: 1 = show the raw quilt in the self-render window instead of the lenticular output
static TAutoConsoleVariable<int32> CVarLKGSelfRenderQuilt(
	TEXT("lkg.SelfRenderQuilt"), 0,
	TEXT("1 = self-render window shows the raw quilt (debug), 0 = lenticular device output"));

// Debug: output gamma for the self-render lenticular pass. The sprite quilt is written
// gamma-neutral (TargetGamma 1 on the RT), so no extra encode is wanted here.
static TAutoConsoleVariable<float> CVarLKGSelfRenderGamma(
	TEXT("lkg.SelfRenderGamma"), 1.0f,
	TEXT("Display gamma the self-render pass encodes with (1 = pass quilt values through untouched)"));



static FName LevelEditorModuleName(TEXT("LevelEditor"));

FOnLookingGlassFrameReady FLookingGlassViewportClient::OnLookingGlassFrameReady;

// The live viewport client, for the lkg.SaveQuilt console command. The plugin window's own
// exec routing (LookingGlass.ScreenshotQuilt) is unreachable from GEngine->Exec in separate
// window mode, and the F9 hotkey needs that window focused - a console command works from
// anywhere (including the game's automation harness).
static FLookingGlassViewportClient* GActiveLKGViewportClient = nullptr;


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
	GActiveLKGViewportClient = this;
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
	if (GActiveLKGViewportClient == this)
	{
		GActiveLKGViewportClient = nullptr;
	}
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
		Bridge.RequestStopRendering();
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

// Runs the lenticular ("swizzle") shader: quilt in, view-interleaved device image out.
// This is how the legacy HoloPlay plugin drove the device, entirely in-process - no
// Bridge call happens anywhere on this path.
static void RenderLenticular_RenderThread(FRHICommandListImmediate& RHICmdList,
	const FTextureRHIRef& QuiltTexture, FTextureRHIRef& DestinationTexture,
	const FLookingGlassLenticularPS::FParameters& InParams)
{
	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

	RHICmdList.Transition(FRHITransitionInfo(QuiltTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
	RHICmdList.Transition(FRHITransitionInfo(DestinationTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

	FRHIRenderPassInfo RPInfo(DestinationTexture, ERenderTargetActions::DontLoad_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("LKGLenticular"));

	RHICmdList.SetViewport(0, 0, 0.0f, (float)DestinationTexture->GetSizeX(), (float)DestinationTexture->GetSizeY(), 1.0f);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FLookingGlassLenticularPS> PixelShader(ShaderMap);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

	FLookingGlassLenticularPS::FParameters Params = InParams;
	Params.InputTexture = QuiltTexture;
	// MUST be wrap addressing: FlipYTexCoords negates the view coordinate, so texArr()
	// produces negative/overflowing UVs by design (the legacy plugin sampled with the render
	// target's default wrap sampler). Clamp turns most of the output black.
	Params.InputTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Params);

	// The legacy plugin's fullscreen quad flipped V (top=1, bottom=0); paired with FlipYTexCoords=1
	RendererModule->DrawRectangle(RHICmdList, 0, 0,
		(float)DestinationTexture->GetSizeX(),  // Dest Width
		(float)DestinationTexture->GetSizeY(),  // Dest Height
		0, 1,                                   // Source U, V
		1, -1,                                  // Source USize, VSize
		DestinationTexture->GetSizeXY(),        // Target buffer size
		FIntPoint(1, 1),                        // Source texture size
		VertexShader, EDRF_Default);

	RHICmdList.EndRenderPass();
	RHICmdList.Transition(FRHITransitionInfo(DestinationTexture, ERHIAccess::RTV, ERHIAccess::SRVMask));
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

	// TEMP PERF DIAGNOSTIC: time the three phases of the hologram frame, report once a second
	static double GPhaseAccum[3] = { 0, 0, 0 };
	static int32 GPhaseFrames = 0;
	static double GPhaseNextReport = 0;
	double PhaseT0 = FPlatformTime::Seconds();

	// Render scene to quilt. Update only when bShouldRender is true. If it is false, then previously rendered picture will be reused.
	if (bShouldRender)
	{
		// Try the fast sprite-quilt path first (see RenderSpriteQuilt); fall back to full scene captures
		if (!RenderSpriteQuilt(LookingGlassCaptureComponent.Get(), QuiltRT))
		{
			// Render the actual scene to quilt texture
			RenderToQuilt(LookingGlassCaptureComponent.Get(), QuiltRT);
		}
	}

	double PhaseT1 = FPlatformTime::Seconds();

	// Synchronize game and rendering thread
	FlushRenderingCommands();

	double PhaseT2 = FPlatformTime::Seconds();

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

	double PhaseT3 = FPlatformTime::Seconds();
	GPhaseAccum[0] += PhaseT1 - PhaseT0;
	GPhaseAccum[1] += PhaseT2 - PhaseT1;
	GPhaseAccum[2] += PhaseT3 - PhaseT2;
	GPhaseFrames++;
	if (PhaseT3 > GPhaseNextReport && GPhaseFrames > 0)
	{
		UE_LOG(LookingGlassLogRender, Display, TEXT("LKG frame phases (avg ms over %d frames): RenderToQuilt %.1f, Flush %.1f, Visualize %.1f"),
			GPhaseFrames, GPhaseAccum[0] * 1000.0 / GPhaseFrames, GPhaseAccum[1] * 1000.0 / GPhaseFrames, GPhaseAccum[2] * 1000.0 / GPhaseFrames);
		GPhaseAccum[0] = GPhaseAccum[1] = GPhaseAccum[2] = 0;
		GPhaseFrames = 0;
		GPhaseNextReport = PhaseT3 + 1.0;
	}

	if (OnLookingGlassFrameReady.IsBound())
	{
		ProcessQuiltForMovie(QuiltRT);
	}
	else
	{
		ProcessScreenshotQuilt(QuiltRT);
	}
}

static TAutoConsoleVariable<int32> CVarLKGSpriteQuilt(
	TEXT("lkg.SpriteQuilt"), 1,
	TEXT("1 = render the quilt directly from the show-only textured quads (fast path), 0 = full scene capture per view"));

// Save the next rendered quilt as a png in Saved/Screenshots (same pipeline as the F9 hotkey,
// but reachable from any exec source - the automation harness uses "exec lkg.SaveQuilt").
// The file gets the usual _qsCxRaA.AA suffix naming.
static FAutoConsoleCommand CCmdLKGSaveQuilt(
	TEXT("lkg.SaveQuilt"),
	TEXT("Save the next rendered quilt to Saved/Screenshots as a png."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (GActiveLKGViewportClient)
		{
			GActiveLKGViewportClient->TakeQuiltScreenshot();
			UE_LOG(LookingGlassLogRender, Display, TEXT("lkg.SaveQuilt: quilt screenshot queued"));
		}
		else
		{
			UE_LOG(LookingGlassLogRender, Warning, TEXT("lkg.SaveQuilt: no active LookingGlass viewport client"));
		}
	}));

static TAutoConsoleVariable<float> CVarLKGSpriteShadow(
	TEXT("lkg.SpriteShadow"), 0.65f,
	TEXT("Opacity of the black drop shadow each sprite layer casts on the layers behind it (0 = off)"));
static TAutoConsoleVariable<int32> CVarLKGSpriteShadowSoft(
	TEXT("lkg.SpriteShadowSoft"), 2,
	TEXT("Shadow softness: number of 2x box-filter halvings applied to the 512px caster union before it stamps (0 = hard pixel-exact silhouette, 2 = 128px mask, 3 = 64px)"));

static TAutoConsoleVariable<float> CVarLKGSpriteAmbient(
	TEXT("lkg.SpriteAmbient"), 0.7f,
	TEXT("Ambient floor for the sprite-quilt directional lighting (1 = fully unlit/no light influence)"));

static TAutoConsoleVariable<int32> CVarLKGShowFPS(
	TEXT("lkg.ShowFPS"), 1,
	TEXT("1 = draw the fps counter into the top-left of every quilt tile"));

static TAutoConsoleVariable<float> CVarLKGSpriteTilt(
	TEXT("lkg.SpriteTilt"), 8.0f,
	TEXT("Degrees the sprite-quilt camera is raised above the diorama (vertical off-axis shear, like the old build's slightly-looking-down framing). 0 = straight on"));

static TAutoConsoleVariable<float> CVarLKGSpriteDepthDim(
	TEXT("lkg.SpriteDepthDim"), 0.0f,
	TEXT("How much darker the deepest layer renders vs the nearest (0 = off, matching the old build - the projected shadows provide the depth cue now)"));

bool FLookingGlassViewportClient::RenderSpriteQuilt(ULookingGlassSceneCaptureComponent2D* CaptureComponent, UTextureRenderTarget2D* InQuiltRT)
{
	if (CVarLKGSpriteQuilt.GetValueOnGameThread() == 0)
	{
		return false;
	}
	if (CaptureComponent == nullptr || InQuiltRT == nullptr)
	{
		return false;
	}
	// Only meaningful when the game curated an explicit show-only list of quads
	if (CaptureComponent->PrimitiveRenderMode != ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
	{
		return false;
	}
	// The projection shortcut below assumes an unrotated capture (quads parallel to the focal plane)
	AActor* CaptureOwner = CaptureComponent->GetOwner();
	if (CaptureOwner == nullptr || !CaptureOwner->GetActorRotation().IsNearlyZero(2.0))
	{
		return false;
	}

	// Gather the layer quads: every visible primitive must be a mesh with a dynamic material
	// instance whose first texture parameter is the sprite texture, or we bail to the scene path
	struct FSpriteLayer
	{
		const FTexture* Tex = nullptr;
		FBox Box;
		ESimpleElementBlendMode Blend = SE_BLEND_Translucent;
		// Lit materials receive the directional light tint / depth dimming / shadows; unlit ones
		// draw raw. The game's 8 key swaps layer materials between lit and unlit variants, and
		// this is what makes that toggle work on the hologram too.
		bool bLit = true;
		// Per-layer shadow receiving, from the mesh flag the game sets per game profile
		// (m_layerSetupInfo -> bReceiveMobileCSMShadows) and the backdrop's material choice
		bool bReceiveShadows = true;
		// The "LayerBG" backdrop wall: its material is emissive (full-bright in the scene render
		// no matter the light), so it draws raw, colored only by the game's SetTintBG params
		bool bBackdrop = false;
		// Material color multiplier (the game's ColorTint/TintStrength on the backdrop MID)
		FLinearColor Color = FLinearColor::White;
		// UV rect of the texels that actually hold content (the game reports it via custom
		// primitive data); shadow stamps are bounded by it and empty layers cast nothing
		FVector4 ContentUV = FVector4(0, 0, 1, 1);
		// Per-layer shadow casting, from the mesh flag (the splash screen and other overlay
		// primitives get SetCastShadow(false) from the game)
		bool bCastShadows = true;
		// Diagnostics only
		FString Name;
	};
	TArray<FSpriteLayer> Layers;
	FString OverlayText;
	FString HelpText;

	for (const TObjectPtr<AActor>& ActorPtr : CaptureComponent->ShowOnlyActors)
	{
		AActor* Actor = ActorPtr.Get();
		if (Actor == nullptr || Actor->IsHidden())
		{
			continue;
		}
		for (UActorComponent* C : Actor->GetComponents())
		{
			if (UTextRenderComponent* TextComp = Cast<UTextRenderComponent>(C))
			{
				// The game's help screen travels as text on a "HelpScreen"-tagged actor (runtime
				// contract with HoloVCS, like the status text below); non-empty = help is up and
				// gets its own full panel draw, never the bottom status line
				if (Actor->ActorHasTag(TEXT("HelpScreen")))
				{
					HelpText = TextComp->Text.ToString();
					continue;
				}
				OverlayText = TextComp->Text.ToString();
				continue;
			}
			UMeshComponent* Mesh = Cast<UMeshComponent>(C);
			if (Mesh == nullptr || !Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame)
			{
				continue;
			}
			// Throttled: in a bail-every-frame config these would flood the log at 60Hz
			static double GNextBailLog = 0.0;
			UMaterialInterface* Mat = Mesh->GetMaterial(0);
			if (Mat == nullptr)
			{
				if (FPlatformTime::Seconds() > GNextBailLog)
				{
					GNextBailLog = FPlatformTime::Seconds() + 5.0;
					UE_LOG(LookingGlassLogRender, Warning, TEXT("SpriteQuilt bail: %s/%s has no material"),
						*Actor->GetName(), *Mesh->GetName());
				}
				return false;
			}
			// HoloVCS binds the emulator screen as the "Texture" parameter; try that name first,
			// then fall back to the first texture parameter that has a value
			UTexture* Tex = nullptr;
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat))
			{
				MID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Texture")), Tex);
				if (Tex == nullptr)
				{
					TArray<FMaterialParameterInfo> ParamInfos;
					TArray<FGuid> ParamIds;
					MID->GetAllTextureParameterInfo(ParamInfos, ParamIds);
					for (const FMaterialParameterInfo& Info : ParamInfos)
					{
						UTexture* Candidate = nullptr;
						if (MID->GetTextureParameterValue(Info, Candidate) && Candidate != nullptr)
						{
							Tex = Candidate;
							break;
						}
					}
				}
			}
			if (Tex == nullptr)
			{
				// Plain (non-dynamic) materials - the LayerBG backdrop wall picture is a stock
				// UMaterial set per game profile - take the first texture the material references.
				// (GetUsedTextures returns nothing at runtime here; the static reference list is
				// what the material actually keeps loaded.)
				for (const TObjectPtr<UObject>& Ref : Mat->GetReferencedTextures())
				{
					UTexture* Candidate = Cast<UTexture>(Ref.Get());
					if (Candidate != nullptr && Candidate->GetResource() != nullptr)
					{
						Tex = Candidate;
						break;
					}
				}
			}
			if (Tex == nullptr || Tex->GetResource() == nullptr)
			{
				if (FPlatformTime::Seconds() > GNextBailLog)
				{
					GNextBailLog = FPlatformTime::Seconds() + 5.0;
					UE_LOG(LookingGlassLogRender, Warning, TEXT("SpriteQuilt bail: no usable texture on %s/%s (mat %s)"),
						*Actor->GetName(), *Mesh->GetName(), *Mat->GetName());
				}
				return false;
			}
			FSpriteLayer Layer;
			Layer.Tex = Tex->GetResource();
			Layer.Box = Mesh->Bounds.GetBox();
			// Opaque materials (the backdrop picture) must not be alpha-blended: their textures
			// often carry no meaningful alpha channel and would vanish
			Layer.Blend = (Mat->GetBlendMode() == BLEND_Opaque) ? SE_BLEND_Opaque : SE_BLEND_Translucent;
			Layer.bLit = !Mat->GetShadingModels().HasShadingModel(MSM_Unlit);
			Layer.bReceiveShadows = Mesh->bReceiveMobileCSMShadows;
			Layer.bCastShadows = Mesh->CastShadow;
			Layer.bBackdrop = Actor->ActorHasTag(FName(TEXT("LayerBG")));
			const FCustomPrimitiveData& CPD = Mesh->GetCustomPrimitiveData();
			if (CPD.Data.Num() >= 4)
			{
				Layer.ContentUV = FVector4(CPD.Data[0], CPD.Data[1], CPD.Data[2], CPD.Data[3]);
			}
			Layer.Name = FString::Printf(TEXT("%s(%s matblend=%d castshadow=%d)"),
				*Actor->GetName(), *Mat->GetName(), (int32)Mat->GetBlendMode(), Mesh->CastShadow ? 1 : 0);
			if (Layer.bBackdrop)
			{
				// Two backdrop flavors, judged by the BASE material (the mesh often holds an
				// auto-named MID): the SetBGPic picture materials (castlevania_backdrop_Mat) are
				// emissive - full bright in the scene render - while the BGLayer family is a LIT
				// wall colored by SetTintBG's ColorTint/TintStrength (SMB sky blue, black to
				// disable). Its no-shadow variant means the game asked for a shadow-free wall.
				const FString BaseMatName = Mat->GetMaterial() ? Mat->GetMaterial()->GetName() : Mat->GetName();
				// SetBGPic pictures AND the NoShadow tint wall are unlit in the old build
				// (SMB's sky is a bright blue "lighting turned off" wall)
				Layer.bLit = !BaseMatName.Contains(TEXT("backdrop")) && !BaseMatName.Contains(TEXT("NoShadow"));
				if (UMaterialInstanceDynamic* BGMID = Cast<UMaterialInstanceDynamic>(Mat))
				{
					FLinearColor TintColor = FLinearColor::White;
					float TintStrength = 0.0f;
					if (BGMID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ColorTint")), TintColor) &&
						BGMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("TintStrength")), TintStrength))
					{
						// The BGLayer materials REPLACE their texture with the tint as strength
						// approaches 1 (SMB's wall is a solid sky blue) - at full strength draw
						// a solid color instead of tinting the material's (mostly black) texture
						const float S = FMath::Clamp(TintStrength, 0.0f, 1.0f);
						if (S >= 0.99f)
						{
							Layer.Tex = GWhiteTexture;
							Layer.Color = FLinearColor(TintColor.R, TintColor.G, TintColor.B, 1.0f);
						}
						else
						{
							Layer.Color = FLinearColor(
								FMath::Lerp(1.0f, TintColor.R, S),
								FMath::Lerp(1.0f, TintColor.G, S),
								FMath::Lerp(1.0f, TintColor.B, S), 1.0f);
						}
					}
				}
				if (BaseMatName.Contains(TEXT("NoShadow")))
				{
					Layer.bReceiveShadows = false;
				}
			}
			Layers.Add(Layer);
		}
	}
	if (Layers.Num() == 0)
	{
		return false;
	}

	// Painter's algorithm: camera looks down +X, so farthest (largest X) draws first
	Layers.Sort([](const FSpriteLayer& A, const FSpriteLayer& B) { return A.Box.GetCenter().X > B.Box.GetCenter().X; });

	// One-shot diagnostic dump whenever the layer set changes shape
	static uint32 GLastLayerDumpHash = 0;
	uint32 LayerDumpHash = (uint32)Layers.Num();
	for (const FSpriteLayer& L : Layers)
	{
		LayerDumpHash = LayerDumpHash * 31 + (L.Tex ? (uint32)L.Tex->GetSizeX() : 0);
		LayerDumpHash = LayerDumpHash * 31 + (uint32)(int32)L.Box.GetCenter().X;
	}
	if (LayerDumpHash != GLastLayerDumpHash)
	{
		GLastLayerDumpHash = LayerDumpHash;
		for (int32 i = 0; i < Layers.Num(); i++)
		{
			const FSpriteLayer& L = Layers[i];
			const FVector C = L.Box.GetCenter();
			const FVector S = L.Box.GetSize();
			UE_LOG(LookingGlassLogRender, Display, TEXT("Sprite layer %d: %s X=%.0f center %.0f,%.0f size %.0fx%.0f tex=%dx%d lit=%d recv=%d bg=%d blend=%d"),
				i, *L.Name, C.X, C.Y, C.Z, S.Y, S.Z,
				L.Tex ? (int32)L.Tex->GetSizeX() : 0, L.Tex ? (int32)L.Tex->GetSizeY() : 0,
				L.bLit ? 1 : 0, L.bReceiveShadows ? 1 : 0, L.bBackdrop ? 1 : 0, (int32)L.Blend);
		}
	}

	// Depth-based dimming: deeper layers render darker, like the lit falloff of the old build
	const float DeepLayerX = Layers[0].Box.GetCenter().X;
	const float NearLayerX = Layers.Last().Box.GetCenter().X;
	const float DepthDim = FMath::Clamp(CVarLKGSpriteDepthDim.GetValueOnGameThread(), 0.0f, 0.9f);
	const float DepthDimRange = FMath::Max(DeepLayerX - NearLayerX, 1.0f);

	// Frame parameters, mirroring ULookingGlassSceneCaptureComponent2D::RenderViews()
	const FLookingGlassTilingQuality& Tiling = CaptureComponent->GetTilingValues();
	const int32 NumTiles = Tiling.GetNumTiles();
	const int32 TilesX = Tiling.TilesX;
	const int32 TileSizeX = Tiling.TileSizeX;
	const int32 TileSizeY = Tiling.TileSizeY;
	const int32 PaddingY = Tiling.QuiltH - Tiling.TilesY * TileSizeY;
	if (NumTiles <= 0 || TileSizeX <= 0 || TileSizeY <= 0)
	{
		return false;
	}

	const float CamDistance = CaptureComponent->GetCameraDistance();
	const float TanHalfFOV = FMath::Tan(FMath::DegreesToRadians(CaptureComponent->FOV) * 0.5f);
	const float Aspect = CaptureComponent->GetAspectRatio();
	const FLGDeviceCalibration& Calibration = ILookingGlassRuntime::Get().GetCurrentCalibration();
	const float ViewConeSweep = CamDistance * FMath::Tan(FMath::DegreesToRadians(Calibration.ViewCone));
	const FVector CamPos = CaptureComponent->GetComponentLocation();
	const FVector Focal = CamPos + FVector(CamDistance, 0.0f, 0.0f);

	// Vertical off-axis: the camera is raised above the diorama and sheared back so the focal
	// plane stays framed - the old build's capture sat high looking slightly down, which is a
	// big part of its depth feel (near layers ride lower, the back wall peeks over them)
	const float TiltOffset = CamDistance * FMath::Tan(FMath::DegreesToRadians(
		FMath::Clamp(CVarLKGSpriteTilt.GetValueOnGameThread(), -30.0f, 30.0f)));

	// The light of record is the map's POINT light (the old build's rig: shadows project from
	// its position onto every layer behind the caster); a directional is the fallback for maps
	// without one. If the game hides its light (8 key), the hologram renders unlit.
	FLinearColor LayerTint = FLinearColor::White;
	FVector LightPos = FVector::ZeroVector;
	bool bHaveLightPos = false;
	float ShadowStrength = 0.0f;
	{
		UWorld* World = CaptureComponent->GetWorld();
		const float Ambient = FMath::Clamp(CVarLKGSpriteAmbient.GetValueOnGameThread(), 0.0f, 1.0f);
		// Approximates the scene path's auto-exposure + filmic tonemap + display gamma.
		// Two-point fit against the real 2D window: 0.095 linear renders white texels at
		// 121/255 and 0.979 linear at 219/255 (the ACES shoulder flattens the bright end).
		auto ToDisplay = [](float V) { return 0.864f * FMath::Pow(FMath::Max(V, 0.0f), 0.2545f); };

		float EffectiveLux = 0.0f;
		FLinearColor LightColor = FLinearColor::White;
		float NdotL = 1.0f;
		bool bFoundLight = false;
		bool bLightCastsShadows = false;

		for (TActorIterator<APointLight> It(World); It; ++It)
		{
			UPointLightComponent* LightComp = Cast<UPointLightComponent>(It->GetLightComponent());
			if (LightComp == nullptr || !LightComp->IsRegistered() || !LightComp->IsVisible() || It->IsHidden())
			{
				continue;
			}
			LightPos = LightComp->GetComponentLocation();
			bHaveLightPos = true;
			// Illuminance at the focal plane: candela / distance^2 (UE units are cm)
			float Candela = LightComp->Intensity;
			if (LightComp->IntensityUnits == ELightUnits::Lumens)
			{
				Candela = LightComp->Intensity / (4.0f * PI);
			}
			const float DistM = FMath::Max(FVector::Dist(LightPos, Focal) / 100.0f, 0.1f);
			EffectiveLux = Candela / (DistM * DistM);
			LightColor = LightComp->GetLightColor();
			bLightCastsShadows = LightComp->CastShadows;
			bFoundLight = true;
			break;
		}
		if (!bFoundLight)
		{
			for (TActorIterator<ADirectionalLight> It(World); It; ++It)
			{
				UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(It->GetLightComponent());
				if (LightComp == nullptr || !LightComp->IsRegistered() || !LightComp->IsVisible() || It->IsHidden())
				{
					continue;
				}
				const FVector LightDir = LightComp->GetForwardVector();
				// Quads face -X (toward the capture): N = (-1,0,0), so NdotL = saturate(+LightDir.X)
				NdotL = FMath::Clamp((float)LightDir.X, 0.0f, 1.0f);
				EffectiveLux = LightComp->Intensity;
				LightColor = LightComp->GetLightColor();
				bLightCastsShadows = LightComp->CastShadows;
				// Synthesize a far-away light position along the light direction so the same
				// projection code handles both light types (scale ~1, offset ~direction)
				LightPos = Focal - LightDir * (CamDistance * 20.0f);
				bHaveLightPos = LightDir.X > 0.05;
				bFoundLight = true;
				break;
			}
		}

		if (bFoundLight)
		{
			FLinearColor DisplayLight;
			if (bHaveLightPos && EffectiveLux > 0.0f && NdotL >= 1.0f)
			{
				// The old rig's point light hits the camera-facing layers head-on from a nearly
				// uniform distance, so the old build's lit look is FULL brightness plus shadows.
				// Only the light's color tints; the shadows do the lighting work.
				const float MaxComp = FMath::Max3(LightColor.R, LightColor.G, LightColor.B);
				DisplayLight = (MaxComp > KINDA_SMALL_NUMBER) ? (LightColor / MaxComp) : FLinearColor::White;
			}
			else
			{
				// Directional fallback: match the tonemapped 2D view (two-point fit against the
				// real scene render: dark end and the ACES shoulder)
				const FLinearColor LinearLight = LightColor * (EffectiveLux / PI);
				DisplayLight = FLinearColor(ToDisplay(LinearLight.R), ToDisplay(LinearLight.G), ToDisplay(LinearLight.B), 1.0f);
			}
			const float Brightness = Ambient + (1.0f - Ambient) * NdotL;
			LayerTint = FLinearColor(
				FMath::Clamp(DisplayLight.R * Brightness, 0.0f, 1.0f),
				FMath::Clamp(DisplayLight.G * Brightness, 0.0f, 1.0f),
				FMath::Clamp(DisplayLight.B * Brightness, 0.0f, 1.0f), 1.0f);

			if (bLightCastsShadows && bHaveLightPos)
			{
				ShadowStrength = FMath::Clamp(CVarLKGSpriteShadow.GetValueOnGameThread(), 0.0f, 1.0f);
			}

			static bool bLoggedLight = false;
			if (!bLoggedLight)
			{
				bLoggedLight = true;
				UE_LOG(LookingGlassLogRender, Display, TEXT("Sprite light: pos %s lux %.3f color %s -> tint %s shadows=%d"),
					*LightPos.ToString(), EffectiveLux, *LightColor.ToString(), *LayerTint.ToString(), ShadowStrength > 0.0f ? 1 : 0);
			}
		}

		// The game's 8 key swaps layer materials to unlit variants - an unlit scene shows no
		// shadows in the 2D view, so none on the hologram either (the backdrop is always
		// "unlit"/emissive, so judge by the actual game layers)
		bool bAnyLitLayer = false;
		for (const FSpriteLayer& L : Layers)
		{
			if (L.bLit && !L.bBackdrop) { bAnyLitLayer = true; break; }
		}
		if (!bAnyLitLayer)
		{
			ShadowStrength = 0.0f;
		}
	}

	// Track our own fps for the on-quilt readout
	static int32 GSpriteQuiltFrames = 0;
	static int32 GSpriteQuiltLastFPS = 0;
	static double GSpriteQuiltNextFPSTime = 0.0;
	GSpriteQuiltFrames++;
	const double Now = FPlatformTime::Seconds();
	if (Now >= GSpriteQuiltNextFPSTime)
	{
		if (GSpriteQuiltNextFPSTime != 0.0)
		{
			GSpriteQuiltLastFPS = GSpriteQuiltFrames;
		}
		GSpriteQuiltFrames = 0;
		GSpriteQuiltNextFPSTime = Now + 1.0;
	}

	FGameTime GameTime = FGameTime::CreateUndilated(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime());

	// SHADOW MASKS, one per receiving layer, built once per frame (the light projection is pure
	// world math, so masks are view-independent). Each mask's alpha = UNION of every nearer
	// caster's silhouette projected from the light onto this layer's plane, multiplied by the
	// receiver's own texture alpha. That is what a real shadow map computes: every caster always
	// casts, every pixel darkens at most ONCE no matter how many casters cover it, and shadow
	// only lands where the receiver has pixels. No content-size thresholds anywhere - they made
	// whole layers pop dark/light as the level scrolled and silently killed the player's shadow.
	TArray<const FTexture*> LayerMasks;
	LayerMasks.Init(nullptr, Layers.Num());
	if (ShadowStrength > 0.0f)
	{
		// Rooted pool of scratch RTs: index 0 accumulates the inverted caster union for the
		// receiver currently being built, the rest hold one finished mask per receiving layer
		static TArray<UTextureRenderTarget2D*> GMaskPool;
		const int32 MaskSize = 512;
		int32 PoolUsed = 1;
		auto GetPoolRT = [&](int32 Index) -> UTextureRenderTarget2D*
		{
			while (GMaskPool.Num() <= Index)
			{
				UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), UTextureRenderTarget2D::StaticClass());
				RT->AddToRoot();
				RT->ClearColor = FLinearColor(0, 0, 0, 0);
				RT->TargetGamma = 1.0f;
				RT->AddressX = TA_Clamp;
				RT->AddressY = TA_Clamp;
				RT->Filter = TF_Bilinear;
				RT->InitCustomFormat(MaskSize, MaskSize, PF_B8G8R8A8, false);
				RT->UpdateResourceImmediate();
				GMaskPool.Add(RT);
			}
			return GMaskPool[Index];
		};
		// A finished mask is sampled by a later canvas; canvas flushes only ever transition
		// their own target to RTV, so make the RTV -> SRV hand-off explicit
		auto FlushToTexture = [](FCanvas& InCanvas, FTextureRenderTargetResource* Res)
		{
			InCanvas.Flush_GameThread();
			ENQUEUE_RENDER_COMMAND(LKGShadowMaskToSRV)(
				[Res](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.Transition(FRHITransitionInfo(Res->GetRenderTargetTexture(), ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
				});
		};

		// Downsample chain for shadow softening (lkg.SpriteShadowSoft halvings of the union):
		// one rooted RT per step, shared by every receiver since each is consumed before the
		// next receiver starts
		static TArray<UTextureRenderTarget2D*> GSoftPool;
		const int32 SoftSteps = FMath::Clamp(CVarLKGSpriteShadowSoft.GetValueOnGameThread(), 0, 5);
		TArray<FTextureRenderTargetResource*, TInlineAllocator<5>> SoftRes;
		for (int32 Step = 0; Step < SoftSteps; Step++)
		{
			while (GSoftPool.Num() <= Step)
			{
				const int32 Size = MaskSize >> (GSoftPool.Num() + 1);
				UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), UTextureRenderTarget2D::StaticClass());
				RT->AddToRoot();
				RT->ClearColor = FLinearColor(0, 0, 0, 0);
				RT->TargetGamma = 1.0f;
				RT->AddressX = TA_Clamp;
				RT->AddressY = TA_Clamp;
				RT->Filter = TF_Bilinear;
				RT->InitCustomFormat(Size, Size, PF_B8G8R8A8, false);
				RT->UpdateResourceImmediate();
				GSoftPool.Add(RT);
			}
			SoftRes.Add(GSoftPool[Step]->GameThread_GetRenderTargetResource());
		}
		if (SoftRes.Contains(nullptr))
		{
			SoftRes.Empty();
		}
		const int32 SoftStepsAvail = SoftRes.Num();

		for (int32 RecvIdx = 0; RecvIdx < Layers.Num(); RecvIdx++)
		{
			const FSpriteLayer& Recv = Layers[RecvIdx];
			if (!Recv.bReceiveShadows)
			{
				continue;
			}
			const float RecvX = Recv.Box.GetCenter().X;
			const float RInvW = 1.0f / FMath::Max((float)(Recv.Box.Max.Y - Recv.Box.Min.Y), 1.0f);
			const float RInvH = 1.0f / FMath::Max((float)(Recv.Box.Max.Z - Recv.Box.Min.Z), 1.0f);

			// Project each nearer caster's populated content rect onto this layer's plane;
			// receivers no caster reaches skip mask building entirely
			struct FStamp { const FTexture* Tex; FVector4 UV; float X0, Y0, X1, Y1; };
			TArray<FStamp, TInlineAllocator<16>> Stamps;
			for (int32 CasterIdx = RecvIdx + 1; CasterIdx < Layers.Num(); CasterIdx++)
			{
				const FSpriteLayer& Caster = Layers[CasterIdx];
				const float CasterX = Caster.Box.GetCenter().X;
				if (!Caster.bCastShadows || CasterX - LightPos.X < 1.0f)
				{
					continue;	// light must be in front of the caster
				}
				const FVector4& CUV = Caster.ContentUV;
				if (CUV.Z - CUV.X <= 0.0 || CUV.W - CUV.Y <= 0.0)
				{
					continue;	// empty layers cast nothing
				}
				// Texture UV -> world on the caster quad (U spans Y min->max, V spans Z
				// top->bottom), then project from the light onto the receiver's plane. The
				// projection scale (Lr-Lx)/(Lc-Lx) stays near 1 for the far-forward light,
				// which is why the old build's shadows hugged their casters.
				const float CY0 = FMath::Lerp((float)Caster.Box.Min.Y, (float)Caster.Box.Max.Y, (float)CUV.X);
				const float CY1 = FMath::Lerp((float)Caster.Box.Min.Y, (float)Caster.Box.Max.Y, (float)CUV.Z);
				const float CZ1 = FMath::Lerp((float)Caster.Box.Max.Z, (float)Caster.Box.Min.Z, (float)CUV.Y);
				const float CZ0 = FMath::Lerp((float)Caster.Box.Max.Z, (float)Caster.Box.Min.Z, (float)CUV.W);
				const float S = FMath::Clamp((RecvX - LightPos.X) / (CasterX - LightPos.X), 1.0f, 3.0f);
				const float PY0 = LightPos.Y + (CY0 - LightPos.Y) * S;
				const float PY1 = LightPos.Y + (CY1 - LightPos.Y) * S;
				const float PZ0 = LightPos.Z + (CZ0 - LightPos.Z) * S;
				const float PZ1 = LightPos.Z + (CZ1 - LightPos.Z) * S;

				FStamp Stamp;
				Stamp.Tex = Caster.Tex;
				Stamp.UV = CUV;
				Stamp.X0 = (PY0 - (float)Recv.Box.Min.Y) * RInvW * MaskSize;
				Stamp.X1 = (PY1 - (float)Recv.Box.Min.Y) * RInvW * MaskSize;
				Stamp.Y0 = ((float)Recv.Box.Max.Z - PZ1) * RInvH * MaskSize;
				Stamp.Y1 = ((float)Recv.Box.Max.Z - PZ0) * RInvH * MaskSize;
				if (Stamp.X1 <= Stamp.X0 || Stamp.Y1 <= Stamp.Y0 ||
					Stamp.X1 <= 0.0f || Stamp.X0 >= MaskSize || Stamp.Y1 <= 0.0f || Stamp.Y0 >= MaskSize)
				{
					continue;
				}
				Stamps.Add(Stamp);
			}
			if (Stamps.Num() == 0)
			{
				continue;
			}

			UTextureRenderTarget2D* UnionRT = GetPoolRT(0);
			UTextureRenderTarget2D* MaskRT = GetPoolRT(PoolUsed);
			FTextureRenderTargetResource* UnionRes = UnionRT->GameThread_GetRenderTargetResource();
			FTextureRenderTargetResource* MaskRes = MaskRT->GameThread_GetRenderTargetResource();
			if (UnionRes == nullptr || MaskRes == nullptr)
			{
				continue;
			}
			PoolUsed++;
			const FTexture* UnionTex = UnionRes;

			// Pass 1: inverted caster union. Alpha starts at 1 and every caster silhouette
			// multiplies in (1 - alpha) via SE_BLEND_AlphaHoldout, so overlapping casters
			// (Castlevania's stacked tree layers) still only count once. NOTE: the plain
			// SE_BLEND_Translucent never writes dest alpha (BF_Zero/BF_One) - building a mask
			// with it leaves alpha at the clear value and the stamp draws nothing.
			{
				FCanvas UnionCanvas(UnionRes, nullptr, GameTime, GMaxRHIFeatureLevel);
				UnionCanvas.Clear(FLinearColor(0, 0, 0, 1));
				for (const FStamp& Stamp : Stamps)
				{
					FCanvasTileItem Item(FVector2D(Stamp.X0, Stamp.Y0), Stamp.Tex,
						FVector2D(Stamp.X1 - Stamp.X0, Stamp.Y1 - Stamp.Y0),
						FVector2D(Stamp.UV.X, Stamp.UV.Y), FVector2D(Stamp.UV.Z, Stamp.UV.W), FLinearColor::White);
					Item.BlendMode = SE_BLEND_AlphaHoldout;	// dest alpha *= 1 - caster alpha
					UnionCanvas.DrawItem(Item);
				}
				FlushToTexture(UnionCanvas, UnionRes);
			}

			// Softening: halve the union through the small scratch chain. Each step is a
			// bilinear 2:1 copy, i.e. an exact 2x2 box filter, and the stamp later samples the
			// small mask bilinearly, so the silhouette edge spreads over 2^N mask texels
			// instead of being a pixel-exact copy of the sprite. The data stays in the alpha
			// channel (no canvas blend mode can accumulate alpha additively, which rules out a
			// multi-tap blur). SE_BLEND_AlphaBlend onto a cleared target is a straight copy.
			for (int32 Step = 0; Step < SoftStepsAvail; Step++)
			{
				FTextureRenderTargetResource* SmallRes = SoftRes[Step];
				const float SmallSize = (float)(MaskSize >> (Step + 1));
				FCanvas SmallCanvas(SmallRes, nullptr, GameTime, GMaxRHIFeatureLevel);
				SmallCanvas.Clear(FLinearColor(0, 0, 0, 0));
				FCanvasTileItem CopyItem(FVector2D(0, 0), UnionTex, FVector2D(SmallSize, SmallSize),
					FVector2D(0, 0), FVector2D(1, 1), FLinearColor::White);
				CopyItem.BlendMode = SE_BLEND_AlphaBlend;
				SmallCanvas.DrawItem(CopyItem);
				FlushToTexture(SmallCanvas, SmallRes);
				UnionTex = SmallRes;
			}

			// Pass 2: mask alpha = receiver alpha * caster union. Solid receivers (the backdrop
			// wall, opaque photo materials) count as alpha 1 everywhere; game layers contribute
			// their colorkey alpha so shadow never floats in their transparent space.
			{
				const FTexture* RecvAlphaTex = (Recv.bBackdrop || Recv.Blend == SE_BLEND_Opaque) ? GWhiteTexture : Recv.Tex;
				FCanvas MaskCanvas(MaskRes, nullptr, GameTime, GMaxRHIFeatureLevel);
				MaskCanvas.Clear(FLinearColor(0, 0, 0, 0));
				FCanvasTileItem RecvItem(FVector2D(0, 0), RecvAlphaTex, FVector2D(MaskSize, MaskSize),
					FVector2D(0, 0), FVector2D(1, 1), FLinearColor(0, 0, 0, 1));
				RecvItem.BlendMode = SE_BLEND_AlphaBlend;	// dest alpha = receiver alpha
				MaskCanvas.DrawItem(RecvItem);
				FCanvasTileItem UnionItem(FVector2D(0, 0), UnionTex, FVector2D(MaskSize, MaskSize),
					FVector2D(0, 0), FVector2D(1, 1), FLinearColor::White);
				UnionItem.BlendMode = SE_BLEND_AlphaHoldout;	// dest alpha *= 1 - (1 - union) = union
				MaskCanvas.DrawItem(UnionItem);
				FlushToTexture(MaskCanvas, MaskRes);
			}
			LayerMasks[RecvIdx] = MaskRes;
		}
	}

	FTextureRenderTargetResource* QuiltRes = InQuiltRT->GameThread_GetRenderTargetResource();
	FCanvas Canvas(QuiltRes, nullptr, GameTime, GMaxRHIFeatureLevel);
	Canvas.Clear(FLinearColor::Black);

	// Draws a texture rect clipped to its tile (FCanvas has no scissor, so clip analytically,
	// remapping UVs to the clipped portion)
	auto DrawClippedTile = [&Canvas](const FTexture* Tex, float X0, float Y0, float X1, float Y1,
		const FIntRect& TileRect, const FLinearColor& Color, ESimpleElementBlendMode Blend = SE_BLEND_Translucent,
		float InU0 = 0.0f, float InV0 = 0.0f, float InU1 = 1.0f, float InV1 = 1.0f)
	{
		if (X1 <= X0 || Y1 <= Y0 ||
			X1 <= TileRect.Min.X || X0 >= TileRect.Max.X ||
			Y1 <= TileRect.Min.Y || Y0 >= TileRect.Max.Y)
		{
			return;
		}
		float U0 = InU0, V0 = InV0, U1 = InU1, V1 = InV1;
		const float InvW = (InU1 - InU0) / (X1 - X0);
		const float InvH = (InV1 - InV0) / (Y1 - Y0);
		if (X0 < TileRect.Min.X) { U0 += (TileRect.Min.X - X0) * InvW; X0 = TileRect.Min.X; }
		if (X1 > TileRect.Max.X) { U1 -= (X1 - TileRect.Max.X) * InvW; X1 = TileRect.Max.X; }
		if (Y0 < TileRect.Min.Y) { V0 += (TileRect.Min.Y - Y0) * InvH; Y0 = TileRect.Min.Y; }
		if (Y1 > TileRect.Max.Y) { V1 -= (Y1 - TileRect.Max.Y) * InvH; Y1 = TileRect.Max.Y; }

		FCanvasTileItem Tile(FVector2D(X0, Y0), Tex, FVector2D(X1 - X0, Y1 - Y0),
			FVector2D(U0, V0), FVector2D(U1, V1), Color);
		Tile.BlendMode = Blend;
		Canvas.DrawItem(Tile);
	};

	const FString FPSText = (CVarLKGShowFPS.GetValueOnGameThread() != 0 && GSpriteQuiltLastFPS > 0)
		? FString::Printf(TEXT("%d FPS"), GSpriteQuiltLastFPS) : FString();

	// Help screen layout, computed once: text format is line 1 = title, "key\taction" = two-column
	// row, no tab = centered, empty = half-row spacing (see the game's HelpScreen::BuildHelpText).
	// Sized to fit the tile with the same crude width metric the status text uses.
	struct FHelpLine
	{
		FString Key;
		FString Action;
		bool bTwoColumn = false;
	};
	TArray<FHelpLine> HelpLines;
	float HelpScale = 1.0f, HelpCharH = 0.0f, HelpBlockH = 0.0f;
	if (!HelpText.IsEmpty() && GEngine != nullptr)
	{
		TArray<FString> RawLines;
		HelpText.ParseIntoArrayLines(RawLines, false);	// keep empties, they're the row spacing
		int32 MaxLen = 0;
		for (const FString& Raw : RawLines)
		{
			FHelpLine Line;
			Line.bTwoColumn = Raw.Split(TEXT("\t"), &Line.Key, &Line.Action);
			if (!Line.bTwoColumn)
			{
				Line.Action = Raw;
			}
			MaxLen = FMath::Max(MaxLen, Raw.Len());
			HelpLines.Add(Line);
		}
		HelpCharH = FMath::Max(1.0f, GEngine->GetLargeFont()->GetMaxCharHeight());
		HelpScale = FMath::Min(1.4f, (TileSizeX * 0.92f) / FMath::Max(1.0f, MaxLen * 10.0f));
		auto BlockHeight = [&](float Scale)
		{
			float H = 0.0f;
			for (int32 i = 0; i < HelpLines.Num(); i++)
			{
				const bool bEmpty = !HelpLines[i].bTwoColumn && HelpLines[i].Action.IsEmpty();
				H += HelpCharH * Scale * ((i == 0) ? 1.9f : (bEmpty ? 0.5f : 1.25f));
			}
			return H;
		};
		HelpBlockH = BlockHeight(HelpScale);
		const float MaxBlockH = TileSizeY * 0.82f;
		if (HelpBlockH > MaxBlockH)
		{
			HelpScale *= MaxBlockH / HelpBlockH;
			HelpBlockH = BlockHeight(HelpScale);
		}
	}

	for (int32 View = 0; View < NumTiles; View++)
	{
		// Same tile placement convention as CopyToQuiltShader_RenderThread
		const int32 RI = NumTiles - View - 1;
		const int32 TileX = (View % TilesX) * TileSizeX;
		const int32 TileY = (RI / TilesX) * TileSizeY + PaddingY;
		const FIntRect TileRect(TileX, TileY, TileX + TileSizeX, TileY + TileSizeY);

		const float ViewLerp = (NumTiles > 1) ? ((float)View / (NumTiles - 1.0f) - 0.5f) : 0.0f;
		const float ViewOffset = ViewLerp * ViewConeSweep;

		int32 LayerIdx = -1;

		// While the help screen is up the game is paused and fully hidden: tiles render just
		// the help text on black (game and help used to obscure each other)
		if (HelpLines.Num() == 0)
		for (const FSpriteLayer& Layer : Layers)
		{
			LayerIdx++;
			const float LayerX = Layer.Box.GetCenter().X;
			const float Depth = LayerX - CamPos.X;	// distance from camera plane
			if (Depth < 10.0f)
			{
				continue;
			}
			// Empty layer (zero content rect reported by the game): nothing to draw in any
			// tile - saves NumTiles quad draws per empty layer, which matters at 24+ layers
			if (Layer.ContentUV.Z - Layer.ContentUV.X <= 0.0 ||
				Layer.ContentUV.W - Layer.ContentUV.Y <= 0.0)
			{
				continue;
			}

			// Off-axis projection: camera slides +Y by ViewOffset (and +Z by TiltOffset), frustum
			// shears back so the focal plane (X = Focal.X) is identical in every view
			auto NdcX = [&](float Y)
			{
				return ((Y - Focal.Y - ViewOffset) / Depth + ViewOffset / CamDistance) / TanHalfFOV;
			};
			auto NdcZ = [&](float Z)
			{
				return Aspect * ((Z - Focal.Z - TiltOffset) / Depth + TiltOffset / CamDistance) / TanHalfFOV;
			};
			auto ToTileX = [&](float Ndc) { return TileX + (Ndc * 0.5f + 0.5f) * TileSizeX; };
			auto ToTileY = [&](float Ndc) { return TileY + (0.5f - Ndc * 0.5f) * TileSizeY; };

			// Unlit layers (the 8-key materials, the emissive backdrop) draw raw like the 2D
			// view; Layer.Color carries the game's SetTintBG coloring for the backdrop
			const FLinearColor& Base = Layer.bLit ? LayerTint : FLinearColor::White;
			const float Dim = Layer.bLit
				? 1.0f - DepthDim * FMath::Clamp((LayerX - NearLayerX) / DepthDimRange, 0.0f, 1.0f)
				: 1.0f;
			const FLinearColor Tint(
				Base.R * Dim * Layer.Color.R,
				Base.G * Dim * Layer.Color.G,
				Base.B * Dim * Layer.Color.B, 1.0f);
			DrawClippedTile(Layer.Tex,
				ToTileX(NdcX(Layer.Box.Min.Y)),
				ToTileY(NdcZ(Layer.Box.Max.Z)),
				ToTileX(NdcX(Layer.Box.Max.Y)),
				ToTileY(NdcZ(Layer.Box.Min.Z)),
				TileRect, Tint, Layer.Blend);

			// Shadows: one darkening stamp from this layer's pre-built mask (union of every
			// nearer caster's silhouette projected from the light, already multiplied by this
			// layer's own alpha). Drawn across the same screen rect as the layer itself, right
			// after it, so nearer layers still cover it (occlusion via painter's order).
			if (ShadowStrength > 0.0f && LayerMasks[LayerIdx] != nullptr)
			{
				DrawClippedTile(LayerMasks[LayerIdx],
					ToTileX(NdcX(Layer.Box.Min.Y)),
					ToTileY(NdcZ(Layer.Box.Max.Z)),
					ToTileX(NdcX(Layer.Box.Max.Y)),
					ToTileY(NdcZ(Layer.Box.Min.Z)),
					TileRect, FLinearColor(0.0f, 0.0f, 0.0f, ShadowStrength));
			}
		}

		// Help screen: the key list drawn identically in every tile, so the lens reconstructs
		// it as one screen-locked overlay.  The layer quads are skipped above while it's up, so
		// the tile background is the quilt clear color (black) - no dim panel needed
		if (HelpLines.Num() > 0 && GEngine != nullptr)
		{
			const FLinearColor KeyColor(1.0f, 0.85f, 0.3f);
			const float ColSplitX = TileX + TileSizeX * 0.42f;
			const float ColGap = TileSizeX * 0.03f;
			float Y = TileY + (TileSizeY - HelpBlockH) * 0.5f;
			for (int32 i = 0; i < HelpLines.Num(); i++)
			{
				const FHelpLine& Line = HelpLines[i];
				const bool bEmpty = !Line.bTwoColumn && Line.Action.IsEmpty();
				if (bEmpty)
				{
					Y += HelpCharH * HelpScale * 0.5f;
					continue;
				}
				const float Scale = (i == 0) ? HelpScale * 1.5f : HelpScale;
				if (Line.bTwoColumn)
				{
					FCanvasTextItem KeyItem(FVector2D(ColSplitX - Line.Key.Len() * 10.0f * Scale, Y),
						FText::FromString(Line.Key), GEngine->GetLargeFont(), KeyColor);
					KeyItem.Scale = FVector2D(Scale, Scale);
					KeyItem.EnableShadow(FLinearColor::Black);
					Canvas.DrawItem(KeyItem);

					FCanvasTextItem ActionItem(FVector2D(ColSplitX + ColGap, Y),
						FText::FromString(Line.Action), GEngine->GetLargeFont(), FLinearColor::White);
					ActionItem.Scale = FVector2D(Scale, Scale);
					ActionItem.EnableShadow(FLinearColor::Black);
					Canvas.DrawItem(ActionItem);
				}
				else
				{
					// title (line 0) / centered footer
					const float W = Line.Action.Len() * 10.0f * Scale;
					FCanvasTextItem CenterItem(FVector2D(TileX + (TileSizeX - W) * 0.5f, Y),
						FText::FromString(Line.Action), GEngine->GetLargeFont(),
						(i == 0) ? KeyColor : FLinearColor(0.8f, 0.8f, 0.8f));
					CenterItem.Scale = FVector2D(Scale, Scale);
					CenterItem.EnableShadow(FLinearColor::Black);
					Canvas.DrawItem(CenterItem);
				}
				Y += HelpCharH * HelpScale * ((i == 0) ? 1.9f : 1.25f);
			}
		}

		// FPS counter: top-left of every tile
		if (!FPSText.IsEmpty() && GEngine != nullptr)
		{
			FCanvasTextItem FPSItem(FVector2D(TileX + TileSizeX * 0.05f, TileY + TileSizeY * 0.03f),
				FText::FromString(FPSText), GEngine->GetLargeFont(), FLinearColor::White);
			FPSItem.Scale = FVector2D(2.0f, 2.0f);
			FPSItem.EnableShadow(FLinearColor::Black);
			Canvas.DrawItem(FPSItem);
		}

		// Status text (rom names etc): bottom of the tile, scaled down if it would spill into
		// the neighboring tile
		if (!OverlayText.IsEmpty() && GEngine != nullptr)
		{
			float TextScale = 2.0f;
			const float ApproxWidth = OverlayText.Len() * 10.0f * TextScale;
			const float MaxWidth = TileSizeX * 0.92f;
			if (ApproxWidth > MaxWidth)
			{
				TextScale *= MaxWidth / ApproxWidth;
			}
			FCanvasTextItem TextItem(FVector2D(TileX + TileSizeX * 0.04f, TileY + TileSizeY * 0.9f),
				FText::FromString(OverlayText), GEngine->GetLargeFont(), FLinearColor::White);
			TextItem.Scale = FVector2D(TextScale, TextScale);
			TextItem.EnableShadow(FLinearColor::Black);
			Canvas.DrawItem(TextItem);
		}
	}

	Canvas.Flush_GameThread();

	return true;
}

void FLookingGlassViewportClient::VisualizeRenderTarget(FViewport* InViewport, UTextureRenderTarget2D* QuiltRT, bool bRenderOnDevice, const FIntPoint& Tiles, float Aspect)
{
	// Pass composed quilt to target: either device or debug window
	FTextureRenderTargetResource* RenderTarget = QuiltRT->GameThread_GetRenderTargetResource();
	if (!RenderTarget->GetTexture2DRHI())
	{
		return;
	}

	if (bRenderOnDevice && LookingGlassSelfRenderEnabled())
	{
		// Self-render: run the lenticular shader from the quilt into our own device window's
		// backbuffer. No Bridge call happens per frame, so a Bridge stall can never freeze
		// or pause the hologram (the legacy HoloPlay plugin worked exactly this way).
		const FLGDeviceCalibration& Cal = ILookingGlassRuntime::Get().GetCurrentCalibration();
		if (Cal.Width <= 0 || Cal.Height <= 0 || InViewport->GetSizeXY().X <= 0)
		{
			return;
		}

		FLookingGlassLenticularPS::FParameters Params;
		FMemory::Memzero(Params);

		// Raw factory calibration to shader values, same math the legacy plugin used
		const float RawSlope = (FMath::Abs(Cal.Slope) > KINDA_SMALL_NUMBER) ? Cal.Slope : 1.0f;
		const float ScreenInches = (float)Cal.Width / ((Cal.DPI > 1.0f) ? Cal.DPI : 324.0f);
		Params.pitch = Cal.Pitch * ScreenInches * FMath::Cos(FMath::Atan(1.0f / RawSlope));
		Params.slope = (float)Cal.Height / ((float)Cal.Width * RawSlope);
		Params.center = Cal.Center;
		Params.subp = 1.0f / (3.0f * (float)Cal.Width) * ((Cal.FlipX > 0.5f) ? -1.0f : 1.0f);

		const int32 NumViews = FMath::Max(1, Tiles.X * Tiles.Y);
		Params.tile = FVector4f((float)Tiles.X, (float)Tiles.Y, (float)NumViews, (float)NumViews);

		// Used fraction of the quilt texture; 1.0 when tiles fill it exactly (Portrait: 8x6 into
		// 3360x3360 is exact) and for the single-tile 2D mode
		float PortionX = 1.0f, PortionY = 1.0f;
		TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> CaptureComponent = LookingGlass::GetGameLookingGlassCaptureComponent();
		if (NumViews > 1 && CaptureComponent.IsValid())
		{
			const FLookingGlassTilingQuality& TilingValues = CaptureComponent->GetTilingValues();
			if (TilingValues.TilesX == Tiles.X && TilingValues.TilesY == Tiles.Y &&
				TilingValues.PortionX > 0.0f && TilingValues.PortionY > 0.0f)
			{
				PortionX = TilingValues.PortionX;
				PortionY = TilingValues.PortionY;
			}
		}
		Params.viewPortion = FVector2f(PortionX, PortionY);

		const float DeviceAspect = (float)InViewport->GetSizeXY().X / (float)InViewport->GetSizeXY().Y;
		Params.aspect = FVector4f(DeviceAspect, DeviceAspect, 0.0f, 0.0f);
		Params.QuiltMode = CVarLKGSelfRenderQuilt.GetValueOnGameThread();
		Params.FlipYTexCoords = 1;
		// The 16-bit quilt holds linear values; the window backbuffer is gamma-space
		Params.OutputGamma = FMath::Max(0.1f, CVarLKGSelfRenderGamma.GetValueOnGameThread());

		ENQUEUE_RENDER_COMMAND(LKGRenderLenticular)(
			[RenderTarget, InViewport, Params](FRHICommandListImmediate& RHICmdList)
			{
				FTextureRHIRef ViewportRT = InViewport->GetRenderTargetTexture();
				FTextureRHIRef Quilt = RenderTarget->GetRenderTargetTexture();
				if (ViewportRT.IsValid() && Quilt.IsValid())
				{
					RenderLenticular_RenderThread(RHICmdList, Quilt, ViewportRT, Params);
				}
			});
		return;
	}

	if (bRenderOnDevice)
	{
		// Prepare Bridge if needed
		FLookingGlassBridge& Bridge = ILookingGlassRuntime::Get().GetBridge();
#if 1
		// Bridge calls have hard thread affinity AND stall for 0.7-20s in the field
		// (DrawInteropQuiltTextureDX itself, measured). The whole Bridge lifecycle therefore
		// lives on its own thread - initialized there, window created there, drawn there - and
		// this call just queues the latest frame and returns. Stalls only pause the hologram;
		// offenders are logged to lkg_diag.txt next to the top-level exe.
		void* RTNativeHandle = RenderTarget->GetTexture2DRHI()->GetNativeResource();
		Bridge.QueueDraw(RTNativeHandle, Tiles.X, Tiles.Y, Aspect);

		// Mid-stall diagnostic: when a bridge draw has been blocked >2s, capture that thread's
		// callstack ONCE per stall - it names the exact API the stall parks in (service CPU is
		// idle during stalls, so this is a blocked wait; the stack says on what)
		{
			static bool GStackCapturedThisStall = false;
			uint32 BridgeThreadId = 0;
			if (Bridge.IsDrawStuck(2.0, BridgeThreadId))
			{
				if (!GStackCapturedThisStall && BridgeThreadId != 0)
				{
					GStackCapturedThisStall = true;
					ANSICHAR StackTrace[16384];
					StackTrace[0] = 0;
					FPlatformStackWalk::ThreadStackWalkAndDump(StackTrace, UE_ARRAY_COUNT(StackTrace), 0, BridgeThreadId);
					const FString DiagLine = FString::Printf(TEXT("MID-STALL bridge thread callstack:\n%s"), ANSI_TO_TCHAR(StackTrace));
					FFileHelper::SaveStringToFile(DiagLine, *(FPaths::RootDir() / TEXT("lkg_diag.txt")),
						FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
					UE_LOG(LookingGlassLogRender, Warning, TEXT("%s"), *DiagLine);
				}
			}
			else
			{
				GStackCapturedThisStall = false;
			}
		}
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
		// Keep the whole sprite chain gamma-neutral: emulator textures are SRGB=0 and already
		// hold display-encoded palette values. TargetGamma 1 stops FCanvas applying its 1/2.2
		// display encode on top (and the lenticular pass encodes nothing either), so vertex
		// tints and shadow alphas multiply the displayed value EXACTLY - with the default 2.2
		// chain a 0.47 tint reached the panel as 0.86 and all lighting looked washed out.
		StaticQuiltRT->TargetGamma = 1.0f;
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
