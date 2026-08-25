// Fill out your copyright notice in the Description page of Project Settings.

#include "PlayerPawn.h"
#include "Shared/UnrealMisc.h"
#include "LibretroManager.h"
#include "LibretroManagerActor.h"
#include "StatusDisplayActor.h"
#include "HoloHUD.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Components/InputComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"

// Sets default values
APlayerPawn::APlayerPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	//14 degrees at ~1221 units matches the LookingGlass capture's default framing (Size 150, dist = Size/tan(FOV/2)).
	//Move/rotate the pawn in the level to aim it, it's the root component.
	m_pFlatCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FlatCamera"));
	RootComponent = m_pFlatCamera;
	m_pFlatCamera->SetFieldOfView(14.0f);

	ConstructorHelpers::FObjectFinder<UMaterial> newMat(TEXT("/Game/Textures/castlevania_backdrop_Mat"));

	//make sure it was found
	if (newMat.Succeeded())
	{
		m_pPicBG = newMat.Object;
		LogMsg("Found castlevania_backdrop_Mat");
	}
	else
	{
		LogMsg("Error, couldn't find castlevania_backdrop_Mat");
	}

	//The old project's pawn blueprint assigned these in the editor; the ported map lost the
	//references, which left the backdrop wall on WorldGridMaterial (a giant noise wall on the
	//hologram) because SetTintBG built its dynamic instance from a null parent.
	ConstructorHelpers::FObjectFinder<UMaterial> bgMat(TEXT("/Game/Textures/BGLayer"));
	if (bgMat.Succeeded())
	{
		m_pBGMatNormal = bgMat.Object;
	}
	ConstructorHelpers::FObjectFinder<UMaterial> bgMatNS(TEXT("/Game/Textures/BGLayer_NoShadow"));
	if (bgMatNS.Succeeded())
	{
		m_pBGNoShadowMat = bgMatNS.Object;
	}
}

// Called when the game starts or when spawned
void APlayerPawn::BeginPlay()
{
	Super::BeginPlay();

	//swap in our canvas HUD (draws the help screen) - done here at runtime so the plain
	//GameModeBase and the maps stay untouched
	if (APlayerController* pPC = GetWorld()->GetFirstPlayerController())
	{
		pPC->ClientSetHUD(AHoloHUD::StaticClass());
	}
	else
	{
		LogMsg("PlayerPawn: no PlayerController found, help screen HUD not installed");
	}

	auto crap = GetActorByTag(GetWorld(), "LayerBG");
	if (crap == NULL)
	{
		LogMsg("PlayerPawn: no LayerBG actor found at BeginPlay");
	}
	if (crap != NULL)
	{
		m_pMesh = (UStaticMeshComponent*) GetComponentByTag(crap, "StaticMeshComponent");
		if (!m_pMesh)
		{
			//the ported map's LayerBG mesh lost its component tag - any static mesh on it will do
			m_pMesh = crap->FindComponentByClass<UStaticMeshComponent>();
			LogMsg("PlayerPawn: LayerBG mesh via class fallback: %s", m_pMesh ? "found" : "STILL NOT FOUND");
		}
		if (m_pMesh)
		{
			auto pMat = m_pMesh->GetMaterial(0);
			m_pBGMat = UMaterialInstanceDynamic::Create(pMat, NULL);
		}
		else
		{
			LogMsg("Error, couldn't find LayerBG's mesh");
		}
	}
	else
	{
		LogMsg("Found BG layer.");
	}

	//m_pBGMatNoShadow = UMaterialInstanceDynamic::Create(m_pBGNoShadowMat, NULL);
}

void APlayerPawn::SetTintBG(FVector color, float strength, bool bAllowShadows)
{
	
	FindBGMeshIfNeeded();

	if (!g_pLibretroManager || !m_pMesh)
	{
		LogMsg("Error with SetTintBG (manager=%d mesh=%d)", g_pLibretroManager ? 1 : 0, m_pMesh ? 1 : 0);
		return;
	}

	g_pLibretroManager->m_pLibretroManagedActor->m_bg_color = color;
	g_pLibretroManager->m_pLibretroManagedActor->m_bg_color_strength = strength;
	g_pLibretroManager->m_pLibretroManagedActor->m_bgAllowShadows = bAllowShadows;

	auto pMat = m_pMesh->GetMaterial(0);

	UMaterial* pWantedParent = (bAllowShadows && g_pLibretroManager->m_pLibretroManagedActor->m_curLightingMode != LIGHTING_MODE_NONE)
		? m_pBGMatNormal : m_pBGNoShadowMat;
	if (pWantedParent == NULL)
	{
		LogMsg("SetTintBG: BG material reference missing (BGLayer/BGLayer_NoShadow), leaving backdrop alone");
		return;
	}

	UMaterialInstanceDynamic* pMID = Cast<UMaterialInstanceDynamic>(pMat);
	if (pMID == NULL || pMID->GetMaterial() != pWantedParent)
	{
		pMID = UMaterialInstanceDynamic::Create(pWantedParent, NULL);
		m_pMesh->SetMaterial(0, pMID);
	}
	m_pBGMat = pMID;
	m_pBGMat->SetScalarParameterValue(TEXT("TintStrength"), strength);
	m_pBGMat->SetVectorParameterValue("ColorTint", color);
}

