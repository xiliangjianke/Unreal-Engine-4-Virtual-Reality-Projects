// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGlobalSettings.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Delegates/DelegateInstanceInterface.h"
#include "Templates/TypeWrapper.h"
#include "Misc/Crc.h"
#include "UObject/NameTypes.h"	
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "Engine/Classes/GameFramework/PlayerController.h"
#include "Engine/Classes/GameFramework/PlayerState.h"
#include "Engine/Player.h"
#include "PhysicsEngine/PhysicsSettings.h"
#if WITH_PHYSX
#include "Physics/PhysicsInterfaceUtils.h"
#include "Physics/PhysScene_PhysX.h"
//#include "Physics/Experimental/PhysScene_ImmediatePhysX.h"
#include "PhysicsReplication.h"
#endif
#include <functional>
#include <vector>

#include "GrippablePhysicsReplication.generated.h"
//#include "GrippablePhysicsReplication.generated.h"


//DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRPhysicsReplicationDelegate, void, Return);

/*static TAutoConsoleVariable<int32> CVarEnableCustomVRPhysicsReplication(
	TEXT("vr.VRExpansion.EnableCustomVRPhysicsReplication"),
	0,
	TEXT("Enable valves input controller that overrides legacy input.\n")
	TEXT(" 0: use the engines default input mapping (default), will also be default if vr.SteamVR.EnableVRInput is enabled\n")
	TEXT(" 1: use the valve input controller. You will have to define input bindings for the controllers you want to support."),
	ECVF_ReadOnly);*/

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UVRReplicationInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};


class VREXPANSIONPLUGIN_API IVRReplicationInterface
{
	GENERATED_IINTERFACE_BODY()

public:

	// Runs the replication tick, returns if replication is still ongoing
	virtual bool PollReplicationEvent(float DeltaTime) = 0;


	static bool AddObjectToReplicationManager(uint32 UpdateHTZ, UObject * ObjectToAdd);
	static bool RemoveObjectFromReplicationManager(UObject * ObjectToRemove);
};

USTRUCT()
struct VREXPANSIONPLUGIN_API FReplicationBucket
{
	GENERATED_BODY()
public:
	float nUpdateRate;
	float nUpdateCount;

	TArray<TWeakObjectPtr<UObject>> CallbackReferences;

	bool Update(float DeltaTime)
	{
		//#TODO: Need to consider batching / spreading out load if there are a lot of updating objects

		// Check for if this bucket is ready to fire events
		nUpdateCount += DeltaTime;
		if (nUpdateCount >= nUpdateRate)
		{
			nUpdateCount = 0.0f;
			for (int i = CallbackReferences.Num() - 1; i >= 0; --i)
			{
				if (CallbackReferences[i].IsValid() && !CallbackReferences[i]->IsPendingKill())
				{
					if (IVRReplicationInterface * ASI = Cast<IVRReplicationInterface>(CallbackReferences[i]))
					{
						if (ASI->PollReplicationEvent(DeltaTime))
						{
							// Skip deleting the entry, it still wants to run
							continue;
						}
					}
				}

				// Remove the callback, it is complete or invalid
				CallbackReferences.RemoveAt(i);
			}
		}

		return (CallbackReferences.Num() > 0);
	}

	FReplicationBucket() {}

	FReplicationBucket(uint32 UpdateHTZ) :
		nUpdateRate(1.0f/UpdateHTZ),
		nUpdateCount(0.0f)
	{
		int vv = 0;
	}
};

USTRUCT()//Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
struct VREXPANSIONPLUGIN_API FReplicationBucketContainer
{
	GENERATED_BODY()
public:


	bool bNeedsUpdate;
	TMap<uint32, FReplicationBucket> ReplicationBuckets;

