//  Programmer(s):  Seth A. Robinson (seth@rtsoft.com)

//Overidding this so I can get player input

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PlayerPawn.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;


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
	UMaterialInstanceDynamic* m_pBGMatNoShadow = NULL;
	UPROPERTY()
	UMaterial* m_pPicBG = NULL;
	UPROPERTY()
	UMaterial* m_pBGNoShadowMat = NULL;

	UPROPERTY()
	UStaticMeshComponent* m_pMesh = NULL;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void SetTintBG(FVector color, float strength, bool bAllowShadows);
	void SetBGPic();

	void OnSubtractKey();

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Move_XAxis(float AxisValue);
	void Move_YAxis(float AxisValue);

	void JoyPad_B_Pressed();
	void JoyPad_B_Released();

	void JoyPad_A_Pressed();

	void JoyPad_A_Released();

	void JoyPad_Y_Pressed();

	void JoyPad_Y_Released();

	void JoyPad_Start_Pressed();
	void JoyPad_Start_Released();

	void JoyPad_Select_Pressed();

	void JoyPad_Select_Released();

	void OnAKey();
	void OnNum1Key();
	void OnNum2Key();
	void OnNum3Key();
	void OnNum4Key();
	void OnNum5Key();

	void OnAddKey();
	void OnSKey();
	void OnLKey();

	void OnCommaKey();
	void OnPeriodKey();
	void OnResetGame();

};
