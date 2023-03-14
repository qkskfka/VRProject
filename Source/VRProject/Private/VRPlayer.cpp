// Fill out your copyright notice in the Description page of Project Settings.


#include "VRPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "VRGameModeBase.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include <Camera/CameraComponent.h>
#include <MotionControllerComponent.h>
#include <DrawDebugHelpers.h>
#include <Haptics/HapticFeedbackEffect_Curve.h>
#include <HeadMountedDisplayFunctionLibrary.h>
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Components/CapsuleComponent.h"
#include <NiagaraComponent.h>
#include <UMG/Public/Components/WidgetInteractionComponent.h>
#include "Components/WidgetInteractionComponent.h"

// Sets default values
AVRPlayer::AVRPlayer()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
	VRCamera->SetupAttachment(RootComponent);
	VRCamera->bUsePawnControlRotation = false;

	//손추가
	LeftHand = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftHand"));
	LeftHand->SetupAttachment(RootComponent);
	LeftHand->SetTrackingMotionSource(FName("Left"));
	RightHand = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightHand"));
	RightHand->SetupAttachment(RootComponent);
	RightHand->SetTrackingMotionSource(FName("Right"));

	// 스켈레탈 메쉬 컴포넌트 만들기
	LeftHandMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LeftHandMesh"));
	LeftHandMesh->SetupAttachment(LeftHand);
	RightHandMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RightHandMesh"));
	RightHandMesh->SetupAttachment(RightHand);
	
	// 스켈레탈 메쉬 로드해서 할당하기
	ConstructorHelpers::FObjectFinder<USkeletalMesh> TempMesh(TEXT("/Script/Engine.SkeletalMesh'/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_left.SKM_MannyXR_left'"));
	if (TempMesh.Succeeded())
	{
		LeftHandMesh->SetSkeletalMesh(TempMesh.Object);
		LeftHandMesh->SetRelativeLocation(FVector(-2.9f, -3.5f, 4.5f));
		LeftHandMesh->SetRelativeRotation(FRotator(-25, -180, 90));
	}
	
	ConstructorHelpers::FObjectFinder<USkeletalMesh> TempMesh2(TEXT("/Script/Engine.SkeletalMesh'/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_right.SKM_MannyXR_right'"));
	if (TempMesh2.Succeeded())
	{
		RightHandMesh->SetSkeletalMesh(TempMesh2.Object);
		RightHandMesh->SetRelativeLocation(FVector(-2.9f, 3.5f, 4.5f));
		RightHandMesh->SetRelativeRotation(FRotator(25, 0, 90));
	}
	
	// Teleport
	TeleportCircle = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TeleportCircle"));
	TeleportCircle->SetupAttachment(RootComponent);
	TeleportCircle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TeleportCurveComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TeleportCurveComp"));
	TeleportCurveComp->SetupAttachment(RootComponent);

	// 집게손가락
	RightAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightAim"));
	RightAim->SetupAttachment(RootComponent);
	RightAim->SetTrackingMotionSource(FName("RightAim"));

	WidgetInteractionComp = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("WidgetInteractionComp"));
	WidgetInteractionComp->SetupAttachment(RightAim);
}

// Called when the game starts or when spawned
void AVRPlayer::BeginPlay()
{
	Super::BeginPlay();

	// Enhanved Input 사용처리
	auto PC = Cast<APlayerController>(GetWorld()->GetFirstPlayerController());
	if (PC)
	{
		// LocalPlayer
		auto localPlayer = PC->GetLocalPlayer();
		auto subSystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(localPlayer);
		if (subSystem)
		{
			subSystem->AddMappingContext(IMC_VRInput, 0);
			subSystem->AddMappingContext(IMC_Hand, 0);
			
		}
	}

	ResetTeleport();

	// 크로스헤어 객체 만들기
	if (CrosshairFactroy)
	{
		Crosshair = GetWorld()->SpawnActor<AActor>(CrosshairFactroy);
		
	}

	// HMD가 연결되어 있지 않다면 
	if(UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()== false)
	{
		// Hand 을 테스트 할 수 있는 위치로 이동시키자
		RightHand->SetRelativeLocation(FVector(20,20,0));
		RightAim->SetRelativeLocation(FVector(20,20,0));
		// 카메라의 UsePawn Control Rotation을 활성화 시키자
		VRCamera->bUsePawnControlRotation = true;
	}
	// 만약 HMD가 연결되어 있다면
	else
	{
		// -> 기본 트랙킹 
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Eye);
	}
}

