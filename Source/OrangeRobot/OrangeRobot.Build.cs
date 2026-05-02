// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OrangeRobot : ModuleRules
{
	public OrangeRobot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			"EnhancedInput",
			// 物理约束组件所需
			"PhysicsCore", 
			"Schola", 
			"ScholaNNE", 
			"ScholaInferenceUtils",
			"NNEOnnxruntime",
			"NNE",
			"Json", 
			"JsonUtilities"
			
        });

		PrivateDependencyModuleNames.AddRange(new string[] {
			// StaticMeshComponent 等渲染/物理组件
			"RenderCore"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
