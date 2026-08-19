//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)

//Overidding this so I can get player input

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PlayerPawn.generated.h"

class UMaterial;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UCameraComponent;


UCLASS()
class HOLOVCS_API APlayerPawn : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	APlayerPawn();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY()
		UMaterialInstanceDynamic* m_pBGMat = NULL;

	UPROPERTY()
	UMaterial* m_pPicBG = NULL;
	
	UPROPERTY(EditAnywhere)
	UMaterial *m_pBGNoShadowMat;
	
	UPROPERTY(EditAnywhere)
		UMaterial *m_pBGMatNormal;

	

	UPROPERTY()
	UStaticMeshComponent* m_pMesh = NULL;

	//Only used when the LookingGlass plugin isn't active (flat/normal monitor build), the plugin's capture actor
	//takes over the viewport when it is.  Narrow FOV pulled way back so the layer parallax reads like the real device.
	UPROPERTY(VisibleAnywhere)
	UCameraComponent* m_pFlatCamera = NULL;

	//Extra margin around the layer stack when the flat camera auto-frames it (1 = exact fit)
	UPROPERTY(EditAnywhere)
	float m_flatCameraMargin = 1.1f;

	//---- Flat camera orbit: idle isometric sweep + mouse orbit.  Angles in degrees. ----

	//Idle mode: how far the camera slowly sweeps left and right of center
	UPROPERTY(EditAnywhere)
	float m_autoOrbitYawRange = 30.0f;

	//Idle mode: seconds for one full left-right-left sweep
	UPROPERTY(EditAnywhere)
	float m_autoOrbitPeriod = 24.0f;

	//Idle mode: downward tilt of the view for the isometric look (negative = camera above, looking down)
	UPROPERTY(EditAnywhere)
	float m_autoOrbitPitch = -18.0f;

	//Degrees of orbit per unit of mouse axis input.  Negate to invert.
	UPROPERTY(EditAnywhere)
	float m_mouseYawSensitivity = 3.0f;

	UPROPERTY(EditAnywhere)
	float m_mousePitchSensitivity = 3.0f;

	//Mouse orbit hands the camera back to the idle sweep after this many seconds without movement
	UPROPERTY(EditAnywhere)
	float m_idleReturnDelay = 5.0f;

	//Seconds the mouse-to-idle handback blend takes
	UPROPERTY(EditAnywhere)
	float m_returnBlendTime = 2.0f;

	//Mouse orbit pitch is clamped to +/- this so we never flip over the poles
	UPROPERTY(EditAnywhere)
	float m_manualPitchLimit = 85.0f;

	FBox m_layerBounds = FBox(ForceInit);
	bool m_bLayerBoundsValid = false;
	FVector m_camPivot = FVector::ZeroVector;
	float m_camDist = 0;
	float m_dispYaw = 0;   //what's actually on screen this frame
	float m_dispPitch = 0;
	float m_manualYaw = 0;
	float m_manualPitch = 0;
	float m_manualBlend = 0; //1 = mouse owns the camera, 0 = idle sweep owns it
	float m_autoClock = 0;
	float m_timeSinceMouseMove = 999;
	float m_mouseDX = 0;
	float m_mouseDY = 0;

	void UpdateFlatCamera(float DeltaTime);
	float ComputeFlatCameraFitDist(const FRotator& camRot) const;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void SetTintBG(FVector color, float strength, bool bAllowShadows);
	void SetBGPic();
	void FindBGMeshIfNeeded();

	//Each emulator spawns its layers at very different world scales/offsets, so re-aim and re-distance
	//the flat camera to frame whatever is actually there.  Called after InitLayers.  No-op on LG hardware
	//since the plugin owns the viewport.
	void FitFlatCameraToLayers();

	void OnSubtractKey();

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Move_XAxis(float AxisValue);
	void Move_YAxis(float AxisValue);
	void RMove_XAxis(float AxisValue);
	void RMove_YAxis(float AxisValue);

	void OnMouseX(float AxisValue);
	void OnMouseY(float AxisValue);

	void JoyPad_B_Pressed();
	void JoyPad_B_Released();

	void JoyPad_A_Pressed();
	void JoyPad_A_Released();

	void JoyPad_Y_Pressed();
	void JoyPad_Y_Released();

	void JoyPad_X_Pressed();
	void JoyPad_X_Released();

	void JoyPad_Start_Pressed();
	void JoyPad_Start_Released();

	void JoyPad_Select_Pressed();
	void JoyPad_Select_Released();

	void JoyPad_LShoulder_Pressed();
	void JoyPad_LShoulder_Released();
	void JoyPad_RShoulder_Pressed();
	void JoyPad_RShoulder_Released();
	void JoyPad_RTrigger_Pressed();
	void JoyPad_LTrigger_Pressed();
	void JoyPad_RTrigger_Released();
	void JoyPad_LTrigger_Released();

	void JoyPad_LeftStick_Pressed();
	void JoyPad_LeftStick_Released();

	void JoyPad_RightStick_Pressed();
	void JoyPad_RightStick_Released();


	void OnAKey();
	void OnNum0Key();
	void OnNum1Key();
	void OnNum2Key();
	void OnNum3Key();
	void OnNum4Key();
	void OnNum5Key();
	void OnNum6Key();
	void OnNum7Key();
	void OnNum8Key();

	void OnPKey();
	void OnAddKey();
	void OnSKey();
	void OnLKey();

	void OnCommaKey();
	void OnPeriodKey();
	void OnNKey();
	void OnResetGame();

};
