// Copyright © 2024 Playton. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReplicationGraph.h"

#include "GameRepGraphNode_PlayerStateFrequencyLimiter.generated.h"

struct FActorRepListRefView;
struct FConnectionGatherActorListParameters;
struct FNewReplicatedActorInfo;
class UObject;

/**
 * This node is responsible for limiting the number of player states that are replicated per frame.
 * This is useful for games with a large number of players where we want to limit the number of player states that are replicated per frame.
 */
UCLASS()
class UGameRepGraphNode_PlayerStateFrequencyLimiter : public UReplicationGraphNode
{
	GENERATED_BODY()

public:
	UGameRepGraphNode_PlayerStateFrequencyLimiter();

	//~ Begin UReplicationGraphNode Interface
	virtual void NotifyAddNetworkActor(const FNewReplicatedActorInfo& Actor) override { }
	virtual bool NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, bool bWarnIfNotFound=true) override { return false; }
	virtual bool NotifyActorRenamed(const FRenamedReplicatedActorInfo& Actor, bool bWarnIfNotFound=true) override { return false; }
	//~ End UReplicationGraphNode Interface

	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;

	virtual void PrepareForReplication() override;

	virtual void LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const override;

	/** How many actors we want to return to the replication driver per frame. Will not suppress ForceNetUpdate. */
	int32 TargetActorsPerFrame = 2;

private:
	TArray<FActorRepListRefView> ReplicationActorLists;
	FActorRepListRefView ForceNetUpdateReplicationActorList;
};
