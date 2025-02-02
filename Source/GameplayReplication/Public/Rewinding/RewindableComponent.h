// Copyright © 2024 Playton. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "RewindableComponent.generated.h"

class APlayerController;
class AActor;
struct FFrame;

/** Packaged information about the state of an actor at a given frame. */
USTRUCT(BlueprintType)
struct FFramePackage
{
	GENERATED_BODY()

	FFramePackage()
		: HitBox(ForceInit)
		, bTeleported(false)
		, Time(0.0f)
	{
	}

	explicit FFramePackage(const FBox& InHitBox, bool bInTeleported, double InTime)
		: HitBox(InHitBox)
		, bTeleported(bInTeleported)
		, Time(InTime)
	{
	}

public:
	FORCEINLINE bool IsValid() const { return Time > 0.0; }

public:
	/** The HitBox extent of the actor at time. */
	UPROPERTY()
	FBox HitBox;

	/** True, if a teleport occured getting to current position (Don't interpolate). */
	UPROPERTY()
	uint8 bTeleported : 1;

	/** Current server world time when this position was updated. */
	UPROPERTY()
	double Time;
};

/**
 * A generic component that can be used to mark an actor as rewindable.
 * Allowing the actor to be rewound to a previous state by a rewind manager/the server.
 *
 * Common use cases are for player characters, projectiles, etc.
 *
 * The important functions:
 *	Test()
 */
UCLASS(ClassGroup = (Networking), meta = (BlueprintSpawnableComponent))
class GAMEPLAYREPLICATION_API URewindableComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class ACharacter;

public:
	URewindableComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Static function to get the rewindable component for a given actor. */
	static URewindableComponent* FindRewindableComponent(const AActor* Actor);

	/** Should be called whenever the actor is teleported. */
	virtual void SetJustTeleported(bool bInJustTeleported);

	/** Updates a single frame package with the current state of the actor. */
	virtual void UpdateFramePackage(FFramePackage& InOutPackage, bool bInTeleported = false);
	virtual void UpdateFramePackage(bool bInTeleported = false);

	/** Returns this actors' position and HitBox in the desired timestamp. */
	virtual FBox GetRewoundHitBox(double InTime) const;

	/** Returns the frame package at the desired timestamp. */
	virtual FFramePackage GetFramePackage(double InTime) const;

	/** Interpolates between two frame packages at the given time. */
	virtual FFramePackage InterpBetweenFrames(const FFramePackage& A, const FFramePackage& B, double Time) const;

protected:
	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

#if ENABLE_DRAW_DEBUG
	virtual void DrawDebugFramePackage(const FFramePackage& Package, FColor Color, float DrawDuration = 4.f) const;
#endif

protected:
	/** True, if a teleport occured getting to current position (Don't interpolate). */
	uint8 bJustTeleported : 1;

	/** Cached-off pointer to the owning actors controller. */
	UPROPERTY()
	TObjectPtr<APlayerController> Controller;

	/** Linked list of frame packages. */
	TDoubleLinkedList<FFramePackage> FrameHistory;

	/** The maximum number of seconds to keep in the frame history. */
	UPROPERTY(EditAnywhere, Category = Rewinding, meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float MaxRecordTime = 0.8f;
};
