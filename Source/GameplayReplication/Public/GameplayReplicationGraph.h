// Copyright © 2024 Playton. All Rights Reserved.

#pragma once

#include "ReplicationGraph.h"
#include "GameplayReplicationGraphTypes.h"

#include "GameplayReplicationGraph.generated.h"

class UReplicationGraphNode_ActorList;
class UReplicationGraphNode_GridSpatialization2D;
class AGameplayDebuggerCategoryReplicator;
class APlayerController;
class APawn;
class UClass;
class UObject;

/**
 * Gameplay Replication Graph implementation.
 */
UCLASS(Transient, Config = Engine)
class GAMEPLAYREPLICATION_API UGameplayReplicationGraph : public UReplicationGraph
{
	GENERATED_BODY()

public:
	UGameplayReplicationGraph();

	//~ Begin UReplicationGraph Interface
	virtual void ResetGameWorldState() override;

	virtual void InitGlobalActorClassSettings() override;
	virtual void InitGlobalGraphNodes() override;
	virtual void InitConnectionGraphNodes(UNetReplicationGraphConnection* ConnectionManager) override;

	virtual void RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo) override;
	virtual void RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo) override;
	//~ End UReplicationGraph Interface

#if WITH_GAMEPLAY_DEBUGGER
	void OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner);
#endif

	void PrintRepNodePolicies();

public:
	/** List of always relevant classes. */
	UPROPERTY()
	TArray<TObjectPtr<UClass>> AlwaysRelevantClasses;

	/** Grid node to use for spatialization. */
	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_GridSpatialization2D> GridNode;

	/** Node for always relevant actors. */
	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_ActorList> AlwaysRelevantNode;

	/** List of always relevant streaming level actors. */
	TMap<FName, FActorRepListRefView> AlwaysRelevantStreamingLevelActors;

private:
	void AddClassRepInfo(UClass* Class, EClassRepNodeMapping Mapping);
	void RegisterClassRepNodeMapping(UClass* Class);
	EClassRepNodeMapping GetClassNodeMapping(UClass* Class) const;

	void RegisterClassReplicationInfo(UClass* Class);
	bool ConditionalInitClassReplicationInfo(UClass* Class, FClassReplicationInfo& ClassInfo);
	void InitClassReplicationInfo(FClassReplicationInfo& Info, UClass* Class, bool Spatialize) const;

	EClassRepNodeMapping GetMappingPolicy(UClass* Class);
	static bool IsSpatialized(EClassRepNodeMapping Mapping) { return Mapping >= EClassRepNodeMapping::Spatialize_Static; }

private:
	TClassMap<EClassRepNodeMapping> ClassRepNodePolicies;

	/** Classes that had their replication settings explicitly set by code in UGameplayReplicationGraph::InitGlobalActorClassSettings */
	TArray<UClass*> ExplicitlySetClasses;
};
