// Copyright © 2024 MajorT. All Rights Reserved.

#pragma once

#include "GameplayReplicationGraphTypes.generated.h"

class UObject;
class UReplicationGraphNode;
struct FFrame;

DECLARE_LOG_CATEGORY_EXTERN(LogGameRepGraph, Display, All);


/**
 * The main enum to use to route actors to the right replication node.
 * Each class maps to one enum.
 *
 * – Not Routed
 * – Relevant All Connections
 * – Spatialize Static
 * – Spatialize Dynamic
 * – Spatialize Dormancy
 */
UENUM()
enum class EClassRepNodeMapping : uint8
{
	/**
	 * Doesn't map to any node.
	 * Used for special case actors that are handled by special case nodes.
	 * (UGameplayReplicationGraphNode_PlayerStateFrequencyLimiter)
	 */
	NotRouted,

	/**
	 * Routes to an AlwaysRelevantNode or AlwaysRelevantStreamingLevelNode node.
	 */
	RelevantAllConnections,

	/** ONLY SPATIALIZED Enums below here! See UGameplayReplicationGraph::IsSpatialized */

	/**
	 * Routes to GridNode:
	 * These actors don't move and don't need to be updated every frame.
	 */
	Spatialize_Static,

	/**
	 * Routes to GridNode:
	 * These actors move frequently and are updated once per frame.
	 */
	Spatialize_Dynamic,

	/**
	 * Routes to GridNode:
	 * These actors are treated as static while dormant.
	 * When flushed/not dormant, they're treated as dynamic.
	 * Note this is for things that "move while not dormant".
	 */
	Spatialize_Dormancy,
};

/**
 * Actor class settings that can be assigned directly to a class.
 * Can also be mapped to a FRepGraphActorTemplateSettings.
 */
USTRUCT()
struct FRepGraphActorClassSettings
{
	GENERATED_BODY()

	FRepGraphActorClassSettings() = default;

	/** Returns the static actor class associated with this setting */
	FORCEINLINE UClass* GetStaticActorClass() const
	{
		UClass* StaticActorClass;
		const FString ActorClassNameString = ActorClass.ToString();

		if (FPackageName::IsScriptPackage(ActorClassNameString))
		{
			StaticActorClass = FindObject<UClass>(nullptr, *ActorClassNameString, true);

			if (StaticActorClass == nullptr)
			{
				UE_LOG(LogGameRepGraph, Error, TEXT("FRepGraphActorClassSettings: Cannot Find Static Class for %s"), *ActorClassNameString);
			}
		}
		else
		{
			// Allow blueprints to be used for custom class settings
			StaticActorClass = (UClass*)StaticLoadObject(UClass::StaticClass(), nullptr, *ActorClassNameString);

			if (StaticActorClass == nullptr)
			{
				UE_LOG(LogGameRepGraph, Error, TEXT("FRepGraphActorClassSettings: Cannot Load Static Class for %s"), *ActorClassNameString);
			}
		}

		return StaticActorClass;
	}

public:
	/** The name of the class the settings will be applied to. */
	UPROPERTY(EditAnywhere, Category = ClassSettings)
	FSoftClassPath ActorClass;

	/** True, if we should add this class' replication info to the ClassRepNodePolicies map. */
	UPROPERTY(EditAnywhere, Category = ClassSettings, meta = (InlineEditConditionToggle))
	bool bAddClassRepInfoToMap = true;

	/** What ClassNodeMapping we should use when adding the class to the ClassRepNodePolicies map. */
	UPROPERTY(EditAnywhere, Category = ClassSettings, meta = (EditCondition = bAddClassRepInfoToMap))
	EClassRepNodeMapping ClassNodeMapping = EClassRepNodeMapping::NotRouted;

	/** Should we add this class to the RPC_Multicast_OpenChannelForClass map? */
	UPROPERTY(EditAnywhere, Category = ClassSettings, meta = (InlineEditConditionToggle))
	bool bAddToRPC_Multicast_OpenChannelForClassMap = false;

	/** If this is added to RPC_Multicast_OpenChannelForClass map, should we actually open a channel or not? */
	UPROPERTY(EditAnywhere, Category = ClassSettings, meta = (EditCondition = bAddToRPC_Multicast_OpenChannelForClassMap))
	bool bRPC_Multicast_OpenChannelForClass = true;
};