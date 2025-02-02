// Copyright © 2024 Playton. All Rights Reserved.


#include "GameplayReplicationGraphSettings.h"

#include "GameplayReplicationGraph.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameplayReplicationGraphSettings)

UGameplayReplicationGraphSettings::UGameplayReplicationGraphSettings()
{
	CategoryName = TEXT("Game");
	DefaultReplicationGraphClass = UGameplayReplicationGraph::StaticClass();
	BasePawnClass = ACharacter::StaticClass();
}
