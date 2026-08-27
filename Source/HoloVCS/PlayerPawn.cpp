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
#include "HAL/IConsoleManager.h"

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
	//while the fly camera is out, keep tracking bounds (exit handback and fly speed use them)
	//but don't yank the camera - layer rebuilds land here on every depth ramp tick
	if (m_bFlyCam) return;
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
	if (!m_pFlatCamera) return;

	//debug fly mode owns the camera outright - the orbit/fit machinery below would stomp it
	if (m_bFlyCam)
	{
		UpdateFlyCamera(DeltaTime);
		return;
	}

	if (!m_bLayerBoundsValid) return;

	float dx = m_mouseDX;
	float dy = m_mouseDY;
	m_mouseDX = m_mouseDY = 0;

	if (FMath::Abs(dx) + FMath::Abs(dy) > 0.02f)
	{
		if (m_camScript != ECamScript::None)
		{
			//the user grabbed the mouse mid-script: the script yields exactly where it is
			m_camScript = ECamScript::None;
			m_manualYaw = m_dispYaw;
			m_manualPitch = m_dispPitch;
			m_manualBlend = 1.0f;
			LogMsg("Camera script cancelled by mouse");
		}
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

	if (m_camScript != ECamScript::None)
	{
		//a scripted GIF move drives the displayed angles directly; the shared fit/transform
		//code below keeps the framing correct at every angle for free
		m_scriptT += DeltaTime;
		switch (m_camScript)
		{
		case ECamScript::Sweep:
		{
			float alpha = FMath::Clamp(m_scriptT / m_scriptDur, 0.0f, 1.0f);
			m_dispYaw = FRotator::NormalizeAxis(FMath::Lerp(m_scriptA, m_scriptB, alpha));
			m_dispPitch = m_scriptPitch;
			if (m_scriptT >= m_scriptDur) FinishCamScript();
			break;
		}
		case ECamScript::Pose:
		{
			float alpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(m_scriptT / m_scriptDur, 0.0f, 1.0f));
			m_dispYaw = FRotator::NormalizeAxis(m_scriptStartYaw + FRotator::NormalizeAxis(m_scriptA - m_scriptStartYaw) * alpha);
			m_dispPitch = FMath::Lerp(m_scriptStartPitch, m_scriptB, alpha);
			if (m_scriptT >= m_scriptDur) FinishCamScript();
			break;
		}
		case ECamScript::Wiggle:
		{
			m_dispYaw = FRotator::NormalizeAxis(m_scriptStartYaw + m_scriptA * FMath::Sin(m_scriptT * (2.0f * PI) / m_scriptDur));
			m_dispPitch = m_scriptPitch;
			//whole cycles end at sin=0, exactly where they started = a seamless GIF loop
			if (m_scriptCycles > 0 && m_scriptT >= m_scriptCycles * m_scriptDur)
			{
				m_dispYaw = m_scriptStartYaw;
				FinishCamScript();
			}
			break;
		}
		default:
			break;
		}
	}
	else
	{
		float blend = FMath::SmoothStep(0.0f, 1.0f, m_manualBlend);
		m_dispYaw = FRotator::NormalizeAxis(autoYaw + FRotator::NormalizeAxis(m_manualYaw - autoYaw) * blend);
		m_dispPitch = FMath::Lerp(autoPitch, m_manualPitch, blend);
	}

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