// Called every frame
void AVRPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// HMD 가 연결돼 있지 않으면
	if(UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()== false)
	{
		// -> 손이 카메라 방향과 일치하도록 하자
		RightHand->SetRelativeRotation(VRCamera->GetRelativeRotation());
		RightAim->SetRelativeRotation(VRCamera->GetRelativeRotation());
	}
	
	// 텔레포트 확인 처리
	if (bTeleporting)
	{
		// 만약 직선을 그린다면
		if(bTeleportCurve == false)
		{
			DrawTeleportStraight();
			
		}
		// 그렇지 않으면
		else
		{
			// 곡선 그리기
			DrawTelportCurve();
		}

		// 나이아가라를 이용해 선그리기
		if (TeleportCurveComp)
		{
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(TeleportCurveComp, FName("User.PointArray"), Lines);
		}
	}
	// Crosshair
	DraweCrosshiar();

	Grabbing();
}

// Called to bind functionality to input
void AVRPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	auto InputSystem = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
	if(InputSystem)
	{
		//Binding for Moving
		InputSystem->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AVRPlayer::Move);
		InputSystem->BindAction(IA_Mouse, ETriggerEvent::Triggered, this, &AVRPlayer::Turn);
		InputSystem->BindAction(IA_Teleport, ETriggerEvent::Started, this, &AVRPlayer::TeleportStart);
		InputSystem->BindAction(IA_Teleport, ETriggerEvent::Completed, this, &AVRPlayer::TeleportEnd);
		InputSystem->BindAction(IA_Fire, ETriggerEvent::Started, this, &AVRPlayer::FireInput);
		InputSystem->BindAction(IA_Fire, ETriggerEvent::Completed, this, &AVRPlayer::ReleaseUIInput);

		//잡기
		InputSystem->BindAction(IA_Grab, ETriggerEvent::Started, this, &AVRPlayer::TryGrab);
		InputSystem->BindAction(IA_Grab, ETriggerEvent::Completed, this, &AVRPlayer::UnTryGrab);
		
	}
}

void AVRPlayer::Move(const FInputActionValue& Values)
{
	UE_LOG(LogTemp, Warning, TEXT("Move !!!!"));
	// 사용자의 입력에 따라 앞뒤 좌우로 입력하고싶다.
	// 3. 사용자의 입력에 따라
	FVector2D Axis = Values.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), Axis.X);
	AddMovementInput(GetActorRightVector(), Axis.Y);

	//// 2. 앞뒤좌우라는 방향이 필요.
	//FVector Dir(Axis.X, Axis.Y, 0);
	//// 1. 이동하고 싶다.
	//// p = p0 +vt
	//FVector P0 = GetActorLocation();
	//FVector vt = Dir * moveSpeed * GetWorld()->DeltaTimeSeconds;
	//FVector P = P0 + vt;
	//SetActorLocation(P);
	
}

void AVRPlayer::Turn(const FInputActionValue& Values)
{
	FVector2D Axis = Values.Get<FVector2D>();
	AddControllerPitchInput(Axis.Y);
	AddControllerYawInput(Axis.X);
}

//텔레포트 기능 활성화처리
void AVRPlayer::TeleportStart(const FInputActionValue& Values)
{
	// 누르고 있는 중에는 사용자가 어디를 가리키는지 주시하고 싶다.
	bTeleporting = true;
	// 라인이 보이도록 활성화
	TeleportCurveComp->SetVisibility(true);
}

