#include "HoloHUD.h"
#include "LibretroManager.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GlobalRenderResources.h" //GWhiteTexture

void AHoloHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GEngine || !g_pLibretroManager) return;
	if (!g_pLibretroManager->m_helpScreen.IsVisible()) return;

	FString helpText = g_pLibretroManager->m_helpScreen.GetHelpText();
	if (helpText.IsEmpty()) return;

	//line-by-line drawing on purpose: FCanvasTextItem silently drops \n with offline-cached
	//fonts, and the two-column layout needs per-line control anyway
	TArray<FString> lines;
	helpText.ParseIntoArrayLines(lines, false); //keep empty lines, they're the row spacing

	UFont* pFont = GEngine->GetLargeFont();
	if (!pFont || lines.Num() == 0) return;

	const float baseScale = FMath::Max(0.75f, Canvas->ClipY / 720.0f) * 1.15f;
	const float titleScale = baseScale * 1.6f;
	const float charH = pFont->GetMaxCharHeight();
	const float lineH = charH * baseScale * 1.25f;
	const float titleH = charH * titleScale * 1.5f;

	float panelH = titleH + lineH; //title + bottom padding
	for (int i = 1; i < lines.Num(); i++)
	{
		panelH += lines[i].IsEmpty() ? lineH * 0.5f : lineH;
	}

	const float panelW = FMath::Min(640.0f * baseScale, Canvas->ClipX * 0.9f);
	const float panelX = (Canvas->ClipX - panelW) * 0.5f;
	const float panelY = FMath::Max(0.0f, (Canvas->ClipY - panelH) * 0.5f);

	const FLinearColor keyColor(1.0f, 0.85f, 0.3f);
	const FLinearColor textColor(1.0f, 1.0f, 1.0f);
	const FLinearColor footColor(0.7f, 0.7f, 0.7f);

	//opaque full-screen cover: the game is paused underneath anyway, and letting it show
	//through made both the game and the help hard to read
	FCanvasTileItem cover(FVector2D(0, 0), GWhiteTexture, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor::Black);
	cover.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(cover);

	float y = panelY + lineH * 0.4f;
	for (int i = 0; i < lines.Num(); i++)
	{
		const FString& line = lines[i];
		if (i > 0 && line.IsEmpty())
		{
			y += lineH * 0.5f;
			continue;
		}

		float scale = (i == 0) ? titleScale : baseScale;
		FString key, action;
		bool bTwoColumn = line.Split(TEXT("\t"), &key, &action);

		if (i == 0 || !bTwoColumn)
		{
			//title / footer: centered
			float w, h;
			Canvas->TextSize(pFont, line, w, h, scale, scale);
			FCanvasTextItem item(FVector2D(panelX + (panelW - w) * 0.5f, y), FText::FromString(line), pFont,
				(i == 0) ? keyColor : footColor);
			item.Scale = FVector2D(scale, scale);
			item.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(item);
		}
		else
		{
			//key right-aligned against the column split, action left-aligned after it
			float w, h;
			Canvas->TextSize(pFont, key, w, h, scale, scale);
			FCanvasTextItem keyItem(FVector2D(panelX + panelW * 0.40f - w, y), FText::FromString(key), pFont, keyColor);
			keyItem.Scale = FVector2D(scale, scale);
			keyItem.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(keyItem);

			FCanvasTextItem actionItem(FVector2D(panelX + panelW * 0.44f, y), FText::FromString(action), pFont, textColor);
			actionItem.Scale = FVector2D(scale, scale);
			actionItem.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(actionItem);
		}

		y += (i == 0) ? titleH : lineH;
	}
}
