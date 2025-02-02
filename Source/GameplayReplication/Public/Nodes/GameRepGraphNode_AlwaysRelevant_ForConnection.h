// Copyright © 2024 Playton. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReplicationGraph.h"

#include "GameRepGraphNode_AlwaysRelevant_ForConnection.generated.h"

struct FActorRepListRefView;
struct FConnectionGatherActorListParameters;
struct FNewReplicatedActorInfo;
class UObject;
class AGameplayDebuggerCategoryReplicator;

UCLASS()
class UGameRepGraphNode_AlwaysRelevant_ForConnection
	: public UReplicationGraphNode_AlwaysRelevant_ForConnection
{
	GENERATED_BODY()

public:
	//~ Begin UReplicationGraphNode Interface
	virtual void NotifyAddNetworkActor(const FNewReplicatedActorInfo& Actor) override { }
	virtual bool NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& Actor, bool bWarnIfNotFound = true) override { return false; }
	virtual void NotifyResetAllNetworkActors() override { }

	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;

	virtual void LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const override;
	//~ End UReplicationGraphNode Interface

	void OnClientLevelVisibilityAdd(FName LevelName, UWorld* StreamingWorld);
	void OnClientLevelVisibilityRemove(FName LevelName);
	
	void ResetGameWorldState();

#if WITH_GAMEPLAY_DEBUGGER
	TObjectPtr<AGameplayDebuggerCategoryReplicator> GameplayDebugger = nullptr;
#endif

private:
	TArray<FName, TInlineAllocator<64>> AlwaysRelevantStreamingLevelsNeedingReplication;
	bool bInitializedPlayerState = false;
};