	void UpdateBuckets(float DeltaTime)
	{
		TArray<uint32> BucketsToRemove;
		for(auto& Bucket : ReplicationBuckets)
		{
			if (!Bucket.Value.Update(DeltaTime))
			{
				// Delete empty bucket here, need to run this loop in reverse
				BucketsToRemove.Add(Bucket.Key);
			}
		}

		// Remove unused buckets so that they don't get ticked
		for(const uint32 Key : BucketsToRemove)
		{
			ReplicationBuckets.Remove(Key);
		}

		if (ReplicationBuckets.Num() < 1)
			bNeedsUpdate = false;
	}
	
	bool AddReplicatingObject(uint32 UpdateHTZ, UObject* InObject)
	{
		if (!InObject)
			return false;

		// First verify that this object isn't already contained in a bucket, if it is then erase it so that we can replace it below
		RemoveReplicatingObject(InObject);

		if (IVRReplicationInterface * ReplicationInterface = Cast<IVRReplicationInterface>(InObject))
		{
			if (ReplicationBuckets.Contains(UpdateHTZ))
			{
				ReplicationBuckets[UpdateHTZ].CallbackReferences.Add(MakeWeakObjectPtr(InObject));
			}
			else
			{
				FReplicationBucket & newBucket = ReplicationBuckets.Add(UpdateHTZ, FReplicationBucket(UpdateHTZ));
				ReplicationBuckets[UpdateHTZ].CallbackReferences.Add(MakeWeakObjectPtr(InObject));
			}

			if (ReplicationBuckets.Num() > 0)
				bNeedsUpdate = true;

			return true;
		}

		return false;
	}
	/*
	template<typename classType>
	bool AddReplicatingObject(uint32 UpdateHTZ, classType* InObject, void(classType::* _Func)())
	{
	}
	*/

	bool RemoveReplicatingObject(UObject* ObjectToRemoveFromQueue)
	{
		// Store if we ended up removing it
		bool bRemovedObject = false;

		//TWeakObjectPtr<UObject> *FoundValue;
		TArray<uint32> BucketsToRemove;
		for (auto& Bucket : ReplicationBuckets)
		{
			for (int i = Bucket.Value.CallbackReferences.Num() - 1; i >= 0; --i)
			{
				if (Bucket.Value.CallbackReferences[i].Get() == ObjectToRemoveFromQueue)
				{
					Bucket.Value.CallbackReferences.RemoveAt(i);
					bRemovedObject = true;

					if (Bucket.Value.CallbackReferences.Num() < 1)
					{
						BucketsToRemove.Add(Bucket.Key);
					}

					// Leave the loop, this is called in add as well so we should never get duplicate entries
					break;
				}
			}

			if (bRemovedObject)
			{
				break;
			}
		}

		// Remove unused buckets so that they don't get ticked
		for (const uint32 Key : BucketsToRemove)
		{
			ReplicationBuckets.Remove(Key);
		}

		if (ReplicationBuckets.Num() < 1)
			bNeedsUpdate = false;

		return bRemovedObject;
	}

	FReplicationBucketContainer() :
		bNeedsUpdate(false)
	{
	};

};

#if WITH_PHYSX
class FPhysicsReplicationVR : public FPhysicsReplication
{
public:

	FReplicationBucketContainer BucketContainer;

	FPhysicsReplicationVR(FPhysScene* PhysScene);

