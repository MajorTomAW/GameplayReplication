// Copyright © 2024 Playton. All Rights Reserved.


#include "GameplayReplicationGraph.h"

#include "EngineUtils.h"
#include "GameplayReplicationGraphSettings.h"
#include "GameplayReplicationGraphTypes.h"
#include "Engine/ServerStatReplicator.h"

#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/NetConnection.h"
#include "GameFramework/Character.h"
#include "UObject/UObjectIterator.h"



#include "Nodes/GameRepGraphNode_AlwaysRelevant_ForConnection.h"
#include "Nodes/GameRepGraphNode_PlayerStateFrequencyLimiter.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategoryReplicator.h"
#endif

DEFINE_LOG_CATEGORY(LogGameRepGraph)

namespace GameplayRepGraph
{
	float DestructionInfoMaxDistance= 30000.f;
	static FAutoConsoleVariableRef CVarGameRepGraph_DestructMaxDist(TEXT("GameRepGraph.DestructInfo.MaxDist"), DestructionInfoMaxDistance, TEXT("Max distance (not squared) to rep destruct infos at"), ECVF_Default);

	int32 LogLazyInitClasses = 0;
	static FAutoConsoleVariableRef CVarGameRepGraph_LazyInitClasses(TEXT("GameRepGraph.LogLazyInitClasses"), LogLazyInitClasses, TEXT(""), ECVF_Default);

	/** How much bandwidth to use for FastShared movement updates. This is counted independently of the NetDriver's target bandwidth. */
	int32 TargetKBytesSecFastSharedPath = 10;
	static FAutoConsoleVariableRef CVarGameRepGraph_TargetKBytesSecFastSharedPath(TEXT("GameRepGraph.TargetKBytesSecFastSharedPath"), TargetKBytesSecFastSharedPath, TEXT("How much bandwidth to use for FastShared movement updates. This is counted independently of the NetDriver's target bandwidth."), ECVF_Default);

	float FastSharedPathCullDistPct = 0.80f;
	static FAutoConsoleVariableRef CVarGameRepGraph_FastSharedPathCullDistPct(TEXT("GameRepGraph.FastSharedPathCullDistPct"), FastSharedPathCullDistPct, TEXT("The distance requirement percentage for FastSharedPath"), ECVF_Default);

	/** How many buckets to spread dynamic, spatialized actors across. High number = more buckets = smaller effective replication frequency. This happens before individual actors do their own NetUpdateFrequency check. */
	int32 DynamicActorFrequencyBuckets = 3;
	static FAutoConsoleVariableRef CVarGameRepGraph_DynamicActorFrequencyBuckets(TEXT("GameRepGraph.DynamicActorFrequencyBuckets"), DynamicActorFrequencyBuckets, TEXT("How many buckets to spread dynamic, spatialized actors across. High number = more buckets = smaller effective replication frequency. This happens before individual actors do their own NetUpdateFrequency check."), ECVF_Default);

	int32 EnableFastSharedPath = 1;
	static FAutoConsoleVariableRef CVarGameRepGraph_EnableFastSharedPath(TEXT("GameRepGraph.EnableFastSharedPath"), EnableFastSharedPath, TEXT("Enable FastSharedPath"), ECVF_Default);

	/** The cell size for the spatial grid. */
	float SpatialGridCellSize = 10000.f;
	static FAutoConsoleVariableRef CVarGameRepGraph_CellSize(TEXT("GameRepGraph.CellSize"), SpatialGridCellSize, TEXT("The cell size for the spatial grid."), ECVF_Default);

	/** Essentially "Min X" for replication. This is just an initial value. The system will reset itself if actors appears outside of this. */
	float SpatialBiasX = -200000.f;
	static FAutoConsoleVariableRef CVarGameRepGraph_SpatialBiasX(TEXT("GameRepGraph.SpatialBiasX"), SpatialBiasX, TEXT("Essentially 'Min X' for replication. This is just an initial value. The system will reset itself if actors appears outside of this."), ECVF_Default);

	/** Essentially "Min Y" for replication. This is just an initial value. The system will reset itself if actors appears outside of this. */
	float SpatialBiasY = -200000.f;
	static FAutoConsoleVariableRef CVarGameRepGraph_SpatialBiasY(TEXT("GameRepGraph.SpatialBiasY"), SpatialBiasY, TEXT("Essentially 'Min Y' for replication. This is just an initial value. The system will reset itself if actors appears outside of this."), ECVF_Default);

	/** Whether to disable spatial rebuilds. */
	int32 DisableSpatialRebuilds = 1;
	static FAutoConsoleVariableRef CVarGameRepGraph_DisableSpatialRebuilds(TEXT("GameRepGraph.DisableSpatialRebuilds"), DisableSpatialRebuilds, TEXT("Whether to disable spatial rebuilds."), ECVF_Default);

	/** Whether to display client level streaming. */
	int32 DisplayClientLevelStreaming = 0;
	static FAutoConsoleVariableRef CVarGameRepGraph_DisplayClientLevelStreaming(TEXT("GameRepGraph.DisplayClientLevelStreaming"), DisplayClientLevelStreaming, TEXT("Whether to display client level streaming."), ECVF_Default);

