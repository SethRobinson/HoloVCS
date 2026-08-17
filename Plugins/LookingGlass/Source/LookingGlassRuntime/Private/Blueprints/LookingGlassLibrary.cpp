#include "Blueprints/LookingGlassLibrary.h"
#include "Misc/LookingGlassLog.h"

#include "LookingGlassSettings.h"


ULookingGlassSettings* ULookingGlassLibrary::GetLookingGlassSettings()
{
	return DuplicateObject(GetDefault<ULookingGlassSettings>(), nullptr);
}