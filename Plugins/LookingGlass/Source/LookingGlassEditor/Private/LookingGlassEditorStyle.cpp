
#include "LookingGlassEditorStyle.h"

#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr< FSlateStyleSet > FLookingGlassEditorStyle::StyleInstance = NULL;

void FLookingGlassEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FLookingGlassEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FLookingGlassEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("LookingGlassStyle"));
	return StyleSetName;
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define TTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".ttf") ), __VA_ARGS__ )
#define OTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".otf") ), __VA_ARGS__ )

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);
const FVector2D Icon60x40(60.0f, 40.0f);
const FVector2D Icon30x20(30.0f, 20.0f);
const FVector2D Icon60x60(60.0f, 60.0f);

TSharedRef< FSlateStyleSet > FLookingGlassEditorStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("LookingGlassStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("LookingGlass")->GetBaseDir() / TEXT("Resources"));

	Style->Set("LookingGlass.RepeatLastPlay", new IMAGE_BRUSH("LookingGlassOpenWindow_60_40", Icon60x40));
	Style->Set("LookingGlass.RepeatLastPlay.Small", new IMAGE_BRUSH("LookingGlassOpenWindow_60_40", Icon30x20));

	Style->Set("LookingGlass.OpenWindow", new IMAGE_BRUSH(TEXT("LookingGlassOpenWindow_60_40"), Icon60x40));
	Style->Set("LookingGlass.CloseWindow", new IMAGE_BRUSH(TEXT("LookingGlassCloseWindow_60_40"), Icon60x40));
	Style->Set("LookingGlass.OpenSettings", new IMAGE_BRUSH(TEXT("LookingGlassOpenSettings_60_40"), Icon60x40));

	Style->Set("LookingGlass.PlayInLookingGlassWindow", new IMAGE_BRUSH("LookingGlassOpenWindow_60_40", Icon60x40));
	Style->Set("LookingGlass.OpenLookingGlassWindow.Small", new IMAGE_BRUSH("LookingGlassOpenWindow_60_40", Icon30x20));
	Style->Set("LookingGlass.PlayInMainViewport", new IMAGE_BRUSH("LookingGlassInMainViewport_60_40", Icon60x40));
	Style->Set("LookingGlass.PlayInMainViewport.Small", new IMAGE_BRUSH("LookingGlassInMainViewport_60_40", Icon30x20));

	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT

void FLookingGlassEditorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FLookingGlassEditorStyle::Get()
{
	return *StyleInstance;
}