void APlayerPawn::FitFlatCameraToLayers()
{
	if (!m_pFlatCamera) return;

	//find the extents of all the spawned layer planes
	TArray<AActor*> actors;
	AddActorsByTag(&actors, GetWorld(), "Layers");
	if (actors.Num() == 0) return;

	FBox box(ForceInit);
	for (int i = 0; i < actors.Num(); i++)
	{
		FVector vOrigin, vExtent;
		actors[i]->GetActorBounds(false, vOrigin, vExtent);
		box += FBox(vOrigin - vExtent, vOrigin + vExtent);
	}

	m_layerBounds = box;
	m_camPivot = box.GetCenter();
	m_bLayerBoundsValid = true;
	m_camDist = 0; //next update snaps straight out to whatever the new stack needs
	UpdateFlatCamera(0.0f);

	FVector vSize = box.GetSize();
	LogMsg("Flat camera framed layers: center %.0f,%.0f,%.0f size %.0f x %.0f, camera dist %.0f",
		m_camPivot.X, m_camPivot.Y, m_camPivot.Z, vSize.Y, vSize.Z, m_camDist);
}

//Distance from the pivot, along the view direction, so the whole layer AABB fits on screen at this
//camera orientation.  UE's FieldOfView is the HORIZONTAL fov (AspectRatio_MaintainXFOV is the engine
//default), vertical follows from the real viewport aspect - the old fit had that backwards and also
//hardcoded 16:9, which is why tall games like Castlevania lost their bottom tiles.
float APlayerPawn::ComputeFlatCameraFitDist(const FRotator& camRot) const
{
	float aspect = 16.0f / 9.0f;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		FIntPoint viewSize = GEngine->GameViewport->Viewport->GetSizeXY();
		if (viewSize.X > 0 && viewSize.Y > 0)
		{
			aspect = (float)viewSize.X / (float)viewSize.Y;
		}
	}

	float tanHalfH = FMath::Tan(FMath::DegreesToRadians(m_pFlatCamera->FieldOfView * 0.5f));
	float tanHalfV = tanHalfH / aspect;

	FRotationMatrix mat(camRot);
	FVector vFwd = mat.GetUnitAxis(EAxis::X);
	FVector vRight = mat.GetUnitAxis(EAxis::Y);
	FVector vUp = mat.GetUnitAxis(EAxis::Z);

	float dist = 10.0f;
	for (int corner = 0; corner < 8; corner++)
	{
		FVector v(
			(corner & 1) ? m_layerBounds.Max.X : m_layerBounds.Min.X,
			(corner & 2) ? m_layerBounds.Max.Y : m_layerBounds.Min.Y,
			(corner & 4) ? m_layerBounds.Max.Z : m_layerBounds.Min.Z);
		FVector vOff = v - m_camPivot;

		float fwd = (float)(vOff | vFwd);
		dist = FMath::Max(dist, (FMath::Abs((float)(vOff | vRight)) * m_flatCameraMargin) / tanHalfH - fwd);
		dist = FMath::Max(dist, (FMath::Abs((float)(vOff | vUp)) * m_flatCameraMargin) / tanHalfV - fwd);
	}

	return dist;
}