//Free-fly debug camera: left stick moves, right stick looks, triggers rise/sink, LB/RB set
//speed.  The mouse also looks on non-3DS systems (on the 3DS it stays the touch cursor and
//never reaches m_mouseDX/DY).  All speeds scale by the layer AABB so NES (~41 units) and
//VB (~310) feel the same.
void APlayerPawn::UpdateFlyCamera(float DeltaTime)
{
	//consume the mouse accumulators even if unused - left pooling, they'd yank the orbit
	//into manual mode with one giant delta the moment fly mode exits
	float dx = m_mouseDX;
	float dy = m_mouseDY;
	m_mouseDX = m_mouseDY = 0;

	//m_padRY sign matches the hardware-verified touch-cursor math in UpdateTouchMouseLock
	//(positive = stick up after the ini's -1 scale), so += looks up on stick up.  Mouse
	//forward = look up, standard FPS sense.
	m_flyYaw = FRotator::NormalizeAxis(m_flyYaw + m_padRX * m_flyLookYawSpeed * DeltaTime + dx * m_mouseYawSensitivity);
	m_flyPitch = FMath::Clamp(m_flyPitch + m_padRY * m_flyLookPitchSpeed * DeltaTime + dy * m_mousePitchSensitivity,
		-m_flyPitchLimit, m_flyPitchLimit);

	const float boundsMax = m_bLayerBoundsValid ? (float)m_layerBounds.GetSize().GetMax() : 200.0f;
	const float moveSpeed = m_flyMoveSpeedFactor * boundsMax * m_flySpeedMult;

	FRotator camRot(m_flyPitch, m_flyYaw, 0);
	FRotationMatrix mat(camRot);
	m_flyPos += mat.GetUnitAxis(EAxis::X) * (-m_padLY) * moveSpeed * DeltaTime; //stick up = forward (LY carries the ini's -1 scale, up = -1 like W)
	m_flyPos += mat.GetUnitAxis(EAxis::Y) * m_padLX * moveSpeed * DeltaTime;
	m_flyPos.Z += (m_padRT - m_padLT) * m_flyVerticalSpeedFactor * boundsMax * m_flySpeedMult * DeltaTime;

	m_pFlatCamera->SetWorldLocation(m_flyPos);
	m_pFlatCamera->SetWorldRotation(camRot);
}

void APlayerPawn::SetFlyCamEnabled(bool bEnable)
{
	if (bEnable == m_bFlyCam) return;

	if (bEnable)
	{
		m_camScript = ECamScript::None; //fly and the scripted moves both want the camera (a depth ramp may keep running)
		//seed from the live camera so there's no pop
		if (m_pFlatCamera)
		{
			m_flyPos = m_pFlatCamera->GetComponentLocation();
		}
		m_flyYaw = m_dispYaw;
		m_flyPitch = m_dispPitch;
		m_flySpeedMult = 1.0f;
		//the pad flies the camera now - release everything it was holding in the game.
		//(A keyboard BUTTON held across the toggle is lost until re-pressed - acceptable;
		//keyboard axes re-assert next frame, and harness `press` holds live elsewhere.)
		if (g_pLibretroManager)
		{
			for (int i = 0; i < C_MAX_JOYPAD_BUTTONS; i++)
			{
				g_pLibretroManager->m_joyPad.m_button[i] = false;
			}
			g_pLibretroManager->m_joyPad.m_axisLX = 0;
			g_pLibretroManager->m_joyPad.m_axisLY = 0;
			g_pLibretroManager->m_joyPad.m_axisRX = 0;
			g_pLibretroManager->m_joyPad.m_axisRY = 0;
		}
		m_bFlyCam = true;
		ShowStatusMessage("Fly camera ON (Start+L-stick click or V to exit)");
	}
	else
	{
		m_bFlyCam = false;
		//hand back to the orbit along the ray we're already on: aim at the pivot from here,
		//keep the current distance, and the fit logic walks it to whatever it needs
		FVector toPivot = m_camPivot - m_flyPos;
		if (m_bLayerBoundsValid && toPivot.Size() > 1.0f)
		{
			FRotator r = toPivot.Rotation();
			m_manualYaw = (float)r.Yaw;
			m_manualPitch = FMath::Clamp((float)r.Pitch, -m_manualPitchLimit, m_manualPitchLimit);
			m_manualBlend = 1.0f;
			m_timeSinceMouseMove = 0;
			m_camDist = (float)toPivot.Size();
		}
		ShowStatusMessage("Fly camera OFF");
	}
	LogMsg("Fly camera %s", m_bFlyCam ? "ON" : "OFF");
}

//---- Scripted camera moves for GIF capture.  Feedback is log-only on purpose: a status
//message would render into every captured frame. ----

void APlayerPawn::FinishCamScript()
{
	m_camScript = ECamScript::None;
	//hand off like a mouse release: hold the final angles, the idle-return machinery
	//eases back to the sweep after m_idleReturnDelay
	m_manualYaw = m_dispYaw;
	m_manualPitch = m_dispPitch;
	m_manualBlend = 1.0f;
	m_timeSinceMouseMove = 0;
	LogMsg("Camera script done");
}