	virtual void OnTick(float DeltaSeconds, TMap<TWeakObjectPtr<UPrimitiveComponent>, FReplicatedPhysicsTarget>& ComponentsToTargets) override
	{
		if(BucketContainer.bNeedsUpdate)
			BucketContainer.UpdateBuckets(DeltaSeconds);

		// Skip all of the custom logic if we aren't the server
		if (const UWorld* World = GetOwningWorld())
		{
			if (World->GetNetMode() == ENetMode::NM_Client)
			{
				return FPhysicsReplication::OnTick(DeltaSeconds, ComponentsToTargets);
			}
		}

		const FRigidBodyErrorCorrection& PhysicErrorCorrection = UPhysicsSettings::Get()->PhysicErrorCorrection;

		// Get the ping between this PC & the server
		const float LocalPing = 0.0f;//GetLocalPing();

		float CurrentTimeSeconds = 0.0f;

		if (UWorld* OwningWorld = GetOwningWorld())
		{
			CurrentTimeSeconds = OwningWorld->GetTimeSeconds();
		}

		for (auto Itr = ComponentsToTargets.CreateIterator(); Itr; ++Itr)
		{

			// Its been more than half a second since the last update, lets cease using the target as a failsafe
			// Clients will never update with that much latency, and if they somehow are, then they are dropping so many
			// packets that it will be useless to use their data anyway
			if ((CurrentTimeSeconds - Itr.Value().ArrivedTimeSeconds) > 0.5f)
			{
				OnTargetRestored(Itr.Key().Get(), Itr.Value());
				Itr.RemoveCurrent();
			}
			else if (UPrimitiveComponent* PrimComp = Itr.Key().Get())
			{
				bool bRemoveItr = false;

				if (FBodyInstance* BI = PrimComp->GetBodyInstance(Itr.Value().BoneName))
				{
					FReplicatedPhysicsTarget& PhysicsTarget = Itr.Value();
					FRigidBodyState& UpdatedState = PhysicsTarget.TargetState;
					bool bUpdated = false;
					if (AActor* OwningActor = PrimComp->GetOwner())
					{
						// Deleted everything here, we will always be the server, I already filtered out clients to default logic
						{
							/*const*/ float OwnerPing = 0.0f;// GetOwnerPing(OwningActor, PhysicsTarget);
					
							/*if (UPlayer* OwningPlayer = OwningActor->GetNetOwningPlayer())
							{
								if (APlayerController* PlayerController = OwningPlayer->GetPlayerController(nullptr))
								{
									if (APlayerState* PlayerState = PlayerController->PlayerState)
									{
										OwnerPing = PlayerState->ExactPing;
									}
								}
							}*/

							// Get the total ping - this approximates the time since the update was
							// actually generated on the machine that is doing the authoritative sim.
							// NOTE: We divide by 2 to approximate 1-way ping from 2-way ping.
							const float PingSecondsOneWay = 0.0f;// (LocalPing + OwnerPing) * 0.5f * 0.001f;


							if (UpdatedState.Flags & ERigidBodyFlags::NeedsUpdate)
							{
								const bool bRestoredState = ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, PhysicErrorCorrection, PingSecondsOneWay);

								// Need to update the component to match new position.
								static const auto CVarSkipSkeletalRepOptimization = IConsoleManager::Get().FindConsoleVariable(TEXT("p.SkipSkeletalRepOptimization"));
								if (/*PhysicsReplicationCVars::SkipSkeletalRepOptimization*/CVarSkipSkeletalRepOptimization->GetInt() == 0 || Cast<USkeletalMeshComponent>(PrimComp) == nullptr)	//simulated skeletal mesh does its own polling of physics results so we don't need to call this as it'll happen at the end of the physics sim
								{
									PrimComp->SyncComponentToRBPhysics();
								}

								// Added a sleeping check from the input state as well, we always want to cease activity on sleep
								if (bRestoredState || ((UpdatedState.Flags & ERigidBodyFlags::Sleeping) != 0))
								{
									bRemoveItr = true;
								}
							}
						}
					}
				}

				if (bRemoveItr)
				{
					OnTargetRestored(Itr.Key().Get(), Itr.Value());
					Itr.RemoveCurrent();
				}
			}
		}

		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Phys Rep Tick!"));
		//FPhysicsReplication::OnTick(DeltaSeconds, ComponentsToTargets);
	}
};

class IPhysicsReplicationFactoryVR : public IPhysicsReplicationFactory
{
public:

	virtual FPhysicsReplication* Create(FPhysScene* OwningPhysScene)
	{
		return new FPhysicsReplicationVR(OwningPhysScene);
	}

	virtual void Destroy(FPhysicsReplication* PhysicsReplication)
	{
		if (PhysicsReplication)
			delete PhysicsReplication;
	}
};
#endif