void APlayerPawn::UpdateFlatCamera(float DeltaTime)
{
	if (!m_pFlatCamera || !m_bLayerBoundsValid) return;

	float dx = m_mouseDX;
	float dy = m_mouseDY;
	m_mouseDX = m_mouseDY = 0;

	if (FMath::Abs(dx) + FMath::Abs(dy) > 0.02f)
	{
		if (m_manualBlend < 1.0f)
		{
			//grab the camera from wherever the idle sweep left it so there's no pop
			m_manualYaw = m_dispYaw;
			m_manualPitch = m_dispPitch;
			m_manualBlend = 1.0f;
		}
		m_manualYaw = FRotator::NormalizeAxis(m_manualYaw + dx * m_mouseYawSensitivity);
		m_manualPitch = FMath::Clamp(m_manualPitch - dy * m_mousePitchSensitivity, -m_manualPitchLimit, m_manualPitchLimit);
		m_timeSinceMouseMove = 0;
	}
	else
	{
		m_timeSinceMouseMove += DeltaTime;
		if (m_manualBlend > 0 && m_timeSinceMouseMove >= m_idleReturnDelay)
		{
			m_manualBlend = FMath::Max(0.0f, m_manualBlend - DeltaTime / FMath::Max(0.1f, m_returnBlendTime));
		}
	}

	//the idle sweep keeps running underneath the mouse mode so the handback has something live to blend to
	m_autoClock += DeltaTime;
	float autoYaw = FMath::Sin(m_autoClock * (2.0f * PI) / FMath::Max(1.0f, m_autoOrbitPeriod)) * m_autoOrbitYawRange;
	float autoPitch = m_autoOrbitPitch;

	float blend = FMath::SmoothStep(0.0f, 1.0f, m_manualBlend);
	m_dispYaw = FRotator::NormalizeAxis(autoYaw + FRotator::NormalizeAxis(m_manualYaw - autoYaw) * blend);
	m_dispPitch = FMath::Lerp(autoPitch, m_manualPitch, blend);

	FRotator camRot(m_dispPitch, m_dispYaw, 0);
	float wantDist = ComputeFlatCameraFitDist(camRot);

	if (wantDist > m_camDist)
	{
		m_camDist = wantDist; //zoom out instantly, never crop the picture
	}
	else
	{
		m_camDist = FMath::FInterpTo(m_camDist, wantDist, DeltaTime, 0.5f); //ease back in slowly
	}

	m_pFlatCamera->SetWorldLocation(m_camPivot - camRot.Vector() * m_camDist);
	m_pFlatCamera->SetWorldRotation(camRot);
}

void APlayerPawn::FindBGMeshIfNeeded()
{
	//The game profiles can run before our BeginPlay, so the wall mesh is found on demand.
	//The ported map also lost the "StaticMeshComponent" tag, hence the class fallback.
	if (m_pMesh != NULL) return;

	if (auto pBG = GetActorByTag(GetWorld(), "LayerBG"))
	{
		m_pMesh = (UStaticMeshComponent*)GetComponentByTag(pBG, "StaticMeshComponent");
		if (!m_pMesh)
		{
			m_pMesh = pBG->FindComponentByClass<UStaticMeshComponent>();
		}
	}
}

void APlayerPawn::SetBGPic()
{
	//uh, add a way to dynamically load the texture?  Currently it's just a moon
	FindBGMeshIfNeeded();

	if (m_pMesh)
	{
		//LogMsg("Setting custom BG image");

		m_pMesh->SetMaterial(0, m_pPicBG);
	}
	else
	{
		LogMsg("Error, couldn't find LayerBG's mesh");
	}
}

// Called every frame
void APlayerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateFlatCamera(DeltaTime);
	UpdateTouchMouseLock();
}

const float C_JOYSTICK_DEAD_ZONE = 0.3f;

//Any input while the help screen is up only dismisses it.  Call this FIRST in every
//pressed-input handler: true means the help was up (and just closed), so the input is spent
//and the handler's real action must not run (no accidental save states or rom switches from
//the "press any key" dismissal).
static bool HelpSwallowedInput()
{
	if (g_pLibretroManager && g_pLibretroManager->m_helpScreen.IsVisible())
	{
		g_pLibretroManager->m_helpScreen.Hide();
		return true;
	}
	return false;
}

void APlayerPawn::Move_XAxis(float AxisValue)
{
	if (!g_pLibretroManager) return; //axis events fire every frame, even during startup/teardown when there's no manager
	if (FMath::Abs(AxisValue) > C_JOYSTICK_DEAD_ZONE) HelpSwallowedInput(); //movement closes the help like the old splash
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_LEFT] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_RIGHT] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_axisLX = FMath::Clamp(AxisValue, -1.0f, 1.0f); //3DS circle pad
}

void APlayerPawn::Move_YAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	if (FMath::Abs(AxisValue) > C_JOYSTICK_DEAD_ZONE) HelpSwallowedInput();
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_UP] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_DOWN] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_axisLY = FMath::Clamp(AxisValue, -1.0f, 1.0f); //3DS circle pad
}