	UReplicationDriver* ConditionalCreateReplicationDriver(const UNetDriver* ForNetDriver, const UWorld* World)
	{
		// Only create a replication driver for the GameNetDriver
		if (World && ForNetDriver && ForNetDriver->NetDriverName == NAME_GameNetDriver)
		{
			const UGameplayReplicationGraphSettings* GameRepGraphSettings = UGameplayReplicationGraphSettings::Get();

			// Enable or Disable via developer settings
			if (GameRepGraphSettings && GameRepGraphSettings->bDisableReplicationGraph)
			{
				UE_LOG(LogGameRepGraph, Warning, TEXT("Replication graph is disabled via GameplayReplicationGraphSettings."));
				return nullptr;
			}

			UE_LOG(LogGameRepGraph, Display, TEXT("Replication graph is enabled for %s in world %s."), *GetNameSafe(ForNetDriver), *GetPathNameSafe(World));

			// Load the replication graph class
			TSubclassOf<UGameplayReplicationGraph> GraphClass = GameRepGraphSettings->DefaultReplicationGraphClass.TryLoadClass<UGameplayReplicationGraph>();
			if (GraphClass.Get() == nullptr)
			{
				// Use the default replication graph class as a fallback
				GraphClass = UGameplayReplicationGraph::StaticClass();
			}

			UGameplayReplicationGraph* RepGraph = NewObject<UGameplayReplicationGraph>(GetTransientPackage(), GraphClass.Get());
			return RepGraph;
		}

		return nullptr;
	}
}


// ---------------------------------------------------------------------------------------------------------------------
// UGameplayReplicationGraph
// ---------------------------------------------------------------------------------------------------------------------

UGameplayReplicationGraph::UGameplayReplicationGraph()
{
	if (!UReplicationDriver::CreateReplicationDriverDelegate().IsBound())
	{
		UReplicationDriver::CreateReplicationDriverDelegate().BindLambda([](UNetDriver* ForNetDriver, const FURL& URL, UWorld* World)
		{
			return GameplayRepGraph::ConditionalCreateReplicationDriver(ForNetDriver, World);
		});
	}
}

void UGameplayReplicationGraph::ResetGameWorldState()
{
	AlwaysRelevantStreamingLevelActors.Empty();

	// Managed by the connection managers
	for (const UNetReplicationGraphConnection* Connection : Connections)
	{
		for (UReplicationGraphNode* ConnectionNode : Connection->GetConnectionGraphNodes())
		{
			if (UGameRepGraphNode_AlwaysRelevant_ForConnection* ThisAlwaysRelevantNode = Cast<UGameRepGraphNode_AlwaysRelevant_ForConnection>(ConnectionNode))
			{
				ThisAlwaysRelevantNode->ResetGameWorldState();
			}
		}
	}

	for (const UNetReplicationGraphConnection* Connection : PendingConnections)
	{
		for (UReplicationGraphNode* ConnectionNode : Connection->GetConnectionGraphNodes())
		{
			if (UGameRepGraphNode_AlwaysRelevant_ForConnection* ThisAlwaysRelevantNode = Cast<UGameRepGraphNode_AlwaysRelevant_ForConnection>(ConnectionNode))
			{
				ThisAlwaysRelevantNode->ResetGameWorldState();
			}
		}
	}
}

