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
	//fonts, and the column layout needs per-line control anyway
	TArray<FString> lines;
	helpText.ParseIntoArrayLines(lines, false); //keep empty lines, they're the row spacing

	UFont* pFont = GEngine->GetLargeFont();
	if (!pFont || lines.Num() == 0) return;

	float baseScale = FMath::Max(0.75f, Canvas->ClipY / 720.0f) * 1.15f;
	const float titleScaleMult = 1.6f;
	const float charH = pFont->GetMaxCharHeight();
	auto RowH = [&](const FString& line) { return charH * baseScale * (line.IsEmpty() ? 0.625f : 1.25f); };

	//title = line 0, footer = the trailing centered line; the rows between flow into one or
	//two columns - two whenever a single column would run off the bottom (the key list
	//outgrew a 720-tall window), plus a final shrink-to-fit so nothing can ever overflow
	int footerIdx = lines.Num() - 1;
	while (footerIdx > 1 && lines[footerIdx].IsEmpty()) footerIdx--;
	if (footerIdx <= 0 || lines[footerIdx].Contains(TEXT("\t"))) footerIdx = lines.Num(); //no footer

	const int bodyEnd = FMath::Min(footerIdx, lines.Num());
	float bodyH = 0.0f;
	for (int i = 1; i < bodyEnd; i++) bodyH += RowH(lines[i]);
	float titleH = charH * baseScale * titleScaleMult * 1.5f;
	float footerH = (footerIdx < lines.Num()) ? charH * baseScale * 1.25f * 1.5f : 0.0f;
	const float availH = Canvas->ClipY * 0.94f;

	const int numCols = (titleH + bodyH + footerH > availH) ? 2 : 1;

	//two columns: cut where the heights balance best, preferring a blank spacer row nearby
	int splitIdx = bodyEnd;
	if (numCols == 2)
	{
		float acc = 0.0f, bestDiff = MAX_flt, bestEmptyDiff = MAX_flt;
		int best = bodyEnd, bestEmpty = -1;
		for (int i = 2; i < bodyEnd; i++)
		{
			acc += RowH(lines[i - 1]);
			const float diff = FMath::Abs(acc - (bodyH - acc));
			if (diff < bestDiff) { bestDiff = diff; best = i; }
			if (lines[i].IsEmpty() && diff < bestEmptyDiff) { bestEmptyDiff = diff; bestEmpty = i; }
		}
		splitIdx = (bestEmpty >= 0 && bestEmptyDiff <= bestDiff + charH * baseScale * 2.5f) ? bestEmpty : best;
	}

	//measure each column's key/action widths so the columns are exactly as wide as their text
	struct FCol { int start = 0, end = 0; float keyW = 0.0f, actionW = 0.0f, height = 0.0f; };
	FCol cols[2];
	cols[0] = { 1, splitIdx };
	cols[1] = { splitIdx, bodyEnd };
	for (int c = 0; c < numCols; c++)
	{
		FCol& col = cols[c];
		while (col.start < col.end && lines[col.start].IsEmpty()) col.start++; //no spacer at a column edge
		while (col.end > col.start && lines[col.end - 1].IsEmpty()) col.end--;
		for (int i = col.start; i < col.end; i++)
		{
			col.height += RowH(lines[i]);
			FString key, action;
			float w, h;
			if (lines[i].Split(TEXT("\t"), &key, &action))
			{
				Canvas->TextSize(pFont, key, w, h, baseScale, baseScale);
				col.keyW = FMath::Max(col.keyW, w);
				Canvas->TextSize(pFont, action, w, h, baseScale, baseScale);
				col.actionW = FMath::Max(col.actionW, w);
			}
			else if (!lines[i].IsEmpty())
			{
				Canvas->TextSize(pFont, lines[i], w, h, baseScale, baseScale);
				col.actionW = FMath::Max(col.actionW, w); //centered body line spans the column
			}
		}
	}

	float keyGap = charH * baseScale * 0.9f; //between a key and its action
	float colGap = charH * baseScale * 3.0f; //between the two columns
	float totalW = 0.0f, maxColH = 0.0f;
	for (int c = 0; c < numCols; c++)
	{
		totalW += cols[c].keyW + keyGap + cols[c].actionW;
		maxColH = FMath::Max(maxColH, cols[c].height);
	}
	if (numCols == 2) totalW += colGap;
	float totalH = titleH + maxColH + footerH;

	//final shrink-to-fit: every measured width/height scales linearly with the font scale
	const float shrink = FMath::Min(1.0f, FMath::Min(availH / totalH, (Canvas->ClipX * 0.95f) / totalW));
	if (shrink < 1.0f)
	{
		baseScale *= shrink;
		titleH *= shrink; footerH *= shrink; keyGap *= shrink; colGap *= shrink;
		totalW *= shrink; totalH *= shrink; maxColH *= shrink;
		for (int c = 0; c < numCols; c++)
		{
			cols[c].keyW *= shrink; cols[c].actionW *= shrink; cols[c].height *= shrink;
		}
	}

	const FLinearColor keyColor(1.0f, 0.85f, 0.3f);
	const FLinearColor textColor(1.0f, 1.0f, 1.0f);
	const FLinearColor footColor(0.7f, 0.7f, 0.7f);

	//opaque full-screen cover: the game is paused underneath anyway, and letting it show
	//through made both the game and the help hard to read
	FCanvasTileItem cover(FVector2D(0, 0), GWhiteTexture, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor::Black);
	cover.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(cover);

	const float blockX = (Canvas->ClipX - totalW) * 0.5f;
	const float blockY = (Canvas->ClipY - totalH) * 0.5f;

	auto DrawCentered = [&](const FString& text, float y, float scale, const FLinearColor& color)
	{
		float w, h;
		Canvas->TextSize(pFont, text, w, h, scale, scale);
		FCanvasTextItem item(FVector2D((Canvas->ClipX - w) * 0.5f, y), FText::FromString(text), pFont, color);
		item.Scale = FVector2D(scale, scale);
		item.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(item);
	};

	DrawCentered(lines[0], blockY, baseScale * titleScaleMult, keyColor);

	float colX = blockX;
	for (int c = 0; c < numCols; c++)
	{
		const FCol& col = cols[c];
		float y = blockY + titleH;
		for (int i = col.start; i < col.end; i++)
		{
			const FString& line = lines[i];
			const float rowH = RowH(line);
			FString key, action;
			if (line.Split(TEXT("\t"), &key, &action))
			{
				//key right-aligned against the column split, action left-aligned after it
				float w, h;
				Canvas->TextSize(pFont, key, w, h, baseScale, baseScale);
				FCanvasTextItem keyItem(FVector2D(colX + col.keyW - w, y), FText::FromString(key), pFont, keyColor);
				keyItem.Scale = FVector2D(baseScale, baseScale);
				keyItem.EnableShadow(FLinearColor::Black);
				Canvas->DrawItem(keyItem);

				FCanvasTextItem actionItem(FVector2D(colX + col.keyW + keyGap, y), FText::FromString(action), pFont, textColor);
				actionItem.Scale = FVector2D(baseScale, baseScale);
				actionItem.EnableShadow(FLinearColor::Black);
				Canvas->DrawItem(actionItem);
			}
			else if (!line.IsEmpty())
			{
				//centered within the column
				float w, h;
				Canvas->TextSize(pFont, line, w, h, baseScale, baseScale);
				const float colW = col.keyW + keyGap + col.actionW;
				FCanvasTextItem item(FVector2D(colX + (colW - w) * 0.5f, y), FText::FromString(line), pFont, textColor);
				item.Scale = FVector2D(baseScale, baseScale);
				item.EnableShadow(FLinearColor::Black);
				Canvas->DrawItem(item);
			}
			y += rowH;
		}
		colX += col.keyW + keyGap + col.actionW + colGap;
	}

	if (footerIdx < lines.Num())
	{
		DrawCentered(lines[footerIdx], blockY + titleH + maxColH + footerH - charH * baseScale * 1.25f, baseScale, footColor);
	}
}