void APlayerPawn::RMove_XAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R2] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R3] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_axisRX = FMath::Clamp(AxisValue, -1.0f, 1.0f); //3DS C-stick
}

void APlayerPawn::RMove_YAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L3] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L2] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_axisRY = FMath::Clamp(AxisValue, -1.0f, 1.0f); //3DS C-stick
}

void APlayerPawn::OnMouseX(float AxisValue)
{
	//on the 3DS the mouse drives the bottom-screen touch cursor instead of the orbit camera;
	//the cursor is confined to the screen ("locked"), so you can't click off of it
	if (g_pLibretroManager && g_pLibretroManager->m_emulatorType == EMULATOR_3DS)
	{
		if (AxisValue != 0)
		{
			g_pLibretroManager->m_touchX = FMath::Clamp(g_pLibretroManager->m_touchX + AxisValue, 0.0f, 319.0f);
			g_pLibretroManager->m_touchLastActiveTime = FPlatformTime::Seconds();
		}
		return;
	}
	m_mouseDX += AxisValue;
}

void APlayerPawn::OnMouseY(float AxisValue)
{
	if (g_pLibretroManager && g_pLibretroManager->m_emulatorType == EMULATOR_3DS)
	{
		if (AxisValue != 0)
		{
			//UE mouse Y is positive upward; screen coordinates grow downward
			g_pLibretroManager->m_touchY = FMath::Clamp(g_pLibretroManager->m_touchY - AxisValue, 0.0f, 239.0f);
			g_pLibretroManager->m_touchLastActiveTime = FPlatformTime::Seconds();
		}
		return;
	}
	m_mouseDY += AxisValue;
}

//While 3DS touch is active, keep the OS cursor from wandering out of the window (clicks
//outside would defocus the game).  Recenters only when it nears an edge, NOT per frame -
//per-frame cursor clipping is the documented ~90ms-stall trap (see AGENTS.md audio notes).
//UE reads mouse axes from raw input, so the teleport does not create a phantom delta.
void APlayerPawn::UpdateTouchMouseLock()
{
	if (!g_pLibretroManager || g_pLibretroManager->m_emulatorType != EMULATOR_3DS) return;

	APlayerController* pPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!pPC) return;

	float mx = 0, my = 0;
	if (!pPC->GetMousePosition(mx, my)) return; //no focus / no cursor data - leave it alone

	int32 vx = 0, vy = 0;
	pPC->GetViewportSize(vx, vy);
	if (vx < 100 || vy < 100) return;

	const float margin = 60.0f;
	if (mx < margin || my < margin || mx > vx - margin || my > vy - margin)
	{
		pPC->SetMouseLocation(vx / 2, vy / 2);
	}
}

void APlayerPawn::JoyPad_B_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	//on the 3DS a left CLICK means "touch the bottom screen where the big cursor is", not B
	//(Ctrl and the gamepad button still press B there)
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::LeftMouseButton)
	{
		g_pLibretroManager->m_touchDown = true;
		g_pLibretroManager->m_touchLastActiveTime = FPlatformTime::Seconds();
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = true;
}

void APlayerPawn::JoyPad_B_Released(FKey key)
{
	if (key == EKeys::LeftMouseButton)
	{
		g_pLibretroManager->m_touchDown = false;
	}
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::LeftMouseButton)
	{
		return; //the press went to the touchscreen, don't release a B that was never pressed
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = false;
}

void APlayerPawn::JoyPad_A_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_A] = true;
}

void APlayerPawn::JoyPad_A_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_A] = false;
}

void APlayerPawn::JoyPad_Y_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = true;
}

void APlayerPawn::JoyPad_Y_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = false;
}

//X stays disabled for VB (its core uses the id to toggle low battery) but the 3DS needs it
void APlayerPawn::JoyPad_X_Pressed()
{
	if (HelpSwallowedInput()) return;
	if (g_pLibretroManager->m_emulatorType != EMULATOR_VB)
		g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = true;
}

void APlayerPawn::JoyPad_X_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = false;
}

void APlayerPawn::JoyPad_Start_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = true;
}

void APlayerPawn::JoyPad_Start_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = false;
}

void APlayerPawn::JoyPad_Select_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_SELECT] = true;
}

void APlayerPawn::JoyPad_Select_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_SELECT] = false;
}

void APlayerPawn::JoyPad_LShoulder_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L] = true;
	g_pLibretroManager->SaveStateToFile();
}

void APlayerPawn::JoyPad_LShoulder_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L] = false;
}

void APlayerPawn::JoyPad_RShoulder_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R] = true;
	g_pLibretroManager->LoadStateFromFile();
}