void UGameplayReplicationGraph::InitGlobalActorClassSettings()
{
	// Set up our lazy init function for classes that aren't currently loaded
	GlobalActorReplicationInfoMap.SetInitClassInfoFunc([this](UClass* Class, FClassReplicationInfo& ClassInfo)->bool
	{
		// This needs to run before RegisterClassReplicationInfo
		RegisterClassRepNodeMapping(Class);

		const bool bHandled = ConditionalInitClassReplicationInfo(Class, ClassInfo);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (GameplayRepGraph::LogLazyInitClasses != 0)
		{
			if (bHandled)
			{
				EClassRepNodeMapping Mapping = ClassRepNodePolicies.GetChecked(Class);
				UE_LOG(LogGameRepGraph, Warning, TEXT("%s was Lazy Initialized. (Parent: %s) %d."),
					*GetNameSafe(Class), *GetNameSafe(Class->GetSuperClass()), (int32)Mapping);

				FClassReplicationInfo& ParentRepInfo = GlobalActorReplicationInfoMap.GetClassInfo(Class->GetSuperClass());
				if (ClassInfo.BuildDebugStringDelta() != ParentRepInfo.BuildDebugStringDelta())
				{
					UE_LOG(LogGameRepGraph, Warning, TEXT("Differences Found!"));
					FString DebugStr = ParentRepInfo.BuildDebugStringDelta();
					UE_LOG(LogGameRepGraph, Warning, TEXT("  Parent: %s"), *DebugStr);

					DebugStr = ClassInfo.BuildDebugStringDelta();
					UE_LOG(LogGameRepGraph, Warning, TEXT("  Class : %s"), *DebugStr);
				}
			}
			else
			{
				UE_LOG(LogGameRepGraph, Warning, TEXT("%s skipped Lazy Initialization because it does not differ from its parent. (Parent: %s)"),
					*GetNameSafe(Class), *GetNameSafe(Class->GetSuperClass()));
			}
		}
#endif
		
		return bHandled;
	});

	// Set up the class mapping policy
	ClassRepNodePolicies.InitNewElement = [this](UClass* Class, EClassRepNodeMapping& NodeMapping)->bool
	{
		NodeMapping = GetClassNodeMapping(Class);
		return true;
	};

	const UGameplayReplicationGraphSettings* GameRepGraphSettings = UGameplayReplicationGraphSettings::Get();
	check(GameRepGraphSettings);

	// Set up the class settings and node mappings
	for (const FRepGraphActorClassSettings& ActorClassSetting : GameRepGraphSettings->ClassSettings)
	{
		if (ActorClassSetting.bAddClassRepInfoToMap)
		{
			if (UClass* StaticActorClass = ActorClassSetting.GetStaticActorClass())
			{
				UE_LOG(LogGameRepGraph, Log, TEXT("ActorClassSettings -- AddClassRepInfo - %s :: %i"),
					*StaticActorClass->GetName(), int(ActorClassSetting.ClassNodeMapping));

				AddClassRepInfo(StaticActorClass, ActorClassSetting.ClassNodeMapping);
			}
		}
	}

#if WITH_GAMEPLAY_DEBUGGER
	// Replicated via UGameRepGraphNode_AlwaysRelevant_ForConnection
	AddClassRepInfo(AGameplayDebuggerCategoryReplicator::StaticClass(), EClassRepNodeMapping::NotRouted);
#endif

	// Gather all replicated classes
	TArray<UClass*> AllReplicatedClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		const AActor* CDO = Cast<AActor>(Class->GetDefaultObject());
		if (!CDO || !CDO->GetIsReplicated())
		{
			continue;
		}

		// Skip SKEL and REINST classes.
		if (Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		// --------------------------------------------------------------------
		// This is a replicated class. Save this off for the second pass below
		// --------------------------------------------------------------------

		AllReplicatedClasses.Add(Class);

		RegisterClassRepNodeMapping(Class);
	}

	// ----------------------------------------------------------------------------------------------------------------
	// Setup FClassReplicationInfo.
	// This is essentially the per-class replication settings.
	// Some we set explicitly, the rest we're setting via looking at the legacy settings on AActor.
	// ----------------------------------------------------------------------------------------------------------------

	auto SetClassInfo = [&](UClass* Class, const FClassReplicationInfo& Info)
	{
		GlobalActorReplicationInfoMap.SetClassInfo(Class, Info);
		ExplicitlySetClasses.Add(Class);
	};
	ExplicitlySetClasses.Reset();

	FClassReplicationInfo CharacterClassRepInfo;
	CharacterClassRepInfo.DistancePriorityScale = 1.f;
	CharacterClassRepInfo.StarvationPriorityScale = 1.f;
	CharacterClassRepInfo.ActorChannelFrameTimeout = 4;
	CharacterClassRepInfo.SetCullDistanceSquared(GameRepGraphSettings->BasePawnClass->GetDefaultObject<APawn>()->GetNetCullDistanceSquared());

	SetClassInfo(ACharacter::StaticClass(), CharacterClassRepInfo);

	{ // Sanity-check our FSharedRepMovement type has the same quantization settings as the default character.
		FRepMovement DefaultRepMovement = GameRepGraphSettings->BasePawnClass->GetDefaultObject<APawn>()->GetReplicatedMovement();
		//@TODO: FSharedRepMovement SharedRepMovement;
	}

	// ----------------------------------------------------------------------------------------------------------------
	//	Setup FastShared replication for pawns.
	//	This is called up to once per frame per pawn to see if it wants
	//	to send a FastShared update to all relevant connections.
	// ----------------------------------------------------------------------------------------------------------------
	CharacterClassRepInfo.FastSharedReplicationFunc = [](AActor* Actor)->bool
	{
		bool bSuccess = false;
		//@TODO: ISharedReplicationInterface ??
		return bSuccess;
	};

	CharacterClassRepInfo.FastSharedReplicationFuncName = FName(TEXT("FastSharedReplication"));

	FastSharedPathConstants.MaxBitsPerFrame = (int32)((float)(GameplayRepGraph::TargetKBytesSecFastSharedPath * 1024 * 8) / NetDriver->GetNetServerMaxTickRate());
	FastSharedPathConstants.DistanceRequirementPct = GameplayRepGraph::FastSharedPathCullDistPct;

	SetClassInfo(GameRepGraphSettings->BasePawnClass, CharacterClassRepInfo);

	// ---------------------------------------------------------------------
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.ListSize = 12;
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.NumBuckets = GameplayRepGraph::DynamicActorFrequencyBuckets;
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.BucketThresholds.Reset();
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.EnableFastPath = (GameplayRepGraph::EnableFastSharedPath > 0);
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.FastPathFrameModulo = 1;

	RPCSendPolicyMap.Reset();

	// Set FClassReplicationInfo based on legacy settings from all replicated classes
	for (UClass* ReplicatedClass : AllReplicatedClasses)
	{
		RegisterClassReplicationInfo(ReplicatedClass);
	}

	// Print out what we came up with
	UE_LOG(LogGameRepGraph, Log, TEXT("======== Gameplay Replication Graph Initialized ========"));
	
	UE_LOG(LogGameRepGraph, Log, TEXT(""));
	UE_LOG(LogGameRepGraph, Log, TEXT("Class Routing Map: "));
	for (auto ClassMapIt = ClassRepNodePolicies.CreateIterator(); ClassMapIt; ++ClassMapIt)
	{
		UClass* Class = CastChecked<UClass>(ClassMapIt.Key().ResolveObjectPtr());
		EClassRepNodeMapping Mapping = ClassMapIt.Value();

		// Only print if different from the native class
		UClass* ParentNativeClass = GetParentNativeClass(Class);

		EClassRepNodeMapping* ParentMapping = ClassRepNodePolicies.Get(ParentNativeClass);
		if (ParentMapping && Class != ParentNativeClass && Mapping == *ParentMapping)
		{
			continue;
		}

		UE_LOG(LogGameRepGraph, Log, TEXT("  %s (%s) -> %s"),
			*Class->GetName(), *GetNameSafe(ParentNativeClass), *StaticEnum<EClassRepNodeMapping>()->GetNameStringByValue((int64)Mapping));
	}

	UE_LOG(LogGameRepGraph, Log, TEXT(""));
	UE_LOG(LogGameRepGraph, Log, TEXT("Class Settings Map: "));
	FClassReplicationInfo DefaultValues;
	for (auto ClassRepInfoIt = GlobalActorReplicationInfoMap.CreateClassMapIterator(); ClassRepInfoIt; ++ClassRepInfoIt)
	{
		UClass* Class = CastChecked<UClass>(ClassRepInfoIt.Key().ResolveObjectPtr());
		const FClassReplicationInfo& ClassInfo = ClassRepInfoIt.Value();
		UE_LOG(LogGameRepGraph, Log, TEXT("  %s (%s) -> %s"),
			*Class->GetName(), *GetNameSafe(GetParentNativeClass(Class)), *ClassInfo.BuildDebugStringDelta());
	}

	// Rep destruct infos based on CVar value
	DestructInfoMaxDistanceSquared = GameplayRepGraph::DestructionInfoMaxDistance * GameplayRepGraph::DestructionInfoMaxDistance;

#if WITH_GAMEPLAY_DEBUGGER
	AGameplayDebuggerCategoryReplicator::NotifyDebuggerOwnerChange.AddUObject(this, &UGameplayReplicationGraph::OnGameplayDebuggerOwnerChange);
#endif

	// Add to RPC_Multicast_OpenChannelForClass map
	RPC_Multicast_OpenChannelForClass.Reset();

	// Open channels for multicast RPCs by default
	RPC_Multicast_OpenChannelForClass.Set(AActor::StaticClass(), true);

	// Multicast should never open channels on Controllers since opening a channel on a non-owner breaks the Controller's replication.
	RPC_Multicast_OpenChannelForClass.Set(AController::StaticClass(), false);
	RPC_Multicast_OpenChannelForClass.Set(AServerStatReplicator::StaticClass(), false);

	for (const FRepGraphActorClassSettings& ActorClassSetting : GameRepGraphSettings->ClassSettings)
	{
		if (!ActorClassSetting.bAddToRPC_Multicast_OpenChannelForClassMap)
		{
			continue;
		}

		if (UClass* StaticActorClass = ActorClassSetting.GetStaticActorClass())
		{
			UE_LOG(LogGameRepGraph, Log, TEXT("ActorClassSettings -- RPC_Multicast_OpenChannelForClass - %s"),
				*StaticActorClass->GetName());

			RPC_Multicast_OpenChannelForClass.Set(StaticActorClass, ActorClassSetting.bRPC_Multicast_OpenChannelForClass);
		}
	}
}

