// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerPawn.h"
#include "Shared/UnrealMisc.h"
#include "LibretroManager.h"
#include "LibretroManagerActor.h"
#include "StatusDisplayActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"

// Sets default values
APlayerPawn::APlayerPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	ConstructorHelpers::FObjectFinder<UMaterial> newMat(TEXT("/Game/Textures/castlevania_backdrop_Mat"));
	m_pPicBG = newMat.Object;


} 

// Called when the game starts or when spawned
void APlayerPawn::BeginPlay()
{
	Super::BeginPlay();

	
	auto crap = GetActorByTag(GetWorld(), "LayerBG");
	if (crap != NULL)
	{
		m_pMesh = (UStaticMeshComponent*) GetComponentByTag(crap, "StaticMeshComponent");
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

	//m_pBGMatNoShadow = UMaterialInstanceDynamic::Create(m_pBGNoShadowMat, NULL);
}

void APlayerPawn::SetTintBG(FVector color, float strength, bool bAllowShadows)
{
	if (!g_pLibretroManager || !m_pMesh) return;

	g_pLibretroManager->m_pLibretroManagedActor->m_bg_color = color;
	g_pLibretroManager->m_pLibretroManagedActor->m_bg_color_strength = strength;
	g_pLibretroManager->m_pLibretroManagedActor->m_bgAllowShadows = bAllowShadows;

	auto pMat = m_pMesh->GetMaterial(0);

	if (bAllowShadows && g_pLibretroManager->m_pLibretroManagedActor->m_curLightingMode != LIGHTING_MODE_NONE)
	{
		if (pMat->GetMaterial() != m_pBGMatNormal)
		{
			if (m_pBGMat)
			{
				//free it?  I don't know how though
			}
			m_pBGMat = UMaterialInstanceDynamic::Create(m_pBGMatNormal, NULL);
			m_pMesh->SetMaterial(0, m_pBGMat);
		}
	}
	else
	{
		if (pMat->GetMaterial() != m_pBGNoShadowMat)
		{
			m_pBGMat = UMaterialInstanceDynamic::Create(m_pBGNoShadowMat, NULL);
			m_pMesh->SetMaterial(0, m_pBGMat);
		}
	}

	m_pBGMat->SetScalarParameterValue(TEXT("TintStrength"), strength);
	m_pBGMat->SetVectorParameterValue("ColorTint", color);
}

void APlayerPawn::SetBGPic()
{
	//uh, add a way to dynamically load the texture?  Currently it's just a moon
	if (m_pMesh)
		m_pMesh->SetMaterial(0, m_pPicBG);
}

// Called every frame
void APlayerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

const float C_JOYSTICK_DEAD_ZONE = 0.3f;

void APlayerPawn::Move_XAxis(float AxisValue)
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_LEFT] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_RIGHT] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
}

void APlayerPawn::Move_YAxis(float AxisValue)
{
	
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_UP] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_DOWN] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
}

void APlayerPawn::RMove_XAxis(float AxisValue)
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R2] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R3] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
}

void APlayerPawn::RMove_YAxis(float AxisValue)
{

	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L3] = (AxisValue < -C_JOYSTICK_DEAD_ZONE);
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L2] = (AxisValue > C_JOYSTICK_DEAD_ZONE);
}

void APlayerPawn::JoyPad_B_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = true;
}

void APlayerPawn::JoyPad_B_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = false;
}

void APlayerPawn::JoyPad_A_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_A] = true;
}

void APlayerPawn::JoyPad_A_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_A] = false;
}

void APlayerPawn::JoyPad_Y_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = true;
}

void APlayerPawn::JoyPad_Y_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_Y] = false;
}

//I disabled X because no HoloVCS supported emulator uses it and the VB editor uses to toggle low battery I guess
void APlayerPawn::JoyPad_X_Pressed()
{
	//g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = true;
}

void APlayerPawn::JoyPad_X_Released()
{
	//g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_X] = false;
}