void APlayerPawn::StartCamSweep(float yawA, float yawB, float seconds, float pitch)
{
	SetFlyCamEnabled(false);
	m_camScript = ECamScript::Sweep;
	m_scriptT = 0;
	m_scriptDur = FMath::Max(0.1f, seconds);
	m_scriptA = yawA;
	m_scriptB = yawB;
	m_scriptPitch = (pitch >= CAM_PITCH_KEEP) ? m_dispPitch : pitch;
	LogMsg("holo.CamSweep: yaw %.1f -> %.1f over %.1fs, pitch %.1f", yawA, yawB, m_scriptDur, m_scriptPitch);
}

void APlayerPawn::StartCamPose(float yaw, float pitch, float seconds)
{
	SetFlyCamEnabled(false);
	m_camScript = ECamScript::Pose;
	m_scriptT = 0;
	m_scriptDur = FMath::Max(0.1f, seconds);
	m_scriptA = yaw;
	m_scriptB = pitch;
	m_scriptStartYaw = m_dispYaw;
	m_scriptStartPitch = m_dispPitch;
	LogMsg("holo.CamPose: to yaw %.1f pitch %.1f over %.1fs", yaw, pitch, m_scriptDur);
}

void APlayerPawn::StartCamWiggle(float amplitude, float period, float cycles, float pitch)
{
	SetFlyCamEnabled(false);
	m_camScript = ECamScript::Wiggle;
	m_scriptT = 0;
	m_scriptDur = FMath::Max(0.1f, period);
	m_scriptA = amplitude;
	m_scriptCycles = FMath::Max(0.0f, cycles);
	m_scriptStartYaw = m_dispYaw;
	m_scriptPitch = (pitch >= CAM_PITCH_KEEP) ? m_dispPitch : pitch;
	LogMsg("holo.CamWiggle: +/-%.1f deg, period %.1fs, cycles %.0f", amplitude, m_scriptDur, m_scriptCycles);
}

void APlayerPawn::StartDepthRamp(float from, float to, float seconds)
{
	SetFlyCamEnabled(false);
	m_depthRampFrom = from;
	m_depthRampTo = to;
	m_depthRampDur = FMath::Max(0.1f, seconds);
	m_depthRampT = 0;
	m_depthRampActive = true;
	//snap to the start value now so a recording can begin on the very first frame
	if (g_pLibretroManager && g_pLibretroManager->m_pLibretroManagedActor)
	{
		g_pLibretroManager->m_pLibretroManagedActor->SetUserDepthScale(from, false);
	}
	LogMsg("holo.DepthRamp: %.2f -> %.2f over %.1fs", from, to, m_depthRampDur);
}

void APlayerPawn::StopCamScripts()
{
	if (m_camScript != ECamScript::None)
	{
		FinishCamScript(); //settles at the current angles, no snap
	}
	m_depthRampActive = false;
}

void APlayerPawn::UpdateDepthRamp(float DeltaTime)
{
	if (!m_depthRampActive) return;
	if (!g_pLibretroManager || !g_pLibretroManager->m_pLibretroManagedActor)
	{
		m_depthRampActive = false;
		return;
	}
	m_depthRampT += DeltaTime;
	float alpha = FMath::Clamp(m_depthRampT / m_depthRampDur, 0.0f, 1.0f);
	g_pLibretroManager->m_pLibretroManagedActor->SetUserDepthScale(
		FMath::Lerp(m_depthRampFrom, m_depthRampTo, alpha), false); //silent - no status text in GIF frames
	if (alpha >= 1.0f)
	{
		m_depthRampActive = false;
		LogMsg("holo.DepthRamp done at %.2f", m_depthRampTo);
	}
}

static APlayerPawn* GetHoloPawn()
{
	return g_pLibretroManager ? g_pLibretroManager->m_pPlayerPawn : nullptr;
}

//Console twins so the automation harness can drive all of this headlessly (exec holo.CamSweep ...).
//The harness can `exec` but cannot press a gamepad chord.
static FAutoConsoleCommand CCmdHoloFlyCam(
	TEXT("holo.FlyCam"),
	TEXT("Toggle the debug fly camera (same as V / Start+L-stick click). Usage: holo.FlyCam [0|1]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		APlayerPawn* pPawn = GetHoloPawn();
		if (!pPawn) return;
		bool bEnable = args.Num() > 0 ? (FCString::Atoi(*args[0]) != 0) : !pPawn->IsFlyCamEnabled();
		pPawn->SetFlyCamEnabled(bEnable);
	}));

