#include "LookingGlassSettings.h"
#include "ILookingGlassRuntime.h"
#include "Misc/LookingGlassLog.h"

#include "Engine/Engine.h"

#include "Runtime/Launch/Resources/Version.h"

void FLookingGlassRenderingSettings::UpdateVsync() const
{
	if (GEngine)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		bool CVarbVsync = CVar->GetValueOnGameThread() != 0;
		if (CVarbVsync == bVsync)
		{
			return;
		}

		if (bVsync)
		{
			new(GEngine->DeferredCommands) FString(TEXT("r.vsync 1"));
		}
		else
		{
			new(GEngine->DeferredCommands) FString(TEXT("r.vsync 0"));
		}
	}
}

FLookingGlassTilingQuality ULookingGlassSettings::GetTilingQualityFor(ELookingGlassQualitySettings TilingSettings) const
{
	FLookingGlassTilingQuality TilingValues;

	switch (TilingSettings)
	{
	case ELookingGlassQualitySettings::Q_Portrait:
		TilingValues = PortraitSettings;
		break;
	case ELookingGlassQualitySettings::Q_FourK:
		TilingValues = _16In_Settings;
		break;
	case ELookingGlassQualitySettings::Q_EightK:
		TilingValues = _32In_Settings;
		break;
	case ELookingGlassQualitySettings::Q_65_Inch:
		TilingValues = _65In_Settings;
		break;
	case ELookingGlassQualitySettings::Q_Prototype:
		TilingValues = _Prototype_Settings;
		break;
	case ELookingGlassQualitySettings::Q_GoPortrait:
		TilingValues = _GoPortrait_Settings;
		break;
	case ELookingGlassQualitySettings::Q_Kiosk:
		TilingValues = _Kiosk_Settings;
		break;
	case ELookingGlassQualitySettings::Q_16_Portrait:
		TilingValues = _16In_Portrait_Settings;
		break;
	case ELookingGlassQualitySettings::Q_16_Landscape:
		TilingValues = _16In_Landscape_Settings;
		break;
	case ELookingGlassQualitySettings::Q_32_Portrait:
		TilingValues = _32In_Portrait_Settings;
		break;
	case ELookingGlassQualitySettings::Q_32_Landscape:
		TilingValues = _32In_Landscape_Settings;
		break;
	case ELookingGlassQualitySettings::Q_EightPointNineLegacy:
		TilingValues = _8_9inLegacy_Settings;
		break;
	case ELookingGlassQualitySettings::Q_Custom:
		TilingValues = CustomSettings;
		break;
	}

	// Finalize setup of TilingValues
	TilingValues.Setup();

	return TilingValues;
}

#if WITH_EDITOR
void ULookingGlassSettings::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		FName PropertyName(PropertyChangedEvent.Property->GetFName());
		FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != NULL) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

		if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ULookingGlassSettings, CustomSettings))
		{
			// Changed custom values, recompute other fields
			CustomSettings.Setup();
		}
	}
}
#endif // WITH_EDITOR

void ULookingGlassSettings::LookingGlassSave()
{
	LookingGlassRenderingSettings.UpdateVsync();

#if WITH_EDITOR
	#if ENGINE_MAJOR_VERSION >= 5
	this->TryUpdateDefaultConfigFile();
	#else
	this->UpdateDefaultConfigFile();
#endif
#else
	this->SaveConfig();
#endif
}
