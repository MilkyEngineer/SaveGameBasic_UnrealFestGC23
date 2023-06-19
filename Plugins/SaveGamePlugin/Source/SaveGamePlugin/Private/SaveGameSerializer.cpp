// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "SaveGameSerializer.h"

#include "SaveGameFunctionLibrary.h"
#include "SaveGameObject.h"
#include "SaveGameSubsystem.h"
#include "SaveGameVersion.h"

#include "SaveGameSystem.h"
#include "PlatformFeatures.h"

#define LEVEL_SUBPATH_PREFIX TEXT("PersistentLevel.")

template<bool bLoading>
FORCEINLINE_DEBUGGABLE void SerializeCompressedData(FArchive& Ar, TArray<uint8>& Data)
{
	check(Ar.IsLoading() == bLoading);
	
	int64 UncompressedSize;

	if (!bLoading)
	{
		UncompressedSize = Data.Num();
	}

	Ar << UncompressedSize;
	
	if (bLoading)
	{
		Data.SetNumUninitialized(UncompressedSize);
	}

	Ar.SerializeCompressed(Data.GetData(), UncompressedSize, NAME_Zlib);
}

template <bool bIsLoading, bool bIsTextFormat>
TSaveGameSerializer<bIsLoading, bIsTextFormat>::TSaveGameSerializer(USaveGameSubsystem* InSaveGameSubsystem)
	: SaveGameSubsystem(InSaveGameSubsystem)
	, Archive(Data)
	, ProxyArchive(Archive)
	, Formatter(ProxyArchive)
	, StructuredArchive(Formatter)
	, RootSlot(StructuredArchive.Open())
	, RootRecord(RootSlot.EnterRecord())
	, VersionOffset(0)
{
	static_cast<FArchive&>(ProxyArchive).SetIsTextFormat(bIsTextFormat);

	// Ensure that we're using the latest save game version
	Archive.UsingCustomVersion(FSaveGameVersion::GUID);
}

template <bool bIsLoading, bool bIsTextFormat>
bool TSaveGameSerializer<bIsLoading, bIsTextFormat>::Save()
{
	check(!bIsLoading);
	
	if (ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem())
	{
		SerializeHeader();
		SerializeActors();
		SerializeDestroyedActors();

		if (!bIsTextFormat)
		{
			// Store the version position so that we can serialize it in the header
			VersionOffset = Archive.Tell();
		}
		
		SerializeVersions();

		if (!bIsTextFormat)
		{
			// We've updated the VersionOffset, let's go back to the start and rewrite the header
			Archive.Seek(0);
			SerializeHeader();
		}

		// Be sure to close this, as you'll be missing closed braces for JSON archives
		StructuredArchive.Close();
		
		if (!bIsTextFormat && !bIsLoading)
		{
			// Compress the save game data
			TArray<uint8> CompressedData;
			FSaveGameMemoryArchive CompressorArchive(CompressedData);
			SerializeCompressedData<false>(CompressorArchive, Data);

			return SaveSystem->SaveGame(false, *GetSaveName(), 0, CompressedData);
		}
		
		return SaveSystem->SaveGame(false, *GetSaveName(), 0, Data);
	}

	return false;
}

template <bool bIsLoading, bool bIsTextFormat>
bool TSaveGameSerializer<bIsLoading, bIsTextFormat>::Load()
{
	check(bIsLoading && !bIsTextFormat);

	TArray<uint8> CompressedData;
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (SaveSystem && SaveSystem->LoadGame(false, *GetSaveName(), 0, CompressedData))
	{
		// Decompress the loaded save game data
		FSaveGameMemoryArchive CompressorArchive(CompressedData);
		SerializeCompressedData<true>(CompressorArchive, Data);
		
		SerializeHeader();
		
		{
			const uint64 InitialPosition = Archive.Tell();

			// After serializing versions, go back to initial position
			ON_SCOPE_EXIT
			{
				Archive.Seek(InitialPosition);
			};

			Archive.Seek(VersionOffset);
			SerializeVersions();
		}

		// If we don't have a map, we should bail
		if (MapName.IsEmpty())
		{
			return false;
		}

		check(SaveGameSubsystem.IsValid());
		UWorld* World = SaveGameSubsystem->GetWorld();

		if (World->IsInSeamlessTravel())
		{
			return false;
		}

		// When our map has loaded, call the OnMapLoad method
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddThreadSafeSP(this, &TSaveGameSerializer::OnMapLoad);
		World->SeamlessTravel(MapName, true);

		return true;
	}

	return false;
}