static FAutoConsoleCommand CCmdHoloCamSweep(
	TEXT("holo.CamSweep"),
	TEXT("Linear orbit yaw sweep for GIF capture. Usage: holo.CamSweep <yawA> <yawB> <seconds> [pitch]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		APlayerPawn* pPawn = GetHoloPawn();
		if (!pPawn || args.Num() < 3) return;
		pPawn->StartCamSweep(FCString::Atof(*args[0]), FCString::Atof(*args[1]), FCString::Atof(*args[2]),
			args.Num() > 3 ? FCString::Atof(*args[3]) : APlayerPawn::CAM_PITCH_KEEP);
	}));

static FAutoConsoleCommand CCmdHoloCamPose(
	TEXT("holo.CamPose"),
	TEXT("Ease the orbit camera to a pose. Usage: holo.CamPose <yaw> <pitch> <seconds>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		APlayerPawn* pPawn = GetHoloPawn();
		if (!pPawn || args.Num() < 3) return;
		pPawn->StartCamPose(FCString::Atof(*args[0]), FCString::Atof(*args[1]), FCString::Atof(*args[2]));
	}));

static FAutoConsoleCommand CCmdHoloCamWiggle(
	TEXT("holo.CamWiggle"),
	TEXT("Parallax yaw oscillation; whole cycles loop seamlessly. Usage: holo.CamWiggle <amplitudeDeg> <periodSec> [cycles] [pitch]  (cycles 0 = until holo.CamStop)"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		APlayerPawn* pPawn = GetHoloPawn();
		if (!pPawn || args.Num() < 2) return;
		pPawn->StartCamWiggle(FCString::Atof(*args[0]), FCString::Atof(*args[1]),
			args.Num() > 2 ? FCString::Atof(*args[2]) : 0.0f,
			args.Num() > 3 ? FCString::Atof(*args[3]) : APlayerPawn::CAM_PITCH_KEEP);
	}));

static FAutoConsoleCommand CCmdHoloDepthRamp(
	TEXT("holo.DepthRamp"),
	TEXT("Animate the 3D depth spread (the [ ] value) for GIF capture, status text suppressed. Usage: holo.DepthRamp <from> <to> <seconds>  (0 = flat; values snap to 0 below 0.05)"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		APlayerPawn* pPawn = GetHoloPawn();
		if (!pPawn || args.Num() < 3) return;
		pPawn->StartDepthRamp(FCString::Atof(*args[0]), FCString::Atof(*args[1]), FCString::Atof(*args[2]));
	}));

static FAutoConsoleCommand CCmdHoloCamStop(
	TEXT("holo.CamStop"),
	TEXT("Cancel any running camera script and depth ramp."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
	{
		APlayerPawn* pPawn = GetHoloPawn();
		if (!pPawn) return;
		pPawn->StopCamScripts();
	}));

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

	UpdateDepthRamp(DeltaTime); //before the camera update so the fit sees the freshly moved layers
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
	if (m_bFlyCam) AxisValue -= m_padLX; //strip the gamepad stick (it flies the camera); keyboard keeps playing
	if (FMath::Abs(AxisValue) > C_JOYSTICK_DEAD_ZONE) HelpSwallowedInput(); //movement closes the help like the old splash
	m_moveAxisX = AxisValue;
	UpdateDpadButtons();
	g_pLibretroManager->m_joyPad.m_axisLX = FMath::Clamp(AxisValue, -1.0f, 1.0f); //3DS circle pad
}

void APlayerPawn::Move_YAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	if (m_bFlyCam) AxisValue -= m_padLY;
	if (FMath::Abs(AxisValue) > C_JOYSTICK_DEAD_ZONE) HelpSwallowedInput();
	m_moveAxisY = AxisValue;
	UpdateDpadButtons();
	g_pLibretroManager->m_joyPad.m_axisLY = FMath::Clamp(AxisValue, -1.0f, 1.0f); //3DS circle pad
}

void APlayerPawn::DPad_XAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	if (m_bFlyCam) AxisValue = 0; //gamepad-only keys, the game isn't listening to the pad now
	if (FMath::Abs(AxisValue) > C_JOYSTICK_DEAD_ZONE) HelpSwallowedInput();
	m_dpadAxisX = AxisValue;
	UpdateDpadButtons();
}

