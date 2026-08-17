#include "Render/SLookingGlassViewport.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "Widgets/SToolTip.h"

#include "Render/LookingGlassViewportClient.h"
#include "Misc/LookingGlassLog.h"
#include "ILookingGlassRuntime.h"

void SLookingGlassViewport::Construct(const FArguments& InArgs)
{
	// Create Viewport Widget
	Viewport = SNew(SViewport)
		.IsEnabled(true)
		.ShowEffectWhenDisabled(false)
		.EnableGammaCorrection(false) // FIX dark color, should be false. Whether or not to enable gamma correction.Doesn't apply when rendering directly to a backbuffer.
		.ReverseGammaCorrection(false)
		.EnableBlending(false)
		.EnableStereoRendering(false)
		.PreMultipliedAlpha(true)
		.IgnoreTextureAlpha(true);

	// Set console variables
	// TODO. Something with texture sampler. It happens when you create quilt texture. State
	// SamplerDesc.MaxAnisotropy = ComputeAnisotropyRT(Initializer.MaxAnisotropy);
	// D3D11State.cpp
	IConsoleManager& ConsoleMan = IConsoleManager::Get();
	static IConsoleVariable* MaxAnisotropy = ConsoleMan.FindConsoleVariable(TEXT("r.MaxAnisotropy"));
	static const int32 MaxAnisotropyVal = 0;
	MaxAnisotropy->Set(MaxAnisotropyVal);

	// Fix shadow rendering
	static IConsoleVariable* MaxCSMResolution = ConsoleMan.FindConsoleVariable(TEXT("r.Shadow.MaxCSMResolution"));
	static const int32 MaxCSMResolutionVal = 1024;
	MaxCSMResolution->Set(MaxCSMResolutionVal);

	// Create Viewport Client
	LookingGlassViewportClient = MakeShareable(new FLookingGlassViewportClient());

	// Create Scene Viewport (5.8: the FViewportClient* constructor is deprecated, Create takes shared ownership of the client)
	SceneViewport = FSceneViewport::Create(LookingGlassViewportClient, Viewport);

	// Set Viewport 
	LookingGlassViewportClient->Viewport = SceneViewport.Get();
	LookingGlassViewportClient->LookingGlassSceneViewport = SceneViewport.Get();

	// Assign SceneViewport to Viewport widget. Needed for rendering
	Viewport->SetViewportInterface(SceneViewport.ToSharedRef());

	// Assign Viewport widget for our custom PlayScene Viewport
	this->ChildSlot
		[
			Viewport.ToSharedRef()
		];

	// Resize viewport only if in MainViewport and in game mode	
	if (ILookingGlassRuntime::Get().GetCurrentLookingGlassModeType() == ELookingGlassModeType::PlayMode_InMainViewport && GEngine->GameViewport != nullptr)
	{
		UGameViewportClient* GameViewport = GEngine->GameViewport;
		FIntPoint Size = GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0 && Size.Y > 0)
		{
			SceneViewport->UpdateViewportRHI(false, Size.X, Size.Y, GameViewport->GetWindow()->GetWindowMode(), PF_Unknown);
		}
	}
}

void SLookingGlassViewport::Tick(const FGeometry & AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Call FViewport each engine tick
	SceneViewport->Draw();
}
