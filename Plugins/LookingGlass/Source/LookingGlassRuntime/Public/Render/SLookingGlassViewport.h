#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class SViewport;
class FSceneViewport;
class FLookingGlassViewportClient;
class FSceneViewport;

/**
 * @class	SLookingGlassViewport
 *
 * @brief	A LookingGlass viewport widget
 * 			This is for rendering 2D geometry to the Window
 */

class SLookingGlassViewport : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SLookingGlassViewport) 
		: _Content()
	{ }
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

	TSharedRef<FLookingGlassViewportClient> GetLookingGlassViewportClient() { return LookingGlassViewportClient.ToSharedRef();  }
	TSharedRef<FSceneViewport> GetSceneViewport() { return SceneViewport.ToSharedRef(); }

	/** SWidget interface */

	/**
	 * @fn	virtual void SLookingGlassViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	 *
	 * @brief	Tick each engine tick, this is where we call FViewport::Draw() -->
	 * 			FPlaySceneViewportClient::Draw(...)
	 *
	 * @param	AllottedGeometry	The allotted geometry.
	 * @param	InCurrentTime   	The in current time.
	 * @param	InDeltaTime			The in delta time.
	 */

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;


private:
	TSharedPtr<SViewport> Viewport;
	TSharedPtr<FSceneViewport> SceneViewport;
	TSharedPtr<FLookingGlassViewportClient> LookingGlassViewportClient;
};