void UGameplayReplicationGraph::InitGlobalGraphNodes()
{
	// ----------------------------------------------------------------------------------------------------------------
	//	Spatial Actors
	// ----------------------------------------------------------------------------------------------------------------
	GridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	GridNode->CellSize = GameplayRepGraph::SpatialGridCellSize;
	GridNode->SpatialBias = FVector2D(GameplayRepGraph::SpatialBiasX, GameplayRepGraph::SpatialBiasY);

	if (GameplayRepGraph::DisableSpatialRebuilds)
	{
		// Disable all spatial rebuilds
		GridNode->AddToClassRebuildDenyList(AActor::StaticClass());
	}

	AddGlobalGraphNode(GridNode);

	// ----------------------------------------------------------------------------------------------------------------
	//	Always Relevant (to everyone) Actors
	// ----------------------------------------------------------------------------------------------------------------
	AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_ActorList>();
	AddGlobalGraphNode(AlwaysRelevantNode);

	// ----------------------------------------------------------------------------------------------------------------
	//	Player State specialization.
	//	This will return a rolling subset of the player states to replicate
	// ----------------------------------------------------------------------------------------------------------------
	UGameRepGraphNode_PlayerStateFrequencyLimiter* PlayerStateNode = CreateNewNode<UGameRepGraphNode_PlayerStateFrequencyLimiter>();
	AddGlobalGraphNode(PlayerStateNode);
}

void UGameplayReplicationGraph::InitConnectionGraphNodes(UNetReplicationGraphConnection* ConnectionManager)
{
	Super::InitConnectionGraphNodes(ConnectionManager);

	UGameRepGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = CreateNewNode<UGameRepGraphNode_AlwaysRelevant_ForConnection>();

	// This node needs to know when client levels go in and out of visibility
	ConnectionManager->OnClientVisibleLevelNameAdd.AddUObject(AlwaysRelevantConnectionNode, &UGameRepGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd);
	ConnectionManager->OnClientVisibleLevelNameRemove.AddUObject(AlwaysRelevantConnectionNode, &UGameRepGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove);

	AddConnectionGraphNode(AlwaysRelevantConnectionNode, ConnectionManager);
}

