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

	//---- Debug fly camera (L-stick click + R-stick click on the pad, or V).  The gamepad
	//flies the camera while the game keeps running; keyboard input still reaches the game. ----

	//Movement speed at full stick, as a fraction of the layer AABB's largest dimension per
	//second (each system uses a wildly different world scale: NES ~41 units, Atari ~445, VB ~310)
	UPROPERTY(EditAnywhere)
	float m_flyMoveSpeedFactor = 0.5f;

	//Trigger up/down speed, same units
	UPROPERTY(EditAnywhere)
	float m_flyVerticalSpeedFactor = 0.3f;

	//Degrees per second at full right-stick deflection
	UPROPERTY(EditAnywhere)
	float m_flyLookYawSpeed = 120.0f;

	UPROPERTY(EditAnywhere)
	float m_flyLookPitchSpeed = 90.0f;

	UPROPERTY(EditAnywhere)
	float m_flyPitchLimit = 89.0f;

	bool m_bFlyCam = false;
	FVector m_flyPos = FVector::ZeroVector;
	float m_flyYaw = 0;
	float m_flyPitch = 0;
	float m_flySpeedMult = 1.0f; //runtime speed scale, LB/RB halve/double it while flying
	//Physical gamepad left-stick click: the chord MODIFIER for all pad system hotkeys
	//(L3+RB = load state etc).  It is a pure modifier - never forwarded to the core - so
	//chording can't leak a button into the game (Start used to be the modifier and every
	//chord popped the game's Start menu).
	bool m_bPadL3Held = false;

	//Gamepad-only mirrors of the merged Move/RMove axes (PadLX..PadRT in DefaultInput.ini,
	//identical key+scale, bound FIRST so they're fresh): merged - mirror = keyboard exactly,
	//which is how fly mode hijacks the pad while WASD keeps reaching the game
	float m_padLX = 0;
	float m_padLY = 0;
	float m_padRX = 0;
	float m_padRY = 0;
	float m_padLT = 0;
	float m_padRT = 0;

	//---- Scripted camera moves for GIF capture (holo.CamSweep / CamPose / CamWiggle) ----
	enum class ECamScript : uint8 { None, Sweep, Wiggle, Pose };
	ECamScript m_camScript = ECamScript::None;
	float m_scriptT = 0;        //elapsed seconds
	float m_scriptDur = 4;      //duration (Sweep/Pose) or period (Wiggle)
	float m_scriptA = 0;        //Sweep: yawA. Wiggle: amplitude. Pose: target yaw
	float m_scriptB = 0;        //Sweep: yawB. Pose: target pitch
	float m_scriptPitch = 0;    //Sweep/Wiggle: pitch held for the whole script
	float m_scriptCycles = 0;   //Wiggle: whole cycles to run, 0 = until holo.CamStop
	float m_scriptStartYaw = 0;
	float m_scriptStartPitch = 0;

	//---- Depth-scale ramp (holo.DepthRamp), independent slot so it can run under a Cam script ----
	bool m_depthRampActive = false;
	float m_depthRampFrom = 0;
	float m_depthRampTo = 1;
	float m_depthRampDur = 4;
	float m_depthRampT = 0;

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

	//raw per-frame axis values, kept separate so the 3DS can route the stick/keys to the
	//analog circle pad and the gamepad d-pad to the real digital d-pad independently
	float m_moveAxisX = 0;
	float m_moveAxisY = 0;
	float m_dpadAxisX = 0;
	float m_dpadAxisY = 0;

	void UpdateFlatCamera(float DeltaTime);
	void UpdateFlyCamera(float DeltaTime);
	void UpdateDepthRamp(float DeltaTime);
	void FinishCamScript(); //hand the camera back to the orbit at the script's final angles
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

	//Debug fly camera toggle (L-stick click + R-stick click, V key, or console holo.FlyCam)
	void SetFlyCamEnabled(bool bEnable);
	bool IsFlyCamEnabled() const { return m_bFlyCam; }

	//Place the fly camera exactly (console holo.FlyPose - the harness can't move a gamepad
	//stick).  Enables fly mode if off; posOffset is relative to the layer-stack center.
	void SetFlyPose(float yaw, float pitch, bool bHasPos, const FVector& posOffset);

	//Scripted camera moves for GIF capture (console holo.Cam* / holo.DepthRamp commands).
	//pitch = CAM_PITCH_KEEP means "hold whatever pitch the camera has right now".
	static constexpr float CAM_PITCH_KEEP = 9999.0f;
	void StartCamSweep(float yawA, float yawB, float seconds, float pitch);
	void StartCamPose(float yaw, float pitch, float seconds);
	void StartCamWiggle(float amplitude, float period, float cycles, float pitch);
	void StartDepthRamp(float from, float to, float seconds);
	void StopCamScripts(); //cancels the camera script AND the depth ramp

	void OnSubtractKey();

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Move_XAxis(float AxisValue);
	void Move_YAxis(float AxisValue);
	void RMove_XAxis(float AxisValue);
	void RMove_YAxis(float AxisValue);
	void DPad_XAxis(float AxisValue);
	void DPad_YAxis(float AxisValue);
	void UpdateDpadButtons(); //merges the Move and DPad axes into the digital d-pad bits

	void OnMouseX(float AxisValue);
	void OnMouseY(float AxisValue);

	void JoyPad_B_Pressed(FKey key); //key-aware: on 3DS a left CLICK is a touchscreen tap, not B
	void JoyPad_B_Released(FKey key);
	void UpdateTouchMouseLock(); //keeps the OS cursor inside the window while 3DS touch is active

	void JoyPad_A_Pressed(FKey key);  //key-aware: fly mode blocks the gamepad button, Space still plays
	void JoyPad_A_Released(FKey key);

	void JoyPad_Y_Pressed(FKey key); //key-aware: L3+Y chord = pause; the 3DS maps the gamepad TOP face button to its X (positional)
	void JoyPad_Y_Released(FKey key);

	void JoyPad_X_Pressed(FKey key); //key-aware: the 3DS maps the gamepad LEFT face button to its Y (positional)
	void JoyPad_X_Released(FKey key);

	void JoyPad_Start_Pressed(FKey key);  //key-aware: fly mode blocks the gamepad Start, Enter still works
	void JoyPad_Start_Released(FKey key);

	void JoyPad_Select_Pressed(FKey key);
	void JoyPad_Select_Released(FKey key);

	void JoyPad_LShoulder_Pressed(FKey key); //key-aware: while flying, gamepad LB/RB set the fly speed
	void JoyPad_LShoulder_Released();
	void JoyPad_RShoulder_Pressed(FKey key);
	void JoyPad_RShoulder_Released();
	void JoyPad_RTrigger_Pressed();
	void JoyPad_LTrigger_Pressed();
	void JoyPad_RTrigger_Released();
	void JoyPad_LTrigger_Released();

	void JoyPad_LeftStick_Pressed();  //the pad hotkey chord modifier (see m_bPadL3Held)
	void JoyPad_LeftStick_Released();

	void JoyPad_RightStick_Pressed(); //L3+R3 chord = fly camera toggle
	void JoyPad_RightStick_Released();


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
	void OnFKey(); //save state
	void OnGKey(); //load state
	void OnLKey();

	void OnCommaKey();
	void OnPeriodKey();
	void OnSemicolonKey();   //hide one more of the nearest layers (debug peel)
	void OnApostropheKey();  //unhide one
	void OnNKey();
	void OnVKey(); //keyboard fly-cam toggle (also makes the mode reachable from the harness `key` command)
	void OnResetGame();

	//recorders for the gamepad-only mirror axes (see m_padLX comment)
	void Pad_LX(float v);
	void Pad_LY(float v);
	void Pad_RX(float v);
	void Pad_RY(float v);
	void Pad_LT(float v);
	void Pad_RT(float v);

	void OnLeftBracketKey();
	void OnRightBracketKey();
	void OnSlashKey();
	void OnAnyKey();

};