void APlayerPawn::DPad_YAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	if (m_bFlyCam) AxisValue = 0;
	if (FMath::Abs(AxisValue) > C_JOYSTICK_DEAD_ZONE) HelpSwallowedInput();
	m_dpadAxisY = AxisValue;
	UpdateDpadButtons();
}

//Recorders for the gamepad-only mirror axes (PadLX..PadRT).  Bound BEFORE the merged
//Move/RMove axes so the values are fresh when those handlers subtract them in fly mode.
void APlayerPawn::Pad_LX(float v) { m_padLX = v; }
void APlayerPawn::Pad_LY(float v) { m_padLY = v; }
void APlayerPawn::Pad_RX(float v) { m_padRX = v; }
void APlayerPawn::Pad_RY(float v) { m_padRY = v; }
void APlayerPawn::Pad_LT(float v) { m_padLT = v; }
void APlayerPawn::Pad_RT(float v) { m_padRT = v; }

void APlayerPawn::UpdateDpadButtons()
{
	if (!g_pLibretroManager) return;
	//On the 3DS the circle pad and the d-pad are SEPARATE controls (SM3DL: pad moves Mario,
	//d-pad turns the camera), so the stick/keys must never set the digital bits there - they
	//already reach the game through the analog axes.  Every other core only has a d-pad, so
	//stick, keys and d-pad all merge into it like before.
	float x = m_dpadAxisX;
	float y = m_dpadAxisY;
	if (g_pLibretroManager->m_emulatorType != EMULATOR_3DS)
	{
		if (FMath::Abs(m_moveAxisX) > FMath::Abs(x)) x = m_moveAxisX;
		if (FMath::Abs(m_moveAxisY) > FMath::Abs(y)) y = m_moveAxisY;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_LEFT] = (x < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_RIGHT] = (x > C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_UP] = (y < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_DOWN] = (y > C_JOYSTICK_DEAD_ZONE);
}

void APlayerPawn::RMove_XAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	//fly mode: the right stick looks around, so only the keyboard share (H/K) reaches the
	//game - this also keeps the stick out of the 3DS touch cursor and the digital bits below
	if (m_bFlyCam) AxisValue -= m_padRX;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R2] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R3] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_axisRX = FMath::Clamp(AxisValue, -1.0f, 1.0f); //3DS C-stick
}

void APlayerPawn::RMove_YAxis(float AxisValue)
{
	if (!g_pLibretroManager) return;
	if (m_bFlyCam) AxisValue -= m_padRY;
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
			const float sensitivity = 7.5f; //Seth-tuned: 2.5 still felt sluggish
			g_pLibretroManager->m_touchX = FMath::Clamp(g_pLibretroManager->m_touchX + AxisValue * sensitivity, 0.0f, 319.0f);
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
			const float sensitivity = 7.5f;
			g_pLibretroManager->m_touchY = FMath::Clamp(g_pLibretroManager->m_touchY - AxisValue * sensitivity, 0.0f, 239.0f);
			g_pLibretroManager->m_touchLastActiveTime = FPlatformTime::Seconds();
		}
		return;
	}
	m_mouseDY += AxisValue;
}

//While the 3DS is active and our window is foreground, hard-confine the OS cursor to the
//window (Win32 ClipCursor) so a click can never land on the desktop and steal focus.
//The clip is applied only when Windows has dropped it (focus changes clear clips), NOT
//re-applied every frame - the per-mousemove re-clip is the documented ~90ms-stall trap
//(see AGENTS.md audio notes); the STALL watchdog in the log would catch a regression.
void HoloConfineMouseToGameWindow(bool bEnable); //defined in LibretroManager.cpp (has the windows.h wrapper)
void APlayerPawn::UpdateTouchMouseLock()
{
	const bool b3DS = g_pLibretroManager && g_pLibretroManager->m_emulatorType == EMULATOR_3DS;
	HoloConfineMouseToGameWindow(b3DS);

	//the RIGHT STICK also drives the touch cursor (gamepad touchscreen mode) - it and the
	//mouse move the same cursor, so "whichever was used last" wins automatically
	if (b3DS)
	{
		const float rx = g_pLibretroManager->m_joyPad.m_axisRX;
		const float ry = g_pLibretroManager->m_joyPad.m_axisRY;
		if (FMath::Abs(rx) > 0.2f || FMath::Abs(ry) > 0.2f)
		{
			const float dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
			const float speed = 260.0f; //bottom-screen pixels per second at full deflection
			g_pLibretroManager->m_touchX = FMath::Clamp(g_pLibretroManager->m_touchX + rx * speed * dt, 0.0f, 319.0f);
			//RMoveY already carries a -1 scale in DefaultInput.ini; verified on hardware that
			//stick-up must DECREASE screen Y with another negation here
			g_pLibretroManager->m_touchY = FMath::Clamp(g_pLibretroManager->m_touchY - ry * speed * dt, 0.0f, 239.0f);
			g_pLibretroManager->m_touchLastActiveTime = FPlatformTime::Seconds();
		}

		g_pLibretroManager->RecordTouchHistory(); //feeds the press-position latch
	}
}