USTRUCT()
struct VREXPANSIONPLUGIN_API FRepMovementVR : public FRepMovement
{
	GENERATED_USTRUCT_BODY()
public:

		FRepMovementVR() : FRepMovement()
		{
			LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
			VelocityQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
			RotationQuantizationLevel = ERotatorQuantization::ShortComponents;
		}

		FRepMovementVR(FRepMovement & other) : FRepMovement()
		{
			FRepMovementVR();

			LinearVelocity = other.LinearVelocity;
			AngularVelocity = other.AngularVelocity;
			Location = other.Location;
			Rotation = other.Rotation;
			bSimulatedPhysicSleep = other.bSimulatedPhysicSleep;
			bRepPhysics = other.bRepPhysics;
		}
		
		void CopyTo(FRepMovement &other) const
		{
			other.LinearVelocity = LinearVelocity;
			other.AngularVelocity = AngularVelocity;
			other.Location = Location;
			other.Rotation = Rotation;
			other.bSimulatedPhysicSleep = bSimulatedPhysicSleep;
			other.bRepPhysics = bRepPhysics;
		}

		bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
		{
			return FRepMovement::NetSerialize(Ar, Map, bOutSuccess);
		}

		bool GatherActorsMovement(AActor * OwningActor)
		{
			//if (/*bReplicateMovement || (RootComponent && RootComponent->GetAttachParent())*/)
			{
				UPrimitiveComponent* RootPrimComp = Cast<UPrimitiveComponent>(OwningActor->GetRootComponent());
				if (RootPrimComp && RootPrimComp->IsSimulatingPhysics())
				{
					FRigidBodyState RBState;
					RootPrimComp->GetRigidBodyState(RBState);

					FillFrom(RBState, OwningActor);
					// Don't replicate movement if we're welded to another parent actor.
					// Their replication will affect our position indirectly since we are attached.
					bRepPhysics = !RootPrimComp->IsWelded();
				}
				else if (RootPrimComp != nullptr)
				{
					// If we are attached, don't replicate absolute position, use AttachmentReplication instead.
					if (RootPrimComp->GetAttachParent() != nullptr)
					{
						return false; // We don't handle attachment rep

					}
					else
					{
						Location = FRepMovement::RebaseOntoZeroOrigin(RootPrimComp->GetComponentLocation(), OwningActor);
						Rotation = RootPrimComp->GetComponentRotation();
						LinearVelocity = OwningActor->GetVelocity();
						AngularVelocity = FVector::ZeroVector;
					}

					bRepPhysics = false;
				}
			}

			/*if (const UWorld* World = GetOwningWorld())
			{
				if (APlayerController* PlayerController = World->GetFirstPlayerController())
				{
					if (APlayerState* PlayerState = PlayerController->PlayerState)
					{
						CurrentPing = PlayerState->ExactPing;
					}
				}
			}*/

			return true;
		}
};

template<>
struct TStructOpsTypeTraits<FRepMovementVR> : public TStructOpsTypeTraitsBase2<FRepMovementVR>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

USTRUCT(BlueprintType)
struct VREXPANSIONPLUGIN_API FVRClientAuthReplicationData
{
	GENERATED_BODY()
public:

	// If True and we are using a client auth grip type then we will replicate our throws on release
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRReplication")
		bool bUseClientAuthThrowing;

	// Rate that we will be sending throwing events to the server, not replicated, only serialized
	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadOnly, Category = "VRReplication", meta = (ClampMin = "0", UIMin = "0", ClampMax = "100", UIMax = "100"))
		int32 UpdateRate;

	FTimerHandle ResetReplicationHandle;
	FTransform LastActorTransform;
	float TimeAtInitialThrow;
	bool bIsCurrentlyClientAuth;

	FVRClientAuthReplicationData() :
		bUseClientAuthThrowing(false),
		UpdateRate(30),
		LastActorTransform(FTransform::Identity),
		TimeAtInitialThrow(0.0f),
		bIsCurrentlyClientAuth(false)
	{

	}
};
