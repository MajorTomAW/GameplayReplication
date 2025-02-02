// Copyright © 2024 Playton. All Rights Reserved.


#include "Rewinding/RewindableComponent.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(RewindableComponent)

namespace Rewindable
{
	int32 DrawDebug = 0;
	static FAutoConsoleVariableRef CVarRewindableDrawDebug(TEXT("Rewindable.DrawDebug"), DrawDebug, TEXT("Draw debug information for rewindable components."), ECVF_Default);
}

URewindableComponent::URewindableComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	
	bJustTeleported = false;
}

URewindableComponent* URewindableComponent::FindRewindableComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<URewindableComponent>() : nullptr;
}

void URewindableComponent::SetJustTeleported(bool bInJustTeleported)
{
}

void URewindableComponent::UpdateFramePackage(FFramePackage& Package, bool bInTeleported)
{
#if WITH_SERVER_CODE
	// This is potentially a double-check, but it is better to be safe than sorry
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	// Fill the frame package with the current state of the actor
	Package.Time = GetWorld()->GetTimeSeconds();
	Package.bTeleported = bInTeleported;
	Package.HitBox = GetOwner()->GetComponentsBoundingBox();
#endif
}

void URewindableComponent::UpdateFramePackage(bool bInTeleported)
{
#if WITH_SERVER_CODE
	// This is potentially double-check, but it is better to be safe, than sorry :p
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	// If there is only one frame in history, add a new one to its head (top)
	if (FrameHistory.Num() <= 1)
	{
		FFramePackage ThisFrame;
		UpdateFramePackage(ThisFrame, bInTeleported);
		FrameHistory.AddHead(ThisFrame);
	}
	else
	{
		// If there are more than one frame in history,
		// delete old frames that exceeded the max record duration
		// and add a new frame to the head (top)
		float HistoryLength = FrameHistory.GetHead()->GetValue().Time - FrameHistory.GetTail()->GetValue().Time;

		// Clean up old frames
		while (HistoryLength > MaxRecordTime)
		{
			// Remove the last frame
			FrameHistory.RemoveNode(FrameHistory.GetTail());

			// Update the history length
			HistoryLength = FrameHistory.GetHead()->GetValue().Time - FrameHistory.GetTail()->GetValue().Time;
		}

		// Now create a new frame package and add it to the head (top)
		FFramePackage ThisFrame;
		UpdateFramePackage(ThisFrame, bInTeleported);
		FrameHistory.AddHead(ThisFrame);
	}

#if ENABLE_DRAW_DEBUG
	if (Rewindable::DrawDebug > 0)
	{
		DrawDebugFramePackage(FrameHistory.GetHead()->GetValue(), FColor::White, MaxRecordTime);
	}
#endif
	
#endif
}

FBox URewindableComponent::GetRewoundHitBox(double InTime) const
{
	FBox TargetHitBox = GetOwner()->GetComponentsBoundingBox();
	FBox PreHitBox = TargetHitBox;
	FBox PostHitBox = TargetHitBox;

	const float PredictionTime = GetWorld()->GetTimeSeconds() - InTime;
	float Percent = 0.999f;
	bool bTeleported = false;

	if (PredictionTime > 0.f)
	{
		
	}

	return TargetHitBox;
}

FFramePackage URewindableComponent::GetFramePackage(double InTime) const
{
	// Validate the time and history
	if (GetOwner() == nullptr ||
		FrameHistory.GetHead() == nullptr ||
		FrameHistory.GetTail() == nullptr)
	{
		return FFramePackage();
	}

	// Find the frame package that is closest to the desired time
	FFramePackage FrameToCheck;
	bool bShouldInterpolate = true;

	// Get the frame history
	const float OldestHistoryTime = FrameHistory.GetTail()->GetValue().Time;
	const float NewestHistoryTime = FrameHistory.GetHead()->GetValue().Time;

	// Check if the desired time is within the history
	if (OldestHistoryTime > InTime)
	{
		// Too far back in time
		return FFramePackage();
	}

	if (OldestHistoryTime == InTime)
	{
		FrameToCheck = FrameHistory.GetTail()->GetValue();
		bShouldInterpolate = false;
	}

	if (NewestHistoryTime <= InTime)
	{
		// Too far ahead in time
		// Fallback to the latest frame we currently have
		FrameToCheck = FrameHistory.GetHead()->GetValue();
		bShouldInterpolate = false;
	}

	// Now try to find the frame package that is closest to the desired time
	TDoubleLinkedList<FFramePackage>::TDoubleLinkedListNode* Younger = FrameHistory.GetHead();
	TDoubleLinkedList<FFramePackage>::TDoubleLinkedListNode* Older = Younger;
	while (Older->GetValue().Time > InTime)
	{
		// March back until the desired time is inbetween the two frames
		if (Older->GetNextNode() == nullptr)
		{
			break;
		}

		Older = Older->GetNextNode();
		if (Older->GetValue().Time > InTime)
		{
			Younger = Older;
		}
	}

	// Highly unlikely, but just in case we found the exact frame
	if (Older->GetValue().Time == InTime)
	{
		FrameToCheck = Older->GetValue();
		bShouldInterpolate = false;
	}

	// Interpolate between the two frames
	if (bShouldInterpolate)
	{
		FrameToCheck = InterpBetweenFrames(Older->GetValue(), Younger->GetValue(), InTime);
	}

	return FrameToCheck;
}

FFramePackage URewindableComponent::InterpBetweenFrames(
	const FFramePackage& A, const FFramePackage& B, double Time) const
{
	const float Distance = B.Time - A.Time;
	const float InterpFraction = FMath::Clamp((Time - A.Time) / Distance, 0.f, 1.f);

	FFramePackage InterpFramePackage;
	InterpFramePackage.Time = Time;

	// Interpolate the frames
	InterpFramePackage.bTeleported = A.bTeleported || B.bTeleported;
	InterpFramePackage.HitBox.Max = FMath::VInterpTo(A.HitBox.Max, B.HitBox.Max, 1.f, InterpFraction);
	InterpFramePackage.HitBox.Min = FMath::VInterpTo(A.HitBox.Min, B.HitBox.Min, 1.f, InterpFraction);

#if ENABLE_DRAW_DEBUG
	if (Rewindable::DrawDebug > 0)
	{
		DrawDebugFramePackage(InterpFramePackage, FColor::Yellow, MaxRecordTime);	
	}
#endif

	return InterpFramePackage;
}

void URewindableComponent::BeginPlay()
{
	Super::BeginPlay();
}

void URewindableComponent::TickComponent(
	float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Only rewind time for authority actors
	if (GetOwner()->HasAuthority())
	{
		UpdateFramePackage(bJustTeleported);
		bJustTeleported = false;
	}
}

#if ENABLE_DRAW_DEBUG
void URewindableComponent::DrawDebugFramePackage(const FFramePackage& Package, FColor Color, float DrawDuration) const
{
	DrawDebugBox(
		GetWorld(),
		Package.HitBox.GetCenter(),
		Package.HitBox.GetExtent(),
		FQuat::Identity,
		Color,
		false,
		DrawDuration,
		0,
		0.25f
		);
}
#endif