template <bool bIsLoading, bool bIsTextFormat>
FString TSaveGameSerializer<bIsLoading, bIsTextFormat>::GetSaveName()
{
	FString SaveName = TEXT("SaveGame");

	if (bIsTextFormat)
	{
		SaveName += TEXT(".json");
	}

	return SaveName;
}

template <bool bIsLoading, bool bIsTextFormat>
void TSaveGameSerializer<bIsLoading, bIsTextFormat>::OnMapLoad(UWorld* World)
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	check(SaveGameSubsystem->GetWorld() == World);

	// Actually serialize the actors
	SerializeActors();
	SerializeDestroyedActors();

	SaveGameSubsystem->OnLoadCompleted();
}

template <bool bIsLoading, bool bIsTextFormat>
void TSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeHeader()
{
	// If we already have a map name, don't change it
	if (!bIsLoading && MapName.IsEmpty())
	{
		check(SaveGameSubsystem.IsValid());
		const UWorld* World = SaveGameSubsystem->GetWorld();

		MapName = World->GetOutermost()->GetLoadedPath().GetPackageName();
	}
	
	RootRecord << SA_VALUE(TEXT("Map"), MapName);

	if (!bIsTextFormat)
	{
		// We're a binary archive, so let's serialize where the version is
		// so that we can read it before loading anything
		RootRecord << SA_VALUE(TEXT("VersionsOffset"), VersionOffset);
	}
}

