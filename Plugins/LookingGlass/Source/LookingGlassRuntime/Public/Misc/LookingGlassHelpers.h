#pragma once

#include "CoreMinimal.h"

class ULookingGlassSceneCaptureComponent2D;
class IMovieSceneCaptureInterface;
struct FLookingGlassBridge;

namespace LookingGlass
{
	/**
	 * @fn	UMovieSceneCapture* GetMovieSceneCapture();
	 *
	 * @brief	Gets movie scene capture
	 * 			It is using for check movie capture during capturing video
	 *
	 * @returns	Null if it fails, else the movie scene capture.
	 */

	LOOKINGGLASSRUNTIME_API IMovieSceneCaptureInterface* GetMovieSceneCapture();

	LOOKINGGLASSRUNTIME_API TWeakObjectPtr<ULookingGlassSceneCaptureComponent2D> GetGameLookingGlassCaptureComponent();
};
