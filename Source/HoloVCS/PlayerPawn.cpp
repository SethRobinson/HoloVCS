// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerPawn.h"
#include "Shared/UnrealMisc.h"
#include "LibretroManager.h"
#include "LibretroManagerActor.h"
#include "StatusDisplayActor.h"

// Sets default values
APlayerPawn::APlayerPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void APlayerPawn::BeginPlay()
{
	Super::BeginPlay();
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

void APlayerPawn::JoyPad_B_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = true;

}

void APlayerPawn::JoyPad_B_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_B] = false;
}


void APlayerPawn::JoyPad_Start_Pressed()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = true;
}

void APlayerPawn::JoyPad_Start_Released()
{
	g_pLibretroManager->m_joyPad.m_button[RETRO_DEVICE_ID_JOYPAD_START] = false;
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

void APlayerPawn::ScaleLayersXY(float scaleMod)
{
	//Good thing we've previously marked all things we want to scale with a tag called "Scalable"
	TArray<AActor*> actors;
	AddActorsByTag(&actors, GetWorld(), "Scalable");
	
	for (int i = 0; i < actors.Num(); i++)
	{
		FVector vScale = actors[i]->GetActorScale();
		//LogMsg((string("Scale is ") + toString(vScale)).c_str());
		vScale.X *= scaleMod;
		vScale.Y *= scaleMod;
		actors[i]->SetActorScale3D(vScale);
	}

}

void APlayerPawn::OnAddKey()
{
	ScaleLayersXY(1.05f);
	ShowStatusMessage("Zooming in");
}

void APlayerPawn::OnSubtractKey()
{
	ScaleLayersXY(0.95f);
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
// Called to bind functionality to input
void APlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Respond every frame to the values of our two movement axes, "MoveX" and "MoveY".
	InputComponent->BindAxis("MoveX", this, &APlayerPawn::Move_XAxis);
	InputComponent->BindAxis("MoveY", this, &APlayerPawn::Move_YAxis);
	InputComponent->BindAction("JoyPad_B", IE_Pressed, this, &APlayerPawn::JoyPad_B_Pressed);
	InputComponent->BindAction("JoyPad_B", IE_Released, this, &APlayerPawn::JoyPad_B_Released);
	InputComponent->BindAction("JoyPad_Start", IE_Pressed, this, &APlayerPawn::JoyPad_Start_Pressed);
	InputComponent->BindAction("JoyPad_Start", IE_Released, this, &APlayerPawn::JoyPad_Start_Released);
	
	//I only bound what's needed to play Pitfall.  It's setup for gamepad, arrow keys, and WASD.  Enter or Start on the controller for game reset.

	//I didn't bother to bind actions in the editor for this, this is code only stuff

	InputComponent->BindKey(EKeys::A, IE_Pressed, this, &APlayerPawn::OnAKey);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &APlayerPawn::OnNum1Key);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &APlayerPawn::OnNum2Key);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &APlayerPawn::OnNum3Key);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &APlayerPawn::OnNum4Key);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &APlayerPawn::OnNum5Key);
	
	//zoom
	InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &APlayerPawn::OnAddKey);
	InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &APlayerPawn::OnSubtractKey);

	InputComponent->BindKey(EKeys::S, IE_Pressed, this, &APlayerPawn::OnSKey);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &APlayerPawn::OnLKey);

	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