void APlayerPawn::JoyPad_LeftStick_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L3] = true;

	OnResetGame();
}
void APlayerPawn::JoyPad_LeftStick_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L3] = false;
}

void APlayerPawn::JoyPad_RightStick_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R3] = true;
	g_pLibretroManager->ModRom(1);

}
void APlayerPawn::JoyPad_RightStick_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R3] = false;
}

void APlayerPawn::JoyPad_RTrigger_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R2] = true;
//	g_pLibretroManager->ModRom(-1);
}

void APlayerPawn::JoyPad_LTrigger_Pressed()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L2] = true;
//	g_pLibretroManager->ModRom(1);
}

void APlayerPawn::JoyPad_RTrigger_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R2] = false;
}

void APlayerPawn::JoyPad_LTrigger_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L2] = false;
}

void APlayerPawn::JoyPad_RShoulder_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R] = false;
}

void APlayerPawn::OnNum0Key()
{
	if (HelpSwallowedInput()) return;
	//toggle every frame limiter (vsync, engine cap and the emulator pacing busy-wait) to see
	//true throughput on the fps counter - the game runs fast while uncapped, like frameskip
	static bool bUncapped = false;
	bUncapped = !bUncapped;
	if (GEngine)
	{
		GEngine->Exec(GetWorld(), bUncapped ? TEXT("r.VSync 0") : TEXT("r.VSync 1"));
		GEngine->Exec(GetWorld(), bUncapped ? TEXT("t.MaxFPS 0") : TEXT("t.MaxFPS 60"));
	}
	if (g_pLibretroManager)
	{
		g_pLibretroManager->m_bUncapFPS = bUncapped;
	}
	ShowStatusMessage(bUncapped ? "FPS cap OFF" : "FPS cap ON (60)");
}

void APlayerPawn::OnNum1Key()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->SetFrameSkip(0);
}
void APlayerPawn::OnNum2Key()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->SetFrameSkip(1);
}
void APlayerPawn::OnNum3Key()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->SetFrameSkip(2);
}
void APlayerPawn::OnNum4Key()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->SetFrameSkip(3);
}
void APlayerPawn::OnNum5Key()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->SetFrameSkip(4);
}
void APlayerPawn::OnNum6Key()
{
	if (HelpSwallowedInput()) return;
	bool bTextureSmoothing = g_pLibretroManager->m_pLibretroManagedActor->GetTextureSmoothingToUse();

	bTextureSmoothing = !bTextureSmoothing;

	g_pLibretroManager->m_pLibretroManagedActor->SetTextureSmoothingToUse(bTextureSmoothing);
	
	if (bTextureSmoothing)
	{
		ShowStatusMessage("Texture smoothing enabled");
	}
	else
	{
		ShowStatusMessage("Texture smoothing disabled");
	}
	g_pLibretroManager->m_pLibretroManagedActor->InitLayers();

}

void APlayerPawn::OnNum7Key()
{
	if (HelpSwallowedInput()) return;
	//The rig's light of record is the point light (the old build's setup); fall back to a
	//directional for maps that only have that. m_pLight is an editor-set property that isn't
	//wired up in every map.
	ULightComponent* pLight = nullptr;
	AActor* pLightActor = g_pLibretroManager->m_pLibretroManagedActor->m_pLight;
	if (pLightActor)
	{
		pLight = pLightActor->FindComponentByClass<UPointLightComponent>();
	}
	if (!pLight)
	{
		for (TActorIterator<APointLight> it(GetWorld()); it; ++it)
		{
			pLight = it->GetLightComponent();
			if (pLight) break;
		}
	}
	if (!pLight)
	{
		for (TActorIterator<ADirectionalLight> it(GetWorld()); it; ++it)
		{
			pLight = it->GetLightComponent();
			if (pLight) break;
		}
	}
	if (!pLight)
	{
		ShowStatusMessage("No light found");
		return;
	}

	if (pLight->CastShadows)
	{
		pLight->SetCastShadows(false);
		ShowStatusMessage("Shadows disabled");
	}
	else
	{
		pLight->SetCastShadows(true);
		ShowStatusMessage("Shadows Enabled");
	}

	g_pLibretroManager->m_pLibretroManagedActor->InitLayers();
}