void UGameplayReplicationGraph::RouteAddNetworkActorToNodes(
	const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
	switch (GetMappingPolicy(ActorInfo.Class))
	{
	case EClassRepNodeMapping::NotRouted:
		{
			break;
		}
		
	case EClassRepNodeMapping::RelevantAllConnections:
		{
			if (ActorInfo.StreamingLevelName == NAME_None)
			{
				AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
			}
			else
			{
				FActorRepListRefView& RepList = AlwaysRelevantStreamingLevelActors.FindOrAdd(ActorInfo.StreamingLevelName);
				RepList.ConditionalAdd(ActorInfo.Actor);
			}
			
			break;
		}
		
	case EClassRepNodeMapping::Spatialize_Static:
		{
			GridNode->AddActor_Static(ActorInfo, GlobalInfo);
			break;
		}
		
	case EClassRepNodeMapping::Spatialize_Dynamic:
		{
			GridNode->AddActor_Dynamic(ActorInfo, GlobalInfo);
			break;
		}
		
	case EClassRepNodeMapping::Spatialize_Dormancy:
		{
			GridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
			break;
		}
	}
}

void UGameplayReplicationGraph::RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo)
{
	switch (GetMappingPolicy(ActorInfo.Class)) {
	case EClassRepNodeMapping::NotRouted:
		{
			break;
		}
		
	case EClassRepNodeMapping::RelevantAllConnections:
		{
			if (ActorInfo.StreamingLevelName == NAME_None)
			{
				AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
			}
			else
			{
				FActorRepListRefView& RepList = AlwaysRelevantStreamingLevelActors.FindChecked(ActorInfo.StreamingLevelName);
				if (RepList.RemoveFast(ActorInfo.Actor) == false)
				{
					UE_LOG(LogGameRepGraph, Warning, TEXT("Actor %s was not found in AlwaysRelevantStreamingLevelActors list. LevelName: %s"), *GetActorRepListTypeDebugString(ActorInfo.Actor), *ActorInfo.StreamingLevelName.ToString());
				}					
			}

			SetActorDestructionInfoToIgnoreDistanceCulling(ActorInfo.GetActor());
			
			break;
		}
	case EClassRepNodeMapping::Spatialize_Static:
		{
			GridNode->RemoveActor_Static(ActorInfo);
			break;
		}
		
	case EClassRepNodeMapping::Spatialize_Dynamic:
		{
			GridNode->RemoveActor_Dynamic(ActorInfo);
			break;
		}
		
	case EClassRepNodeMapping::Spatialize_Dormancy:
		{
			GridNode->RemoveActor_Dormancy(ActorInfo);
			break;
		}
	}
}


void UGameplayReplicationGraph::AddClassRepInfo(UClass* Class, EClassRepNodeMapping Mapping)
{
	if (IsSpatialized(Mapping))
	{
		if (Class->GetDefaultObject<AActor>()->bAlwaysRelevant)
		{
			UE_LOG(LogGameRepGraph, Warning, TEXT("Replicated Class %s is AlwaysRelevant but is initialized into a spatialized node (%s)"),
				*Class->GetName(), *StaticEnum<EClassRepNodeMapping>()->GetNameStringByValue((int64)Mapping));
		}
	}

	ClassRepNodePolicies.Set(Class, Mapping);
}

void UGameplayReplicationGraph::RegisterClassRepNodeMapping(UClass* Class)
{
	const EClassRepNodeMapping Mapping = GetClassNodeMapping(Class);
	ClassRepNodePolicies.Set(Class, Mapping);
}

EClassRepNodeMapping UGameplayReplicationGraph::GetClassNodeMapping(UClass* Class) const
{
	if (!Class)
	{
		return EClassRepNodeMapping::NotRouted;
	}
	
	if (const EClassRepNodeMapping* Ptr = ClassRepNodePolicies.FindWithoutClassRecursion(Class))
	{
		return *Ptr;
	}
	
	AActor* ActorCDO = Cast<AActor>(Class->GetDefaultObject());
	if (!ActorCDO || !ActorCDO->GetIsReplicated())
	{
		return EClassRepNodeMapping::NotRouted;
	}
		
	auto ShouldSpatialize = [](const AActor* CDO)
	{
		return CDO->GetIsReplicated() && (!(CDO->bAlwaysRelevant || CDO->bOnlyRelevantToOwner || CDO->bNetUseOwnerRelevancy));
	};

	auto GetLegacyDebugStr = [](const AActor* CDO)
	{
		return FString::Printf(TEXT("%s [%d/%d/%d]"), *CDO->GetClass()->GetName(), CDO->bAlwaysRelevant, CDO->bOnlyRelevantToOwner, CDO->bNetUseOwnerRelevancy);
	};

	// Only handle this class if it differs from its Super. There is no need to put every child class explicitly in the graph class mapping
	UClass* SuperClass = Class->GetSuperClass();
	if (AActor* SuperCDO = Cast<AActor>(SuperClass->GetDefaultObject()))
	{
		if (SuperCDO->GetIsReplicated() == ActorCDO->GetIsReplicated()
			&& SuperCDO->bAlwaysRelevant == ActorCDO->bAlwaysRelevant
			&& SuperCDO->bOnlyRelevantToOwner == ActorCDO->bOnlyRelevantToOwner
			&& SuperCDO->bNetUseOwnerRelevancy == ActorCDO->bNetUseOwnerRelevancy
			)
		{
			return GetClassNodeMapping(SuperClass);
		}
	}

	if (ShouldSpatialize(ActorCDO))
	{
		return EClassRepNodeMapping::Spatialize_Dynamic;
	}
	else if (ActorCDO->bAlwaysRelevant && !ActorCDO->bOnlyRelevantToOwner)
	{
		return EClassRepNodeMapping::RelevantAllConnections;
	}

	return EClassRepNodeMapping::NotRouted;
}