void APlayerPawn::JoyPad_B_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam && key.IsGamepadKey()) return; //fly mode: pad buttons don't reach the game
	//on the 3DS a left CLICK means "touch the bottom screen where the big cursor is", not B
	//(Ctrl and the gamepad button still press B there)
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::LeftMouseButton)
	{
		g_pLibretroManager->SetTouchDown(true);
		return;
	}
	//the gamepad LEFT face button doubles as B for the retro systems (classic NES run-on-X feel),
	//but on the 3DS it is the positional Y button (handled by JoyPad_X) and must not also jump
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::Gamepad_FaceButton_Left)
	{
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = true;
}

void APlayerPawn::JoyPad_B_Released(FKey key)
{
	if (key == EKeys::LeftMouseButton)
	{
		g_pLibretroManager->SetTouchDown(false);
	}
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::LeftMouseButton)
	{
		return; //the press went to the touchscreen, don't release a B that was never pressed
	}
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::Gamepad_FaceButton_Left)
	{
		return; //3DS: that button is the positional Y (see JoyPad_X), no B was pressed
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = false;
}

void APlayerPawn::JoyPad_A_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam && key.IsGamepadKey()) return; //fly mode: pad buttons don't reach the game, Space still does
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_A] = true;
}

void APlayerPawn::JoyPad_A_Released(FKey key)
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_A] = false;
}

//3DS gamepad face buttons map by physical POSITION: gamepad top = 3DS X, gamepad left = 3DS Y
//(the ini binds gamepad top to JoyPad_Y and gamepad left to JoyPad_X, which is correct id-wise
//for the other systems but positionally crossed on the 3DS). Keyboard Z/C keep their ids.
void APlayerPawn::JoyPad_Y_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam && key.IsGamepadKey()) return;
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::Gamepad_FaceButton_Top)
	{
		g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = true;
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = true;
}

void APlayerPawn::JoyPad_Y_Released(FKey key)
{
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::Gamepad_FaceButton_Top)
	{
		g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = false;
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = false;
}

//X stays disabled for VB (its core uses the id to toggle low battery) but the 3DS needs it
void APlayerPawn::JoyPad_X_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam && key.IsGamepadKey()) return;
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::Gamepad_FaceButton_Left)
	{
		g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = true;
		return;
	}
	if (g_pLibretroManager->m_emulatorType != EMULATOR_VB)
		g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = true;
}

void APlayerPawn::JoyPad_X_Released(FKey key)
{
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS && key == EKeys::Gamepad_FaceButton_Left)
	{
		g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = false;
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = false;
}

void APlayerPawn::JoyPad_Start_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (key.IsGamepadKey())
	{
		//the fly-cam chord reads this flag, NOT the game bit, which fly mode keeps clear
		m_bPadStartHeld = true;
		if (m_bFlyCam) return; //pad Start must not pause/menu the game while flying
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = true;
}

void APlayerPawn::JoyPad_Start_Released(FKey key)
{
	if (key.IsGamepadKey())
	{
		m_bPadStartHeld = false;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = false;
}

void APlayerPawn::JoyPad_Select_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam && key.IsGamepadKey()) return;
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_SELECT] = true;
}

void APlayerPawn::JoyPad_Select_Released(FKey key)
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_SELECT] = false;
}