void APlayerPawn::OnNum8Key()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_pLibretroManagedActor->m_curLightingMode =
		(eLightingMode) (
			((int)g_pLibretroManager->m_pLibretroManagedActor->m_curLightingMode + 1) % (int)LIGHTING_MODE_COUNT
		);

	switch (g_pLibretroManager->m_pLibretroManagedActor->m_curLightingMode)
	{
	case LIGHTING_MODE_NORMAL:
		ShowStatusMessage("Lighting enabled");
		break;
	case LIGHTING_MODE_NONE:
		ShowStatusMessage("Lighting disabled");
		break;
	default:
		check(!"Shit!");
	}

	g_pLibretroManager->m_pLibretroManagedActor->InitLayers();
}

void APlayerPawn::OnPKey()
{
	if (HelpSwallowedInput()) return;
	LogMsg("Pressed P");

	bool bIsPaused = !g_pLibretroManager->GetGamePaused();

	g_pLibretroManager->SetGamePaused(bIsPaused);

	if (bIsPaused)
	{
		ShowStatusMessage("Game paused");
	}
	else
	{
		ShowStatusMessage("Game unpaused");
	}
}

void APlayerPawn::OnAddKey()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_pLibretroManagedActor->ScaleLayersXY(1.05f);
	ShowStatusMessage("Zooming in");
}

void APlayerPawn::OnSubtractKey()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->m_pLibretroManagedActor->ScaleLayersXY(0.95f);
	ShowStatusMessage("Zooming out");
}

//Load state alias (L).  Save state is F only (the JoyPad_LShoulder action, with G / the
//right shoulder loading) - S used to save too, but S is also WASD "down", so every walk
//downward overwrote the save.

void APlayerPawn::OnLKey()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->LoadStateFromFile();
}

void APlayerPawn::OnNKey()
{
	if (!g_pLibretroManager) return;
	if (HelpSwallowedInput()) return;

	if (g_pLibretroManager->m_emulatorType != EMULATOR_NES)
	{
		ShowStatusMessage("NES state dump only works for NES games");
		return;
	}

	g_pLibretroManager->m_bNesDumpRequested = true;
	ShowStatusMessage("Dumping NES state to Saved/nes_state_dump.txt");
}

void APlayerPawn::OnCommaKey()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->ModRom(-1);
}

void APlayerPawn::OnPeriodKey()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->ModRom(1);
}

void APlayerPawn::OnResetGame()
{
	if (HelpSwallowedInput()) return;
	LogMsg("Resetting game");
	g_pLibretroManager->ResetRom();
}

