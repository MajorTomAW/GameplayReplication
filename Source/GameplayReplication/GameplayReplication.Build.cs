// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameplayReplication : ModuleRules
{
	public GameplayReplication(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new []
		{ 
			"Core",
			"DeveloperSettings",
			"NetCore",
			"ReplicationGraph",
			"GameplayDebugger",
		});
			
		
		PrivateDependencyModuleNames.AddRange(new []
		{ 
			"CoreUObject", 
			"Engine",
		});
	}
}
