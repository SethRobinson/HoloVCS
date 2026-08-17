// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LookingGlassSettings.h"

#include "RHI.h"
#include "Components/SceneCaptureComponent.h"

class FViewport;
class FTextureRenderTargetResource;
class FTextureResource;

namespace LookingGlass
{
	struct FCopyToQuiltRenderContext
	{
		const FTextureRenderTargetResource* QuiltTargetResource;
		FLookingGlassTilingQuality TilingValues;
		const FTextureResource* TilingTextureResource;
		int32 CurrentViewIndex;
		int32 ViewInfoIndex;
		int32 TotalViews;
		int32 ViewRows;
		int32 ViewColumns;
		FSceneCaptureViewInfo CaptureViewInfo;
	};

	/**
	 * @fn	void CopyToQuiltShader_RenderThread(FRHICommandListImmediate& RHICmdList, const FRenderContext& Context, int CurrentView);
	 *
	 * @brief	Copies to quilt
	 * 			It is copy one by one texture to the quilt render target from tiling texture
	 *
	 * @param [in,out]	RHICmdList 	List of rhi commands.
	 * @param 		  	Context	   	The context.
	 * @param 		  	ViewIndex	The current view.
	 */

	void CopyToQuiltShader_RenderThread(FRHICommandListImmediate& RHICmdList, const FCopyToQuiltRenderContext& Context);
}