void APlayerPawn::OnLeftBracketKey()
{
	if (!g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
	if (HelpSwallowedInput()) return;
	ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
	//subtract-with-divide hybrid so repeated presses actually REACH 0 instead of only
	//approaching it (SetUserDepthScale snaps the last sliver to a true 0)
	pActor->SetUserDepthScale((pActor->m_userDepthScale / 1.15f) - 0.01f);
}

void APlayerPawn::OnRightBracketKey()
{
	if (!g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor) return;
	if (HelpSwallowedInput()) return;
	ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
	//the additive floor lets ] climb back out of a true-0 flat setting
	pActor->SetUserDepthScale(FMath::Max(pActor->m_userDepthScale * 1.15f, pActor->m_userDepthScale + 0.06f));
}

void APlayerPawn::OnSlashKey()
{
	if (!g_pLibretroManager) return;
	if (HelpSwallowedInput()) return; //? closes it like any other key...
	g_pLibretroManager->m_helpScreen.Show(); //...and opens it when it's not up
}

//AnyKey catch-all so keys with no binding of their own still dismiss the help screen
void APlayerPawn::OnAnyKey()
{
	HelpSwallowedInput();
}

// Called to bind functionality to input
void APlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

#if PLATFORM_ANDROID

	//well, I can confirm this doesn't help at all

	PlayerInputComponent->BindKey(FKey("Equals"), IE_Pressed, this, &APlayerPawn::OnAddKey);
	PlayerInputComponent->BindKey(FKey("Hyphen"), IE_Pressed, this, &APlayerPawn::OnSubtractKey);
	PlayerInputComponent->BindKey(FKey("L"), IE_Pressed, this, &APlayerPawn::OnLKey);
	PlayerInputComponent->BindKey(FKey("Comma"), IE_Pressed, this, &APlayerPawn::OnCommaKey);
	PlayerInputComponent->BindKey(FKey("Period"), IE_Pressed, this, &APlayerPawn::OnPeriodKey);
	PlayerInputComponent->BindKey(FKey("R"), IE_Pressed, this, &APlayerPawn::OnResetGame);
	PlayerInputComponent->BindKey(FKey("Zero"), IE_Pressed, this, &APlayerPawn::OnNum0Key);
	PlayerInputComponent->BindKey(FKey("One"), IE_Pressed, this, &APlayerPawn::OnNum1Key);
	PlayerInputComponent->BindKey(FKey("Two"), IE_Pressed, this, &APlayerPawn::OnNum2Key);
	PlayerInputComponent->BindKey(FKey("Three"), IE_Pressed, this, &APlayerPawn::OnNum3Key);
	PlayerInputComponent->BindKey(FKey("Four"), IE_Pressed, this, &APlayerPawn::OnNum4Key);
	PlayerInputComponent->BindKey(FKey("Five"), IE_Pressed, this, &APlayerPawn::OnNum5Key);
	PlayerInputComponent->BindKey(FKey("Six"), IE_Pressed, this, &APlayerPawn::OnNum6Key);
	PlayerInputComponent->BindKey(FKey("Seven"), IE_Pressed, this, &APlayerPawn::OnNum7Key);
	PlayerInputComponent->BindKey(FKey("Eight"), IE_Pressed, this, &APlayerPawn::OnNum8Key);
	PlayerInputComponent->BindKey(FKey("P"), IE_Pressed, this, &APlayerPawn::OnPKey);
	PlayerInputComponent->BindKey(FKey("N"), IE_Pressed, this, &APlayerPawn::OnNKey);
	PlayerInputComponent->BindKey(FKey("LeftBracket"), IE_Pressed, this, &APlayerPawn::OnLeftBracketKey);
	PlayerInputComponent->BindKey(FKey("LeftBracket"), IE_Repeat, this, &APlayerPawn::OnLeftBracketKey);
	PlayerInputComponent->BindKey(FKey("RightBracket"), IE_Pressed, this, &APlayerPawn::OnRightBracketKey);
	PlayerInputComponent->BindKey(FKey("RightBracket"), IE_Repeat, this, &APlayerPawn::OnRightBracketKey);
	PlayerInputComponent->BindKey(FKey("Slash"), IE_Pressed, this, &APlayerPawn::OnSlashKey);
	PlayerInputComponent->BindKey(FKey("AnyKey"), IE_Pressed, this, &APlayerPawn::OnAnyKey).bConsumeInput = false;
#else
	PlayerInputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &APlayerPawn::OnAddKey);
	PlayerInputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &APlayerPawn::OnSubtractKey);
	PlayerInputComponent->BindKey(EKeys::L, IE_Pressed, this, &APlayerPawn::OnLKey);
	PlayerInputComponent->BindKey(EKeys::Comma, IE_Pressed, this, &APlayerPawn::OnCommaKey);
	PlayerInputComponent->BindKey(EKeys::Period, IE_Pressed, this, &APlayerPawn::OnPeriodKey);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &APlayerPawn::OnResetGame);
	PlayerInputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &APlayerPawn::OnNum0Key);
	PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &APlayerPawn::OnNum1Key);
	PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &APlayerPawn::OnNum2Key);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &APlayerPawn::OnNum3Key);
	PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &APlayerPawn::OnNum4Key);
	PlayerInputComponent->BindKey(EKeys::Five, IE_Pressed, this, &APlayerPawn::OnNum5Key);
	PlayerInputComponent->BindKey(EKeys::Six, IE_Pressed, this, &APlayerPawn::OnNum6Key);
	PlayerInputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &APlayerPawn::OnNum7Key);
	PlayerInputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &APlayerPawn::OnNum8Key);
	PlayerInputComponent->BindKey(EKeys::P, IE_Pressed, this, &APlayerPawn::OnPKey);
	PlayerInputComponent->BindKey(EKeys::N, IE_Pressed, this, &APlayerPawn::OnNKey);
	PlayerInputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &APlayerPawn::OnLeftBracketKey);
	PlayerInputComponent->BindKey(EKeys::LeftBracket, IE_Repeat, this, &APlayerPawn::OnLeftBracketKey);
	PlayerInputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &APlayerPawn::OnRightBracketKey);
	PlayerInputComponent->BindKey(EKeys::RightBracket, IE_Repeat, this, &APlayerPawn::OnRightBracketKey);
	PlayerInputComponent->BindKey(EKeys::Slash, IE_Pressed, this, &APlayerPawn::OnSlashKey); //Slash also fires with Shift held, so ? works
	//"press any key to close" for the help screen - keys without a binding of their own land
	//here.  MUST NOT consume, or it would eat every keypress meant for the game/hotkeys.
	//(The help's same-frame show/hide guards keep this from fighting the ? toggle.)
	PlayerInputComponent->BindKey(EKeys::AnyKey, IE_Pressed, this, &APlayerPawn::OnAnyKey).bConsumeInput = false;
