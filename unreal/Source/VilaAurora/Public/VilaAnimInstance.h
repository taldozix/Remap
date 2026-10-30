// Instância de animação nativa (sem AnimBP) dos personagens. Toca uma UAnimSequence em loop e
// faz crossfade com a anterior, como o AnimationPlayer.play(anim, blend) do Godot.
// A avaliação roda no proxy (FVilaAnimProxy, em VilaAnimInstance.cpp), que amostra as duas
// sequências e mistura as poses com FAnimationRuntime::BlendTwoPosesTogether.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"

#include "VilaAnimInstance.generated.h"

// 1: crossfade pelo proxy nativo (FVilaAnimProxy, API de FAnimInstanceProxy::Evaluate,
//    UAnimSequence::GetAnimationPose e FAnimationRuntime::BlendTwoPosesTogether, como o
//    FAnimNode_TwoWayBlend do motor). 0: o UVilaHumanoid usa PlayAnimation (nó único, troca
//    seca de pose) e o proxy nem é compilado. Fonte única do valor (VilaAnimInstance.cpp e
//    VilaHumanoid.cpp leem daqui); pode ser forçado no Build.cs: PublicDefinitions.Add("VILA_ANIM_PROXY=0").
#ifndef VILA_ANIM_PROXY
#define VILA_ANIM_PROXY 1
#endif

class UAnimSequence;
struct FAnimInstanceProxy;

UCLASS(Transient, NotBlueprintable)
class VILAAURORA_API UVilaAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Troca a pose com crossfade de `BlendTime` segundos (0 = corte seco). `StartTime` em segundos.
	// Sequências de outro esqueleto são ignoradas (mantém a pose atual).
	void PlayPose(UAnimSequence* Seq, float BlendTime, float StartTime = 0.f);
	// Velocidade de reprodução (0 congela, como speed_scale = 0 no LOD distante).
	void SetRate(float InRate) { Rate = InRate; }
	float GetRate() const { return Rate; }
	UAnimSequence* GetCurrent() const { return Current; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	// Referências fortes: o proxy guarda ponteiros crus para as mesmas sequências.
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Current;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Previous;

	float CurTime = 0.f;
	float PrevTime = 0.f;
	float BlendDuration = 0.f;
	float BlendElapsed = 0.f;
	float Rate = 1.f;
};
