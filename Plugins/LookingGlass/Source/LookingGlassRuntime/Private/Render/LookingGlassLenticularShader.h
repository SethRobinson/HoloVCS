#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/**
 * Pixel shader that interleaves a quilt into the device's lenticular subpixel pattern.
 * Restored from the legacy HoloPlay plugin so the device image is produced entirely
 * in-process: Bridge is only needed for calibration at boot, never per frame.
 * Pairs with the engine's FScreenVS via IRendererModule::DrawRectangle.
 */
class FLookingGlassLenticularPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FLookingGlassLenticularPS);
	SHADER_USE_PARAMETER_STRUCT(FLookingGlassLenticularPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputTextureSampler)
		SHADER_PARAMETER(float, pitch)
		SHADER_PARAMETER(float, slope)
		SHADER_PARAMETER(float, center)
		SHADER_PARAMETER(float, subp)
		SHADER_PARAMETER(FVector4f, tile)
		SHADER_PARAMETER(FVector2f, viewPortion)
		SHADER_PARAMETER(FVector4f, aspect)
		SHADER_PARAMETER(int32, QuiltMode)
		SHADER_PARAMETER(int32, FlipYTexCoords)
		SHADER_PARAMETER(float, OutputGamma)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};