void AVRPlayer::TeleportEnd(const FInputActionValue& Values)
{
	// 텔레포트 기능 리셋
	// 만약 텔레포트가 불가능하다면
	if(ResetTeleport() == false)
	{
		// 다음 처리를 하지 않는다.
		return;
	}

	// 워프 사용시 워프처리
	if (IsWarp)
	{
		DoWarp();
		return;
	}
	// 그렇지 않을경우 텔레포트
	// 텔레포트 위치로 이동하고 싶다.
	SetActorLocation(TeleportLocation + FVector::UpVector* GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	
}

bool AVRPlayer::ResetTeleport()
{
	// 텔레포트써클이 활성화 되어 있을 때만 텔레포트 가능하다.
	bool bCanTeleport = TeleportCircle->GetVisibleFlag();
	// 써클 않보이게 처리
	TeleportCircle->SetVisibility(false);
	bTeleporting = false;
	TeleportCurveComp->SetVisibility(false);
	return bCanTeleport;
}


void AVRPlayer::DrawTeleportStraight()
{
	Lines.RemoveAt(0, Lines.Num());
	// 직선을 그리고 싶다.
	// 필요정보 : 시작점, 종료점
	FVector StartPos = RightAim->GetComponentLocation();
	FVector EndPos = StartPos + RightAim->GetForwardVector()* 1000;
	
	// 두 점 사이의 충돌체가 있는지 체크하자
	CheckHitTeleport(StartPos, EndPos);
	Lines.Add(StartPos);
	Lines.Add(EndPos);
	//DrawDebugLine(GetWorld(), StartPos, EndPos, FColor::Red, false, -1,0,1);
}

bool AVRPlayer::CheckHitTeleport(FVector LastPos, FVector& CurPos)
{
	FHitResult HitInfo;
	bool bHit = HitTest(LastPos, CurPos, HitInfo);
	// 만약 부딪힌 대상이 바닥이라면
	if(bHit && HitInfo.GetActor()->GetName().Contains(TEXT("Floor")))
	{
		// 마지막 점을 (EndPos) 최종 점으로 수정하고 싶다.
		CurPos = HitInfo.Location;
		//써클 활성화
		TeleportCircle->SetVisibility(true);
		// 텔레포트써클을 위치
		TeleportCircle->SetWorldLocation(CurPos);
		TeleportLocation = CurPos;
	}
	else
	{
		TeleportCircle->SetVisibility(false);
	}
	return bHit;
}

bool AVRPlayer::HitTest(FVector LastPos, FVector CurPos, FHitResult& HitInfo)
{
	FCollisionQueryParams Params;
	// 자기자신은 무시해라
	Params.AddIgnoredActor(this);
	
	bool bHit = GetWorld()->LineTraceSingleByChannel(HitInfo, LastPos, CurPos, ECC_Visibility, Params);
	return bHit;
}

// 주어진 속도로 투사체를 날려보내고 투사체의 지나간 점을 기록하자
void AVRPlayer::DrawTelportCurve()
{
	// Line 초기화
	Lines.RemoveAt(0, Lines.Num());
	// 주어진 속도로 투사체를 날려보내고 투사체의 지나간 점을 기록하자
	// 4. 시작점, 방향, 힘도 투사체를 던진다.
	FVector Pos = RightAim->GetComponentLocation();
	FVector Dir = RightAim->GetForwardVector() * CurvedPower;
	// 시작점을 가장 먼저 기록하자
	Lines.Add(Pos);
	for(int i=0; i<LineSmooth; i++)
	{
		// 이전 점 기억
		FVector LastPos = Pos;
		// 3. 투사체가 이동했으니까 반복적으로
		// V = v0 + at
		Dir += FVector::UpVector * Gravity * simulatedTime;
		// P = P0 + vt
		Pos += Dir* simulatedTime;
		// 2. 투사체의 위치에서
		// -> 점과 점 사이에 물체가 가로막고 있다면
		if(CheckHitTeleport(LastPos, Pos))
		{
			//	-> 그점을 마지막 점으로 하자
			Lines.Add(Pos);
			break;
		}
		// 1. 점을 기록하자
		Lines.Add(Pos);
	}
	// for(int i=0; i<Lines.Num()-1;i++)
	// {
	// 	DrawDebugLine(GetWorld(), Lines[i], Lines[i+1], FColor::Red, false, -1, 0, 1);
	// }
}

void AVRPlayer::DoWarp()
{
	// 워프 기능이 활성화 되어 있을때
	if (IsWarp == false)
	{
		return;
	}
	// 워프 처리 하고 싶다.
	// ->일정시간 동안 빠르게 이동하는 거야
	// 경과시간 초기화
	CurTime = 0;
	// 충돌제 비활성화
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 3. 시간이 흘러야 한다.
	// 2. 일정시간동안
	// 람다는 [캡쳐]()->{body}
	GetWorld()->GetTimerManager().SetTimer(WarpHandle, FTimerDelegate::CreateLambda(
		[this]()->void
	{
			// body 		
			// 일정시간안에 목적지에 도착하고 싶다.
			// 1. 시간이 흘러야한다.
			CurTime += GetWorld()->DeltaTimeSeconds;
			// 현재
			FVector CurPos = GetActorLocation();
			// 도착
			FVector EndPos = TeleportLocation + FVector::UpVector * GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); 
			// 2. 이동해야 한다.
			CurPos = FMath::Lerp<FVector>(CurPos, EndPos, CurTime / WarpTime);
			// 3. 목적지에 도착
			SetActorLocation(CurPos);
			// 시간이 다 흘렀다면
			if (CurTime >= WarpTime)
			{
				// 그 위치로 할당해주기.
				SetActorLocation(EndPos);
				// -> 타이머 종료하기(워프종료)
				GetWorld()->GetTimerManager().ClearTimer(WarpHandle);
				GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			// 거리가 거의 가까워졌다면
			// float Distance = FVector::Dist(CurPos, EndPos);
			// if (Distance < 0.1f)
			// {
			// 	CurPos = EndPos;
			// }
			SetActorLocation(CurPos);
	}
	), 0.02f, true);
	
}
void AVRPlayer::ReleaseUIInput()
{
	if (WidgetInteractionComp)
	{
		WidgetInteractionComp->ReleasePointerKey(EKeys::LeftMouseButton);
	}
}

void AVRPlayer::FireInput(const FInputActionValue& Values)
{
	// UI에 이벤트 전달하고 싶다.
	if (WidgetInteractionComp)
	{
		WidgetInteractionComp->PressPointerKey(EKeys::LeftMouseButton);
	}
	// 진동처리 하고 싶다.
	auto PC = Cast<APlayerController>(GetController());
	if (PC)
	{
		PC->PlayHapticEffect(HF_Fire, EControllerHand::Right);
	}
	
	// LineTrace 이용해서 총을 쏘고 싶다.
	// 시작점
	FVector StartPos = RightAim -> GetComponentLocation();
	// 종료점
	FVector EndPos = StartPos + RightAim-> GetForwardVector()* 10000;
	// 총쏘기 (LineTrace 동작)
	FHitResult HitInfo;
	bool bHit = HitTest(StartPos, EndPos, HitInfo);
	// 만약 부딫히는 녀석이 있으면 날려보내자
	if(bHit)
	{
		auto HitComp = HitInfo.GetComponent();
		if (HitComp && HitComp->IsSimulatingPhysics())
		{
			// 날려보내자
			// F = ma
			HitComp->AddForceAtLocation((EndPos - StartPos).GetSafeNormal()* 1000000* HitComp->GetMass(), HitInfo.Location);
		}
	}
}

// 거리에 따라서 크로스헤어 크기가 같게 보이도록하자
void AVRPlayer::DraweCrosshiar()
{
	//시작점
	FVector StartPos = RightAim->GetComponentLocation();
	// 끝점
	FVector EndPos = StartPos + RightAim->GetForwardVector() * 10000;
	// 충돌정보를 저장
	FHitResult HitInfo;
	// 충돌체크
	bool bHit = HitTest(StartPos, EndPos, HitInfo);

	float Distance = 0;
	// ->충돌이 발생하면
	if (bHit)
	{
		//		->충돌한 지점에 크로스헤어 표시
		Crosshair->SetActorLocation(HitInfo.Location);
		Distance = HitInfo.Distance;
	}
	// ->그렇지 않으면
	else
	{
		//		-> 그냥 끝점에 크로스헤어 표시
		Crosshair->SetActorLocation(EndPos);
		Distance =(EndPos - StartPos).Size();
	}
	Crosshair->SetActorScale3D(FVector(FMath::Max<float>(1,Distance)));
	// 빌보딩
	// -> 크로스헤어가 카메라를 바라보도록 처리
	FVector Direction =  Crosshair->GetActorLocation() - VRCamera->GetComponentLocation();
	Crosshair->SetActorRotation(Direction.ToOrientationRotator());
}

// 물체를 잡고 싶다.
void AVRPlayer::TryGrab()
{
	// 중심점
	FVector Center = RightHand->GetComponentLocation();
	// 충돌체크(구충돌)
	// 충돌한 물체들 기록할 배열
	// 충돌 질의 작성
	FCollisionQueryParams Param;
	Param.AddIgnoredActor(this);
	Param.AddIgnoredComponent(RightHand);
	TArray<FOverlapResult> HitObjs;
	bool bHit = GetWorld()->OverlapMultiByChannel(HitObjs, Center, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(GrabRange), Param);

	// 총돌하지 않았다면 아무처리 하지 않는다.
	if (bHit == false)
	{
		return;
	}
	// -> 가장 가까운 물체 잡도록 하자
	
	// 가장 가까운 물체 인덱스
	int Closest = 0;
	for (int i = 0; i<HitObjs.Num(); i++)
	{
		// 1. 물리 기능이 활성화 되어 있는 녀석만 판단
		// -> 만약 부딪힌 컴포넌트가 물리기능이 비활성화 되어 있다면
		if (HitObjs[i].GetComponent()->IsSimulatingPhysics() == false)
		{
			// 검출하고 싶지 않다.
			continue;
		}
		// 잡았다.
		IsGrabbed = true;
		
		// 2. 현재 손과 가장 가까운 녀석과 이번에 검출할 녀석과 더 가까운 녀석이 있다면
		// -> 필요속성 : 현재 가장가까운 녀석과 손과의 거리
		float ClosestDist = FVector::Dist(HitObjs[Closest].GetActor()->GetActorLocation(), Center);
		// -> 필요속성 : 이번에 검출할 녀석과 손과의 거리
		float NextDist = FVector::Dist(HitObjs[i].GetActor()->GetActorLocation(), Center);
		// 3. 만약 이번에가 현재꺼 보다 더 가깝다면
		if (NextDist < ClosestDist)
		{
			//  -> 가장 가까운 녀석으로 변경하기
			Closest = i;
		}
	}
	// 만약 잡았다면
	if (IsGrabbed)
	{
		GrabbedObject = HitObjs[Closest].GetComponent();
		// -> 물체 물리기능 비활성화
		GrabbedObject->SetSimulatePhysics(false);
		GrabbedObject->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		
		// -> 손에 붙여주자
		GrabbedObject->AttachToComponent(RightHand, FAttachmentTransformRules::KeepWorldTransform);

		PrePos = RightHand->GetComponentLocation();
	}
	
}

// 잡은 녀셕이 있으면 놓고싶다.
void AVRPlayer::UnTryGrab()
{
	if(IsGrabbed == false)
	{
		return;
	}
	
		// 1. 잡지않은 상태로 전환
		IsGrabbed = false;
		// 2. 손에서 떼어내기
		GrabbedObject->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		// 3. 물리기능 활성화
		GrabbedObject->SetSimulatePhysics(true);
		// 4. 충돌기능 활성화
		GrabbedObject->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		// 던지기
		GrabbedObject->AddForce(ThrowDirection * ThrowPower * GrabbedObject->GetMass());
		// 회전 시키기
		// 각속도 = (1 / dt) * dTheta(특정 축 기준 변위 각도 Axis, angle)
		float Angle;
		FVector Axis;
		DeltaRotation.ToAxisAndAngle(Axis, Angle);
		float dt = GetWorld()->DeltaTimeSeconds;
		FVector AngularVeLocity = (1.0f / dt)* Angle *Axis;
		GrabbedObject->SetPhysicsAngularVelocityInRadians(AngularVeLocity* ToquePower, true);

		GrabbedObject = nullptr;
}

// 던질 정보를 업데이트 하기 위한 기능
void AVRPlayer::Grabbing()
{
	if(IsGrabbed == false)
	{
		return;
	}
	
	// 던질방향 업데이트
	ThrowDirection = RightHand->GetComponentLocation() - PrePos;
	// 회전방향 업데이트
	// 쿼터니온 공싱
	// Angle1 = Q1 , Angle 2 = Q2
	// Angle1 + Angle2 = Q1* Q2
	// -Angle1 = Q1.Inverse()
	// Angle2 <- Angle1 = Q2 * Q1.Inverse()
	DeltaRotation = RightHand->GetComponentQuat()* PrevRot.Inverse();
	
	// 이전위치 업데이트
	PrePos = RightHand->GetComponentLocation();
	// 이전 회전값 업데이트
	PrevRot = RightHand->GetComponentQuat();
}