void APlayerPawn::JoyPad_LShoulder_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam && key.IsGamepadKey())
	{
		//while flying LB/RB halve/double the camera speed; keyboard Q/E stay the L/R buttons
		m_flySpeedMult = FMath::Max(0.125f, m_flySpeedMult * 0.5f);
		char st[48];
		snprintf(st, sizeof(st), "Fly speed x%.2f", m_flySpeedMult);
		ShowStatusMessage(st);
		return;
	}
	//gamepad system hotkeys all require HOLDING START (Seth: bare buttons kept triggering
	//them by accident).  With START held the press is a pure hotkey - the game does not see
	//the button id.  Without START, shoulders/triggers are ordinary L/R/ZL/ZR buttons.
	if (g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START])
	{
		g_pLibretroManager->SaveStateToFile();
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L] = true;
}

void APlayerPawn::JoyPad_LShoulder_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L] = false;
}

void APlayerPawn::JoyPad_RShoulder_Pressed(FKey key)
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam && key.IsGamepadKey())
	{
		m_flySpeedMult = FMath::Min(8.0f, m_flySpeedMult * 2.0f);
		char st[48];
		snprintf(st, sizeof(st), "Fly speed x%.2f", m_flySpeedMult);
		ShowStatusMessage(st);
		return;
	}
	if (g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START])
	{
		g_pLibretroManager->LoadStateFromFile();
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R] = true;
}


void APlayerPawn::JoyPad_LeftStick_Pressed()
{
	if (HelpSwallowedInput()) return;
	//START + L-stick click toggles the debug fly camera (same chord family as the
	//shoulder/trigger hotkeys above).  m_bPadStartHeld covers the EXIT press, when fly
	//mode is keeping the game-facing Start bit clear.
	if (m_bPadStartHeld || g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START])
	{
		SetFlyCamEnabled(!m_bFlyCam);
		return;
	}
	if (m_bFlyCam) return; //bare L3 is a game button; the game isn't listening to the pad now
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L3] = true;
}
void APlayerPawn::JoyPad_LeftStick_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L3] = false;
}

void APlayerPawn::JoyPad_RightStick_Pressed()
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam) return; //gamepad-only binding
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS)
	{
		//clicking the right stick taps the touchscreen at the cursor
		g_pLibretroManager->SetTouchDown(true);
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R3] = true;
}
void APlayerPawn::JoyPad_RightStick_Released()
{
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS)
	{
		g_pLibretroManager->SetTouchDown(false);
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R3] = false;
}

void APlayerPawn::JoyPad_RTrigger_Pressed()
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam) return; //gamepad-only binding; the analog trigger axes fly the camera up/down
	if (g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START])
	{
		g_pLibretroManager->ModRom(1); //START + right trigger = next game
		return;
	}
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS)
	{
		//on the 3DS the right trigger taps the touchscreen at the cursor
		g_pLibretroManager->SetTouchDown(true);
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R2] = true;
}

void APlayerPawn::JoyPad_LTrigger_Pressed()
{
	if (HelpSwallowedInput()) return;
	if (m_bFlyCam) return; //gamepad-only binding; the analog trigger axes fly the camera up/down
	if (g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START])
	{
		OnResetGame(); //START + left trigger = reset game
		return;
	}
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L2] = true;
}

