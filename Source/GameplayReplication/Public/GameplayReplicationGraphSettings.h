// Copyright © 2024 Playton. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayReplicationGraphTypes.h"
#include "Engine/DeveloperSettingsBackedByCVars.h"

#include "GameplayReplicationGraphSettings.generated.h"

/**
 * Default settings for the Gameplay Replication Graph. 
 */
UCLASS(MinimalAPI, Config = Game, DefaultConfig, DisplayName = "Gameplay Replication Graph", meta = (Tooltip = "Settings for the Gameplay Replication Graph."))
class UGameplayReplicationGraphSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()

public:
	UGameplayReplicationGraphSettings();

	/** Static getter to find the settings for the Gameplay Replication Graph. */
	static UGameplayReplicationGraphSettings* Get()
	{
		return GetMutableDefault<UGameplayReplicationGraphSettings>();
	}

	/** Whether to enable the Gameplay Replication Graph. */
	UPROPERTY(Config, EditAnywhere, Category = ReplicationGraph)
	bool bDisableReplicationGraph = false;

	/** The default replication graph to use. */
	UPROPERTY(Config, EditAnywhere, Category = ReplicationGraph, meta = (MetaClass = "/Script/GameplayReplication.GameplayReplicationGraph"))
	FSoftClassPath DefaultReplicationGraphClass;

	/** List of custom settings for specific actor classes. */
	UPROPERTY(Config, EditAnywhere, Category = ReplicationGraph)
	TArray<FRepGraphActorClassSettings> ClassSettings;

	/** Base pawn class used by this project. */
	UPROPERTY(Config, EditAnywhere, Category = ReplicationGraph)
	TSubclassOf<APawn> BasePawnClass;

	/** Whether to enable fast shared path. */
	UPROPERTY(EditAnywhere, Category = FastSharedPath, meta = (ConsoleVariable = "GameRepGraph.EnableFastSharedPath"))
	bool bEnableFastSharedPath = true;

	/** How much bandwidth to use for FastShared movement updates. This is counted independently of the NetDriver's target bandwidth. */
	UPROPERTY(EditAnywhere, Category = FastSharedPath, meta = (ForceUnits = Kilobytes, ConsoleVariable = "GameRepGraph.TargetKBytesSecFastSharedPath"))
	int32 TargetKBytesSecFastSharedPath = 10;

	/** The distance requirement percentage for FastSharedPath. */
	UPROPERTY(EditAnywhere, Category = FastSharedPath, meta = (ConsoleVariable = "GameRepGraph.FastSharedPathCullDistPct"))
	float FastSharedPathCullDistPct = 0.80f;

	/** The maximum distance to replicate destruction info at. */
	UPROPERTY(EditAnywhere, Category = DestructionInfo, meta = (ForceUnits = cm, ConsoleVariable = "GameRepGraph.DestructInfo.MaxDist"))
	float DestructionInfoMaxDist = 30000.f;

	/** The cell size for the spatial grid. */
	UPROPERTY(EditAnywhere, Category = SpatialGrid, meta = (ForceUnits = cm, ConsoleVariable = "GameRepGraph.CellSize"))
	float SpatialGridCellSize = 10000.0f;

	/** Essentially "Min X" for replication. This is just an initial value. The system will reset itself if actors appear outside of this. */
	UPROPERTY(EditAnywhere, Category = SpatialGrid, meta = (ForceUnits = cm, ConsoleVariable = "GameRepGraph.SpatialBiasX"))
	float SpatialBiasX = -200000.0f;

	/** Essentially "Min Y" for replication. This is just an initial value. The system will reset itself if actors appear outside of this. */
	UPROPERTY(EditAnywhere, Category = SpatialGrid, meta = (ForceUnits = cm, ConsoleVariable = "GameRepGraph.SpatialBiasY"))
	float SpatialBiasY = -200000.0f;

	/** Whether to disable spatial rebuilds. */
	UPROPERTY(EditAnywhere, Category = SpatialGrid, meta = (ConsoleVariable = "GameRepGraph.DisableSpatialRebuilds"))
	bool bDisableSpatialRebuilds = true;

	/**
	 * How many buckets to spread dynamic, spatialized actors across.
	 * High number = more buckets = smaller effective replication frequency.
	 * This happens before individual actors do their own NetUpdateFrequency check.
	 */
	UPROPERTY(EditAnywhere, Category = DynamicSpatialFrequency, meta = (ConsoleVariable = "GameRepGraph.DynamicActorFrequencyBuckets"))
	int32 DynamicActorFrequencyBuckets = 3;
};
