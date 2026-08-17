#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * @class	FLookingGlassEditorStyle
 *
 * @brief	A LookingGlass editor style.
 * 			Loading UI resources
 */

class FLookingGlassEditorStyle
{
public:

	static void Initialize();

	static void Shutdown();

	/**
	 * @fn	static void FLookingGlassEditorStyle::ReloadTextures();
	 *
	 * @brief	reloads textures used by slate renderer
	 */

	static void ReloadTextures();

	/**
	 * @fn	static const ISlateStyle& FLookingGlassEditorStyle::Get();
	 *
	 * @brief	Gets the get
	 *
	 * @returns	The Slate style set for the Shooter game.
	 */

	static const ISlateStyle& Get();

	static FName GetStyleSetName();

private:
	static TSharedRef< class FSlateStyleSet > Create();

private:
	static TSharedPtr< class FSlateStyleSet > StyleInstance;
};