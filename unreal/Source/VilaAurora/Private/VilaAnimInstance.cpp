#include "VilaAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "VilaAurora.h"

namespace
{
	// Proxy que avalia a pose nos threads de animação. Os dados são copiados do UVilaAnimInstance
	// no NativeUpdateAnimation (thread do jogo), como faz o FAnimSingleNodeInstanceProxy.
	struct FVilaAnimProxy : public FAnimInstanceProxy
	{
		FVilaAnimProxy() {}
		explicit FVilaAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

		UAnimSequence* Cur = nullptr;
		UAnimSequence* Prev = nullptr;
		float CurTime = 0.f;
		float PrevTime = 0.f;
		float Alpha = 1.f;  // peso da pose atual

		static void Sample(const UAnimSequence* Seq, float Time, FPoseContext& Out)
		{
			FAnimationPoseData PoseData(Out);
			Seq->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false));
		}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			if (!Cur)
			{
				Output.ResetToRefPose();
				return true;
			}
			if (!Prev || Alpha >= 1.f)
			{
				Sample(Cur, CurTime, Output);
				return true;
			}
			FPoseContext PoseA(Output);
			FPoseContext PoseB(Output);
			Sample(Cur, CurTime, PoseA);
			Sample(Prev, PrevTime, PoseB);
			const FAnimationPoseData DataA(PoseA);
			const FAnimationPoseData DataB(PoseB);
			FAnimationPoseData OutData(Output);
			FAnimationRuntime::BlendTwoPosesTogether(DataA, DataB, Alpha, OutData);
			return true;
		}
	};

	float AnimWrapTime(float T, const UAnimSequence* Seq)
	{
		const float Len = Seq ? Seq->GetPlayLength() : 0.f;
		if (Len <= UE_KINDA_SMALL_NUMBER) return 0.f;
		T = FMath::Fmod(T, Len);
		return T < 0.f ? T + Len : T;
	}
}

FAnimInstanceProxy* UVilaAnimInstance::CreateAnimInstanceProxy()
{
	return new FVilaAnimProxy(this);
}

void UVilaAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FVilaAnimProxy*>(InProxy);
}

void UVilaAnimInstance::PlayPose(UAnimSequence* Seq, float BlendTime, float StartTime)
{
	if (!Seq) return;
	// Uma animação de outro esqueleto quebraria a avaliação: ignora.
	const USkeletalMeshComponent* Comp = GetSkelMeshComponent();
	const USkeletalMesh* Mesh = Comp ? Comp->GetSkeletalMeshAsset() : nullptr;
	if (Mesh && Mesh->GetSkeleton() && Seq->GetSkeleton() != Mesh->GetSkeleton())
	{
		UE_LOG(LogVila, Warning, TEXT("Animação %s não pertence ao esqueleto de %s"), *Seq->GetName(), *Mesh->GetName());
		return;
	}
	if (Seq == Current) return;
	if (Current && BlendTime > 0.f)
	{
		Previous = Current;
		PrevTime = CurTime;
		BlendDuration = BlendTime;
		BlendElapsed = 0.f;
	}
	else
	{
		Previous = nullptr;
		BlendDuration = 0.f;
		BlendElapsed = 0.f;
	}
	Current = Seq;
	CurTime = AnimWrapTime(StartTime, Seq);
}

void UVilaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const float Dt = DeltaSeconds * Rate;
	if (Current) CurTime = AnimWrapTime(CurTime + Dt, Current);
	float Alpha = 1.f;
	if (Previous)
	{
		PrevTime = AnimWrapTime(PrevTime + Dt, Previous);
		// O crossfade corre em tempo real (mesmo com a animação congelada pelo LOD).
		BlendElapsed += DeltaSeconds;
		Alpha = BlendDuration > 0.f ? FMath::Clamp(BlendElapsed / BlendDuration, 0.f, 1.f) : 1.f;
		if (Alpha >= 1.f) Previous = nullptr;
	}

	FVilaAnimProxy& Proxy = GetProxyOnGameThread<FVilaAnimProxy>();
	Proxy.Cur = Current;
	Proxy.Prev = Previous;
	Proxy.CurTime = CurTime;
	Proxy.PrevTime = PrevTime;
	Proxy.Alpha = Alpha;
}