void APlayerPawn::JoyPad_Start_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = true;
}

void APlayerPawn::JoyPad_Start_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = false;
}

void APlayerPawn::JoyPad_Select_Pressed()
{
	
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_SELECT] = true;
}

void APlayerPawn::JoyPad_Select_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_SELECT] = false;
}

void APlayerPawn::JoyPad_LShoulder_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L] = true;
}

void APlayerPawn::JoyPad_LShoulder_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_L] = false;
}

void APlayerPawn::JoyPad_RShoulder_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R] = true;
}
void APlayerPawn::JoyPad_RShoulder_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_R] = false;
}

void APlayerPawn::OnAKey()
{
	g_pLibretroManager->SetSampleRate();
}

void APlayerPawn::OnNum1Key()
{
	g_pLibretroManager->SetFrameSkip(0);
}
void APlayerPawn::OnNum2Key()
{
	g_pLibretroManager->SetFrameSkip(1);
}
void APlayerPawn::OnNum3Key()
{
	g_pLibretroManager->SetFrameSkip(2);
}
void APlayerPawn::OnNum4Key()
{
	g_pLibretroManager->SetFrameSkip(3);
}
void APlayerPawn::OnNum5Key()
{
	g_pLibretroManager->SetFrameSkip(4);
}
void APlayerPawn::OnNum6Key()
{
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
	auto pLight = g_pLibretroManager->m_pLibretroManagedActor->m_pLight->FindComponentByClass<UPointLightComponent>();

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
	g_pLibretroManager->m_pLibretroManagedActor->ScaleLayersXY(1.05f);
	ShowStatusMessage("Zooming in");
}

void APlayerPawn::OnSubtractKey()
{
	g_pLibretroManager->m_pLibretroManagedActor->ScaleLayersXY(0.95f);
	ShowStatusMessage("Zooming out");
}

//Save state with loading/saving, mostly for debugging purposes

void APlayerPawn::OnSKey()
{
	g_pLibretroManager->SaveStateToFile();
}

void APlayerPawn::OnLKey()
{
	g_pLibretroManager->LoadStateFromFile();
}

void APlayerPawn::OnCommaKey()
{
	g_pLibretroManager->ModRom(-1);
}

void APlayerPawn::OnPeriodKey()
{
	g_pLibretroManager->ModRom(1);
}

void APlayerPawn::OnResetGame()
{
	LogMsg("Resetting game");
	g_pLibretroManager->ResetRom();
}

// Called to bind functionality to input
void APlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

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

	//I only bound things for Atari and NES.   It's setup for gamepad, arrow keys, and WASD.  Enter or Start on the controller for game reset.

	//well now, I guess I need a few more things for the NES too

	//I didn't bother to bind actions in the editor for this, this is code only stuff

	InputComponent->BindKey(EKeys::A, IE_Pressed, this, &APlayerPawn::OnAKey);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &APlayerPawn::OnNum1Key);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &APlayerPawn::OnNum2Key);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &APlayerPawn::OnNum3Key);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &APlayerPawn::OnNum4Key);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &APlayerPawn::OnNum5Key);
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &APlayerPawn::OnNum6Key);
	InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &APlayerPawn::OnNum7Key);
	InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &APlayerPawn::OnNum8Key);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &APlayerPawn::OnPKey);

	//zoom
	InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &APlayerPawn::OnAddKey);
	InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &APlayerPawn::OnSubtractKey);

	InputComponent->BindKey(EKeys::S, IE_Pressed, this, &APlayerPawn::OnSKey);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &APlayerPawn::OnLKey);

	InputComponent->BindKey(EKeys::Comma, IE_Pressed, this, &APlayerPawn::OnCommaKey);
	InputComponent->BindKey(EKeys::Period, IE_Pressed, this, &APlayerPawn::OnPeriodKey);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &APlayerPawn::OnResetGame);

}