template<bool bIsLoading, bool bIsTextFormat>
void TSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeActors()
{
	// This serialize method assumes that we don't have any streamed/sub levels
	check(SaveGameSubsystem.IsValid());
	UWorld* World = SaveGameSubsystem->GetWorld();
	const FTopLevelAssetPath LevelAssetPath(World->GetCurrentLevel()->GetPackage()->GetFName(), World->GetCurrentLevel()->GetOuter()->GetFName());

	int32 NumActors;
	TArray<AActor*> Actors;
	TMap<FGuid, AActor*> SpawnIDs;
	
	const uint64 ActorsPosition = Archive.Tell();
	const FArchiveFieldName ActorsFieldName(TEXT("Actors"));
	
	if (bIsLoading)
	{
		// Iterate through our live actors so that we can map their SpawnIDs
		for (const TWeakObjectPtr<AActor>& ActorPtr : SaveGameSubsystem->SaveGameActors)
		{
			AActor* Actor = ActorPtr.Get();
			if (IsValid(Actor) && Actor->Implements<USaveGameSpawnActor>())
			{
				const FGuid SpawnID = ISaveGameSpawnActor::Execute_GetSpawnID(Actor);

				if (SpawnID.IsValid())
				{
					SpawnIDs.Add(SpawnID, Actor);
				}
			}
		}
		
		FStructuredArchive::FMap ActorMap = RootRecord.EnterMap(ActorsFieldName, NumActors);

		Actors.SetNumZeroed(NumActors);

		// Iterate through the saved actors and spawn or find their live equivalent
		for (int32 ActorIdx = 0; ActorIdx < NumActors; ++ActorIdx)
		{
			AActor*& Actor = Actors[ActorIdx];

			// Populate our actors list with spawned actors or level references to actors
			SerializeActor(ActorMap, Actor, [&](const FString& ActorName, const FSoftClassPath& Class, const FGuid& SpawnID, FStructuredArchive::FSlot&)
			{
				ensureAlways(!ActorName.IsEmpty());

				if (Class.IsNull())
				{
					// This is a loaded actor (is a level actor), let's find it
					Actor = FindObjectFast<AActor>(World->GetCurrentLevel(), *ActorName);
				}
				else if (SpawnID.IsValid() && SpawnIDs.Contains(SpawnID))
				{
					Actor = SpawnIDs[SpawnID];
				}
				else
				{
					UClass* ActorClass = Class.TryLoadClass<AActor>();

					// This is a spawned actor, let's spawn it
					FActorSpawnParameters SpawnParameters;

					// If we were handling levels, specify it here
					SpawnParameters.OverrideLevel = World->GetCurrentLevel();
					SpawnParameters.Name = *ActorName;
					SpawnParameters.bNoFail = true;
					
					Actor = World->SpawnActor(ActorClass, nullptr, nullptr, SpawnParameters);

					if (SpawnID.IsValid() && Actor->Implements<USaveGameSpawnActor>())
					{
						ISaveGameSpawnActor::Execute_SetSpawnID(Actor, SpawnID);
					}
				}

				if (SpawnID.IsValid())
				{
					const FString ActorSubPath = LEVEL_SUBPATH_PREFIX + ActorName;
					
					// We potentially have a spawned actor that other actors reference, be sure to update its redirects
					ProxyArchive.AddRedirect(FSoftObjectPath(LevelAssetPath, ActorSubPath), FSoftObjectPath(Actor));
				}
				
				check(IsValid(Actor));
			});
		}
	}
	else
	{
		NumActors = SaveGameSubsystem->SaveGameActors.Num();
	}
	
	{
		if (bIsLoading && !bIsTextFormat)
		{
			// Go back to the start of the actor data
			Archive.Seek(ActorsPosition);
		}
		
		FStructuredArchive::FMap ActorMap = RootRecord.EnterMap(ActorsFieldName, NumActors);

		auto ActorsIt = SaveGameSubsystem->SaveGameActors.CreateConstIterator();
		
		// Actually serialize the actor data and their properties
		for (int32 ActorIdx = 0; ActorIdx < NumActors; ++ActorIdx)
		{
			AActor* Actor;
			
			if (bIsLoading)
			{
				Actor = Actors[ActorIdx];
			}
			else
			{
				Actor = ActorsIt->Get();
				++ActorsIt;
			}

			check(IsValid(Actor));
			
			// Do the actual serialization of the properties
			SerializeActor(ActorMap, Actor, [&](const FString&, const FSoftClassPath&, const FGuid& SpawnID, FStructuredArchive::FSlot& ActorSlot)
			{
				Actor->SerializeScriptProperties(ActorSlot.EnterAttribute(TEXT("Properties")));

				FStructuredArchive::FSlot CustomDataSlot = ActorSlot.EnterAttribute(TEXT("Data"));
				FStructuredArchive::FRecord CustomDataRecord = CustomDataSlot.EnterRecord();

				// Encapsulate the record in something a Blueprint can access 
				FSaveGameArchive SaveGameArchive(CustomDataRecord);
								
				ISaveGameObject::Execute_OnSerialize(Actor, SaveGameArchive, bIsLoading);
			});
		}
	}
}

template <bool bIsLoading, bool bIsTextFormat>
void TSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeDestroyedActors()
{
	check(SaveGameSubsystem.IsValid());
	const UWorld* World = SaveGameSubsystem->GetWorld();
	
	int32 NumDestroyedActors;

	if (!bIsLoading)
	{
		NumDestroyedActors = SaveGameSubsystem->DestroyedLevelActors.Num();
	}

	FStructuredArchive::FArray DestroyedActorsArray = RootRecord.EnterArray(TEXT("DestroyedActors"), NumDestroyedActors);

	if (bIsLoading)
	{
		// Allocate our expected number of actors
		SaveGameSubsystem->DestroyedLevelActors.Reset();
		SaveGameSubsystem->DestroyedLevelActors.Reserve(NumDestroyedActors);
	}

	auto DestroyedActorsIt = SaveGameSubsystem->DestroyedLevelActors.CreateConstIterator();
	for (int32 ActorIdx = 0; ActorIdx < NumDestroyedActors; ++ActorIdx)
	{
		FName ActorName;

		if (!bIsLoading)
		{
			// Only store the object name without the prefix and full path
			FString ActorSubPath = (*DestroyedActorsIt).GetSubPathString();
			ActorSubPath.RemoveFromStart(LEVEL_SUBPATH_PREFIX);
			ActorName = *ActorSubPath;

			++DestroyedActorsIt;
		}
		
		DestroyedActorsArray.EnterElement() << ActorName;

		if (bIsLoading)
		{
			// Find the live actor in the level
			if (AActor* DestroyedActor = FindObjectFast<AActor>(World->GetCurrentLevel(), ActorName))
			{
				// Be sure to add any valid destroyed actors back into the array for saving later!
				SaveGameSubsystem->DestroyedLevelActors.Add(DestroyedActor);
				
				DestroyedActor->Destroy();
			}
		}
	}
}