void UGameplayReplicationGraph::RegisterClassReplicationInfo(UClass* Class)
{
	FClassReplicationInfo ClassInfo;
	if (ConditionalInitClassReplicationInfo(Class, ClassInfo))
	{
		GlobalActorReplicationInfoMap.SetClassInfo(Class, ClassInfo);
		UE_LOG(LogGameRepGraph, Log, TEXT("Class %s registered with replication info."), *Class->GetName());
		UE_LOG(LogGameRepGraph, Log, TEXT("Setting %s - %.2f"), *GetNameSafe(Class), ClassInfo.GetCullDistance());
	}
}

bool UGameplayReplicationGraph::ConditionalInitClassReplicationInfo(UClass* Class, FClassReplicationInfo& ClassInfo)
{
	if (ExplicitlySetClasses.FindByPredicate([&](const UClass* SetClass)->bool
	{
		return Class->IsChildOf(SetClass);
	}) != nullptr)
	{
		return false;
	}

	const bool bClassIsSpatialized = IsSpatialized(ClassRepNodePolicies.GetChecked(Class));
	InitClassReplicationInfo(ClassInfo, Class, bClassIsSpatialized);
	return true;
}

void UGameplayReplicationGraph::InitClassReplicationInfo(
	FClassReplicationInfo& Info, UClass* Class, bool Spatialize) const
{
	const AActor* CDO = Class->GetDefaultObject<AActor>();
	if (Spatialize)
	{
		Info.SetCullDistanceSquared(CDO->GetNetCullDistanceSquared());
		UE_LOG(LogGameRepGraph, Log, TEXT("Setting cull distance for %s to %f (%f)"),
			*Class->GetName(), Info.GetCullDistanceSquared(), Info.GetCullDistance());
	}

	Info.ReplicationPeriodFrame = GetReplicationPeriodFrameForFrequency(CDO->GetNetUpdateFrequency());

	UClass* NativeClass = Class;
	while (!NativeClass->IsNative() && NativeClass->GetSuperClass() && NativeClass->GetSuperClass() != AActor::StaticClass())
	{
		NativeClass = NativeClass->GetSuperClass();
	}

	UE_LOG(LogGameRepGraph, Log, TEXT("Setting replication period for %s (%s) to %d frames (%.2f)"),
		*Class->GetName(), *NativeClass->GetName(), Info.ReplicationPeriodFrame, CDO->GetNetUpdateFrequency());
}

EClassRepNodeMapping UGameplayReplicationGraph::GetMappingPolicy(UClass* Class)
{
	const EClassRepNodeMapping* PolicyPtr = ClassRepNodePolicies.Get(Class);
	const EClassRepNodeMapping Policy = PolicyPtr ? *PolicyPtr : EClassRepNodeMapping::NotRouted;
	return Policy;
}

// Since we listen to global (static) events, we need to watch out for cross-world broadcasts (PIE)
#if WITH_EDITOR
#define CHECK_WORLDS(X) if(X->GetWorld() != GetWorld()) return;
#else
#define CHECK_WORLDS(X)
#endif

#if WITH_GAMEPLAY_DEBUGGER
void UGameplayReplicationGraph::OnGameplayDebuggerOwnerChange(
	AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner)
{
	CHECK_WORLDS(Debugger);

	auto GetAlwaysRelevantForConnectionNode = [this](APlayerController* Controller) -> UGameRepGraphNode_AlwaysRelevant_ForConnection*
	{
		if (Controller)
		{
			if (UNetConnection* NetConnection = Controller->GetNetConnection())
			{
				if (NetConnection->GetDriver() == NetDriver)
				{
					if (UNetReplicationGraphConnection* GraphConnection = FindOrAddConnectionManager(NetConnection))
					{
						for (UReplicationGraphNode* ConnectionNode : GraphConnection->GetConnectionGraphNodes())
						{
							if (UGameRepGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = Cast<UGameRepGraphNode_AlwaysRelevant_ForConnection>(ConnectionNode))
							{
								return AlwaysRelevantConnectionNode;
							}
						}
					}
				}
			}
		}

		return nullptr;
	};

	if (UGameRepGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = GetAlwaysRelevantForConnectionNode(OldOwner))
	{
		AlwaysRelevantConnectionNode->GameplayDebugger = nullptr;
	}

	if (UGameRepGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = GetAlwaysRelevantForConnectionNode(Debugger->GetReplicationOwner()))
	{
		AlwaysRelevantConnectionNode->GameplayDebugger = Debugger;
	}
}
#endif
#undef CHECK_WORLDS


void UGameplayReplicationGraph::PrintRepNodePolicies()
{
	UEnum* Enum = StaticEnum<EClassRepNodeMapping>();
	if (!Enum)
	{
		return;
	}

	GLog->Logf(TEXT("===================================="));
	GLog->Logf(TEXT("Game Replication Routing Policies"));
	GLog->Logf(TEXT("===================================="));

	for (auto It = ClassRepNodePolicies.CreateIterator(); It; ++It)
	{
		FObjectKey ObjKey = It.Key();
		
		EClassRepNodeMapping Mapping = It.Value();

		GLog->Logf(TEXT("%-40s --> %s"), *GetNameSafe(ObjKey.ResolveObjectPtr()), *Enum->GetNameStringByValue(static_cast<uint32>(Mapping)));
	}
}

// --------------------------------------------------------------------------------------------------------------------
// Console Commands
// --------------------------------------------------------------------------------------------------------------------

FAutoConsoleCommandWithWorldAndArgs PrintRepNodePolicyCmd(TEXT("GameRepGraph.PrintRouting"),TEXT("Prints how actor classes are routed to RepGraph nodes"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		for (TObjectIterator<UGameplayReplicationGraph> It; It; ++It)
		{
			It->PrintRepNodePolicies();
		}
	})
);