#endif
	//mouse orbits the flat camera around the layer diorama, see UpdateFlatCamera
	InputComponent->BindAxisKey(EKeys::MouseX, this, &APlayerPawn::OnMouseX);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &APlayerPawn::OnMouseY);

	// Respond every frame to the values of our two movement axes, "MoveX" and "MoveY".
	InputComponent->BindAxis("MoveX", this, &APlayerPawn::Move_XAxis);
	InputComponent->BindAxis("MoveY", this, &APlayerPawn::Move_YAxis);
	InputComponent->BindAxis("RMoveX", this, &APlayerPawn::RMove_XAxis);
	InputComponent->BindAxis("RMoveY", this, &APlayerPawn::RMove_YAxis);
	InputComponent->BindAction("JoyPad_A", IE_Pressed, this, &APlayerPawn::JoyPad_A_Pressed);
	InputComponent->BindAction("JoyPad_A", IE_Released, this, &APlayerPawn::JoyPad_A_Released);
	InputComponent->BindAction("JoyPad_B", IE_Pressed, this, &APlayerPawn::JoyPad_B_Pressed);
	InputComponent->BindAction("JoyPad_B", IE_Released, this, &APlayerPawn::JoyPad_B_Released);
	InputComponent->BindAction("JoyPad_Y", IE_Pressed, this, &APlayerPawn::JoyPad_Y_Pressed);
	InputComponent->BindAction("JoyPad_Y", IE_Released, this, &APlayerPawn::JoyPad_Y_Released);
	InputComponent->BindAction("JoyPad_X", IE_Pressed, this, &APlayerPawn::JoyPad_X_Pressed);
	InputComponent->BindAction("JoyPad_X", IE_Released, this, &APlayerPawn::JoyPad_X_Released);
	InputComponent->BindAction("JoyPad_Start", IE_Pressed, this, &APlayerPawn::JoyPad_Start_Pressed);
	InputComponent->BindAction("JoyPad_Start", IE_Released, this, &APlayerPawn::JoyPad_Start_Released);
	InputComponent->BindAction("JoyPad_Select", IE_Pressed, this, &APlayerPawn::JoyPad_Select_Pressed);
	InputComponent->BindAction("JoyPad_Select", IE_Released, this, &APlayerPawn::JoyPad_Select_Released);
	InputComponent->BindAction("JoyPad_LShoulder", IE_Pressed, this, &APlayerPawn::JoyPad_LShoulder_Pressed);
	InputComponent->BindAction("JoyPad_LShoulder", IE_Released, this, &APlayerPawn::JoyPad_LShoulder_Released);
	InputComponent->BindAction("JoyPad_RShoulder", IE_Pressed, this, &APlayerPawn::JoyPad_RShoulder_Pressed);
	InputComponent->BindAction("JoyPad_RShoulder", IE_Released, this, &APlayerPawn::JoyPad_RShoulder_Released);
	InputComponent->BindAction("ActionOnRButton", IE_Pressed, this, &APlayerPawn::OnResetGame);
	InputComponent->BindAction("JoyPad_RTrigger", IE_Pressed, this, &APlayerPawn::JoyPad_RTrigger_Pressed);
	InputComponent->BindAction("JoyPad_LTrigger", IE_Pressed, this, &APlayerPawn::JoyPad_LTrigger_Pressed);

    InputComponent->BindAction("JoyPad_RTrigger", IE_Released, this, &APlayerPawn::JoyPad_RTrigger_Released);
    InputComponent->BindAction("JoyPad_LTrigger", IE_Released, this, &APlayerPawn::JoyPad_LTrigger_Released);


    InputComponent->BindAction("JoyPad_LeftStickButton", IE_Pressed, this, &APlayerPawn::JoyPad_LeftStick_Pressed);
    InputComponent->BindAction("JoyPad_LeftStickButton", IE_Released, this, &APlayerPawn::JoyPad_LeftStick_Released);
    InputComponent->BindAction("JoyPad_RightStickButton", IE_Pressed, this, &APlayerPawn::JoyPad_RightStick_Pressed);
    InputComponent->BindAction("JoyPad_RightStickButton", IE_Released, this, &APlayerPawn::JoyPad_RightStick_Released);

	//I only bound things for Atari and NES.   It's setup for gamepad, arrow keys, and WASD.  Enter or Start on the controller for game reset.

	//well now, I guess I need a few more things for the NES too

	//I didn't bother to bind actions in the editor for this, this is code only stuff


}