template <bool bIsLoading, bool bIsTextFormat>
void TSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeVersions()
{
	FCustomVersionContainer VersionContainer;
	
	if (!bIsLoading)
	{
		// Grab a copy of our archive's current versions
		VersionContainer = Archive.GetCustomVersions();
	}

	VersionContainer.Serialize(RootRecord.EnterField(TEXT("Versions")));

	if (bIsLoading)
	{
		// Assign our serialized versions
		Archive.SetCustomVersions(VersionContainer);
	}
}

template <bool bIsLoading, bool bIsTextFormat>
void TSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeActor(FStructuredArchive::FMap& ActorMap, AActor*& Actor, TFunction<void(const FString&, const FSoftClassPath&, const FGuid&, FStructuredArchive::FSlot&)>&& BodyFunction)
{
	FString ActorName;
	FSoftClassPath Class;
	FGuid SpawnID;

	if (!bIsLoading)
	{
		ActorName = Actor->GetName();
				
		if (!USaveGameFunctionLibrary::WasObjectLoaded(Actor))
		{
			// We're a spawned actor, stash the class
			Class = Actor->GetClass();
		}

		if (Actor->Implements<USaveGameSpawnActor>())
		{
			SpawnID = ISaveGameSpawnActor::Execute_GetSpawnID(Actor);
		}
	}

	FStructuredArchive::FSlot ActorSlot = ActorMap.EnterElement(ActorName);

	// If we have a class, we're a spawned actor
	if (TOptional<FStructuredArchive::FSlot> ClassSlot = ActorSlot.TryEnterAttribute(TEXT("Class"), !Class.IsNull()))
	{
		ClassSlot.GetValue() << Class;
	}

	// If we have a GUID, we're a spawn actor that needs to be mapped by GUID
	TOptional<FStructuredArchive::FSlot> GuidSlot = ActorSlot.TryEnterAttribute(TEXT("GUID"), SpawnID.IsValid());
	if (GuidSlot.IsSet())
	{
		GuidSlot.GetValue() << SpawnID;
	}

	uint64 DataSize;

	if (!bIsTextFormat)
	{
		// Pre-write how much data (in bytes) was serialized for this actor
		Archive << DataSize;
	}

	const uint64 BeginDataPosition = Archive.Tell();

	BodyFunction(ActorName, Class, SpawnID, ActorSlot);

	if (!bIsTextFormat)
	{
		if (bIsLoading)
		{
			// Skip our data and onto the next actor
			Archive.Seek(BeginDataPosition + DataSize);
		}
		else
		{
			const uint64 EndDataPosition = Archive.Tell();
			DataSize = EndDataPosition - BeginDataPosition;

			// Store the amount of data we've serialized (in bytes), back before the actual data
			Archive.Seek(BeginDataPosition - sizeof(DataSize));
			Archive << DataSize;

			// Go back to our current position
			Archive.Seek(EndDataPosition);
		}
	}
}

// Instantiate the permutations of TSaveGameSerializer

#if WITH_TEXT_ARCHIVE_SUPPORT
template TSaveGameSerializer<false, true>;
#endif

template TSaveGameSerializer<false, false>;
template TSaveGameSerializer<true, false>;