FAutoConsoleCommandWithWorldAndArgs ChangeFrequencyBucketsCmd(TEXT("GameRepGraph.FrequencyBuckets"), TEXT("Resets frequency bucket count."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* World) 
{
	int32 Buckets = 1;
	if (Args.Num() > 0)
	{
		LexTryParseString<int32>(Buckets, *Args[0]);
	}

	UE_LOG(LogGameRepGraph, Display, TEXT("Setting Frequency Buckets to %d"), Buckets);
	for (TObjectIterator<UReplicationGraphNode_ActorListFrequencyBuckets> It; It; ++It)
	{
		UReplicationGraphNode_ActorListFrequencyBuckets* Node = *It;
		Node->SetNonStreamingCollectionSize(Buckets);
	}
}));


// --------------------------------------------------------------------------------------------------------------------
// UGameRepGraphNode_AlwaysRelevant_ForConnection
// --------------------------------------------------------------------------------------------------------------------

void UGameRepGraphNode_AlwaysRelevant_ForConnection::GatherActorListsForConnection(
	const FConnectionGatherActorListParameters& Params)
{
	UGameplayReplicationGraph* GameGraph = CastChecked<UGameplayReplicationGraph>(GetOuter());

	ReplicationActorList.Reset();

	for (const FNetViewer& CurViewer : Params.Viewers)
	{
		ReplicationActorList.ConditionalAdd(CurViewer.InViewer);
		ReplicationActorList.ConditionalAdd(CurViewer.ViewTarget);

		if (APlayerController* PC = Cast<APlayerController>(CurViewer.InViewer))
		{
			// 50% throttling of PlayerStates.
			const bool bReplicatePS = (Params.ConnectionManager.ConnectionOrderNum % 2) == (Params.ReplicationFrameNum % 2);
			if (bReplicatePS)
			{
				// Always return the player state to the owning player. Simulated proxy player states are handled by UGameRepGraphNode_PlayerStateFrequenceLimiter.
				if (APlayerState* PS = PC->PlayerState)
				{
					if (!bInitializedPlayerState)
					{
						bInitializedPlayerState = true;
						FConnectionReplicationActorInfo& ConnectionActorInfo = Params.ConnectionManager.ActorInfoMap.FindOrAdd(PS);
						ConnectionActorInfo.ReplicationPeriodFrame = 1;
					}

					ReplicationActorList.ConditionalAdd(PS);
				}
			}

			FCachedAlwaysRelevantActorInfo& LastData = PastRelevantActorMap.FindOrAdd(CurViewer.Connection);

			if (ACharacter* Pawn = Cast<ACharacter>(PC->GetPawn()))
			{
				UpdateCachedRelevantActor(Params, Pawn, LastData.LastViewer);

				if (Pawn != CurViewer.ViewTarget)
				{
					ReplicationActorList.ConditionalAdd(Pawn);
				}
			}

			if (ACharacter* ViewTargetPawn = Cast<ACharacter>(CurViewer.ViewTarget))
			{
				UpdateCachedRelevantActor(Params, ViewTargetPawn, LastData.LastViewTarget);
			}
		}
	}

	CleanupCachedRelevantActors(PastRelevantActorMap);

	Params.OutGatheredReplicationLists.AddReplicationActorList(ReplicationActorList);

	// Always relevant streaming level actors.
	FPerConnectionActorInfoMap& ConnectionActorInfoMap = Params.ConnectionManager.ActorInfoMap;
	
	TMap<FName, FActorRepListRefView>& AlwaysRelevantStreamingLevelActors = GameGraph->AlwaysRelevantStreamingLevelActors;

	for (int32 Idx=AlwaysRelevantStreamingLevelsNeedingReplication.Num()-1; Idx >= 0; --Idx)
	{
		const FName& StreamingLevel = AlwaysRelevantStreamingLevelsNeedingReplication[Idx];

		FActorRepListRefView* Ptr = AlwaysRelevantStreamingLevelActors.Find(StreamingLevel);
		if (Ptr == nullptr)
		{
			// No always relevant lists for that level
			UE_CLOG(GameplayRepGraph::DisplayClientLevelStreaming > 0, LogGameRepGraph, Display, TEXT("CLIENTSTREAMING Removing %s from AlwaysRelevantStreamingLevelActors because FActorRepListRefView is null. %s "), *StreamingLevel.ToString(),  *Params.ConnectionManager.GetName());
			AlwaysRelevantStreamingLevelsNeedingReplication.RemoveAtSwap(Idx, 1, EAllowShrinking::No);
			continue;
		}

		FActorRepListRefView& RepList = *Ptr;

		if (RepList.Num() > 0)
		{
			bool bAllDormant = true;
			for (FActorRepListType Actor : RepList)
			{
				FConnectionReplicationActorInfo& ConnectionActorInfo = ConnectionActorInfoMap.FindOrAdd(Actor);
				if (ConnectionActorInfo.bDormantOnConnection == false)
				{
					bAllDormant = false;
					break;
				}
			}

			if (bAllDormant)
			{
				UE_CLOG(GameplayRepGraph::DisplayClientLevelStreaming > 0, LogGameRepGraph, Display, TEXT("CLIENTSTREAMING All AlwaysRelevant Actors Dormant on StreamingLevel %s for %s. Removing list."), *StreamingLevel.ToString(), *Params.ConnectionManager.GetName());
				AlwaysRelevantStreamingLevelsNeedingReplication.RemoveAtSwap(Idx, 1, EAllowShrinking::No);
			}
			else
			{
				UE_CLOG(GameplayRepGraph::DisplayClientLevelStreaming > 0, LogGameRepGraph, Display, TEXT("CLIENTSTREAMING Adding always Actors on StreamingLevel %s for %s because it has at least one non dormant actor"), *StreamingLevel.ToString(), *Params.ConnectionManager.GetName());
				Params.OutGatheredReplicationLists.AddReplicationActorList(RepList);
			}
		}
		else
		{
			UE_LOG(LogGameRepGraph, Warning, TEXT("UGameRepGraphNode_AlwaysRelevant_ForConnection::GatherActorListsForConnection - empty RepList %s"), *Params.ConnectionManager.GetName());
		}

	}

#if WITH_GAMEPLAY_DEBUGGER
	if (GameplayDebugger)
	{
		ReplicationActorList.ConditionalAdd(GameplayDebugger);
	}
#endif
}