void APlayerPawn::JoyPad_RTrigger_Released()
{
	if (g_pLibretroManager->m_emulatorType == EMULATOR_3DS)
	{
		g_pLibretroManager->SetTouchDown(false);
	}
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

void APlayerPawn::OnSemicolonKey()
{
	if (HelpSwallowedInput()) return;
	ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
	pActor->SetLayersPeeled(pActor->GetLayersPeeled() + 1);
	ShowStatusMessage(string("Hiding " + toString(pActor->GetLayersPeeled()) + " nearest layer(s)"));
}

void APlayerPawn::OnApostropheKey()
{
	if (HelpSwallowedInput()) return;
	ALibretroManagerActor* pActor = g_pLibretroManager->m_pLibretroManagedActor;
	pActor->SetLayersPeeled(pActor->GetLayersPeeled() - 1);
	if (pActor->GetLayersPeeled() == 0)
	{
		ShowStatusMessage(string("All layers visible"));
	}
	else
	{
		ShowStatusMessage(string("Hiding " + toString(pActor->GetLayersPeeled()) + " nearest layer(s)"));
	}
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

//Save/load state keys.  F saves, G loads, L is a load alias.  These are direct key
//bindings now: F/G used to ride the JoyPad_LShoulder/RShoulder actions, whose handlers
//require START held (a gamepad-accident guard), so bare keyboard F/G silently acted as
//L/R buttons instead of saving - while the help screen claimed otherwise.  S used to
//save too, but S is also WASD "down", so every walk downward overwrote the save.

void APlayerPawn::OnFKey()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->SaveStateToFile();
}

void APlayerPawn::OnGKey()
{
	if (HelpSwallowedInput()) return;
	g_pLibretroManager->LoadStateFromFile();
}

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

void APlayerPawn::OnVKey()
{
	if (HelpSwallowedInput()) return;
	SetFlyCamEnabled(!m_bFlyCam);
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
	PlayerInputComponent->BindKey(FKey("Semicolon"), IE_Pressed, this, &APlayerPawn::OnSemicolonKey);
	PlayerInputComponent->BindKey(FKey("Apostrophe"), IE_Pressed, this, &APlayerPawn::OnApostropheKey);
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
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &APlayerPawn::OnFKey); //save state
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &APlayerPawn::OnGKey); //load state
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
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &APlayerPawn::OnVKey); //fly camera toggle (pad chord: Start + L-stick click)
	PlayerInputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &APlayerPawn::OnLeftBracketKey);
	PlayerInputComponent->BindKey(EKeys::LeftBracket, IE_Repeat, this, &APlayerPawn::OnLeftBracketKey);
	PlayerInputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &APlayerPawn::OnRightBracketKey);
	PlayerInputComponent->BindKey(EKeys::RightBracket, IE_Repeat, this, &APlayerPawn::OnRightBracketKey);
	PlayerInputComponent->BindKey(EKeys::Slash, IE_Pressed, this, &APlayerPawn::OnSlashKey); //Slash also fires with Shift held, so ? works
	PlayerInputComponent->BindKey(EKeys::Semicolon, IE_Pressed, this, &APlayerPawn::OnSemicolonKey);
	//the physical ' key reaches UE as Quote on Windows keyboards; Apostrophe stays bound too
	PlayerInputComponent->BindKey(EKeys::Quote, IE_Pressed, this, &APlayerPawn::OnApostropheKey);
	PlayerInputComponent->BindKey(EKeys::Apostrophe, IE_Pressed, this, &APlayerPawn::OnApostropheKey);
	//"press any key to close" for the help screen - keys without a binding of their own land
	//here.  MUST NOT consume, or it would eat every keypress meant for the game/hotkeys.
	//(The help's same-frame show/hide guards keep this from fighting the ? toggle.)
	PlayerInputComponent->BindKey(EKeys::AnyKey, IE_Pressed, this, &APlayerPawn::OnAnyKey).bConsumeInput = false;
#endif
	//mouse orbits the flat camera around the layer diorama, see UpdateFlatCamera
	InputComponent->BindAxisKey(EKeys::MouseX, this, &APlayerPawn::OnMouseX);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &APlayerPawn::OnMouseY);

	//gamepad-only mirror axes MUST bind before the merged Move/RMove axes below - the axis
	//delegates fire in binding order and fly mode subtracts these for keyboard passthrough
	InputComponent->BindAxis("PadLX", this, &APlayerPawn::Pad_LX);
	InputComponent->BindAxis("PadLY", this, &APlayerPawn::Pad_LY);
	InputComponent->BindAxis("PadRX", this, &APlayerPawn::Pad_RX);
	InputComponent->BindAxis("PadRY", this, &APlayerPawn::Pad_RY);
	InputComponent->BindAxis("PadLT", this, &APlayerPawn::Pad_LT);
	InputComponent->BindAxis("PadRT", this, &APlayerPawn::Pad_RT);

	// Respond every frame to the values of our two movement axes, "MoveX" and "MoveY".
	InputComponent->BindAxis("MoveX", this, &APlayerPawn::Move_XAxis);
	InputComponent->BindAxis("MoveY", this, &APlayerPawn::Move_YAxis);
	InputComponent->BindAxis("RMoveX", this, &APlayerPawn::RMove_XAxis);
	InputComponent->BindAxis("RMoveY", this, &APlayerPawn::RMove_YAxis);
	InputComponent->BindAxis("DPadX", this, &APlayerPawn::DPad_XAxis);
	InputComponent->BindAxis("DPadY", this, &APlayerPawn::DPad_YAxis);
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