void UGameRepGraphNode_AlwaysRelevant_ForConnection::LogNode(
	FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const
{
	DebugInfo.Log(NodeName);
	DebugInfo.PushIndent();
	LogActorRepList(DebugInfo, NodeName, ReplicationActorList);

	for (const FName& LevelName : AlwaysRelevantStreamingLevelsNeedingReplication)
	{
		UGameplayReplicationGraph* GameGraph = CastChecked<UGameplayReplicationGraph>(GetOuter());
		if (FActorRepListRefView* RepList = GameGraph->AlwaysRelevantStreamingLevelActors.Find(LevelName))
		{
			LogActorRepList(DebugInfo, FString::Printf(TEXT("AlwaysRelevant StreamingLevel List: %s"), *LevelName.ToString()), *RepList);
		}
	}

	DebugInfo.PopIndent();
}

void UGameRepGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd(FName LevelName, UWorld* StreamingWorld)
{
	UE_CLOG(GameplayRepGraph::DisplayClientLevelStreaming > 0, LogGameRepGraph, Display, TEXT("CLIENTSTREAMING Adding %s to AlwaysRelevantStreamingLevelActors for %s"), *LevelName.ToString(), *GetNameSafe(StreamingWorld));
	AlwaysRelevantStreamingLevelsNeedingReplication.Add(LevelName);
}

void UGameRepGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove(FName LevelName)
{
	UE_CLOG(GameplayRepGraph::DisplayClientLevelStreaming > 0, LogGameRepGraph, Display, TEXT("CLIENTSTREAMING Removing %s from AlwaysRelevantStreamingLevelActors for %s"), *LevelName.ToString(), *GetNameSafe(GetOuter()));
	AlwaysRelevantStreamingLevelsNeedingReplication.Remove(LevelName);
}

void UGameRepGraphNode_AlwaysRelevant_ForConnection::ResetGameWorldState()
{
	ReplicationActorList.Reset();
	AlwaysRelevantStreamingLevelsNeedingReplication.Empty();
}


// --------------------------------------------------------------------------------------------------------------------
// UGameRepGraphNode_PlayerStateFrequencyLimiter
// --------------------------------------------------------------------------------------------------------------------

UGameRepGraphNode_PlayerStateFrequencyLimiter::UGameRepGraphNode_PlayerStateFrequencyLimiter()
{
	bRequiresPrepareForReplicationCall = true;
}

void UGameRepGraphNode_PlayerStateFrequencyLimiter::GatherActorListsForConnection(
	const FConnectionGatherActorListParameters& Params)
{
	const int32 ListIdx = Params.ReplicationFrameNum % ReplicationActorLists.Num();
	Params.OutGatheredReplicationLists.AddReplicationActorList(ReplicationActorLists[ListIdx]);

	if (ForceNetUpdateReplicationActorList.Num() > 0)
	{
		Params.OutGatheredReplicationLists.AddReplicationActorList(ForceNetUpdateReplicationActorList);
	}	
}

void UGameRepGraphNode_PlayerStateFrequencyLimiter::PrepareForReplication()
{
	ReplicationActorLists.Reset();
	ForceNetUpdateReplicationActorList.Reset();

	ReplicationActorLists.AddDefaulted();
	FActorRepListRefView* CurrentList = &ReplicationActorLists[0];

	// We rebuild our lists of player states each frame. This is not as efficient as it could be but its the simplest way
	// to handle players disconnecting and keeping the lists compact. If the lists were persistent we would need to defrag them as players left.
	for (TActorIterator<APlayerState> It(GetWorld()); It; ++It)
	{
		APlayerState* PS = *It;
		if (IsActorValidForReplicationGather(PS) == false)
		{
			continue;
		}

		if (CurrentList->Num() >= TargetActorsPerFrame)
		{
			ReplicationActorLists.AddDefaulted();
			CurrentList = &ReplicationActorLists.Last(); 
		}
		
		CurrentList->Add(PS);
	}	
}

void UGameRepGraphNode_PlayerStateFrequencyLimiter::LogNode(FReplicationGraphDebugInfo& DebugInfo,
	const FString& NodeName) const
{
	DebugInfo.Log(NodeName);
	DebugInfo.PushIndent();	

	int32 i=0;
	for (const FActorRepListRefView& List : ReplicationActorLists)
	{
		LogActorRepList(DebugInfo, FString::Printf(TEXT("Bucket[%d]"), i++), List);
	}

	DebugInfo.PopIndent();
}
