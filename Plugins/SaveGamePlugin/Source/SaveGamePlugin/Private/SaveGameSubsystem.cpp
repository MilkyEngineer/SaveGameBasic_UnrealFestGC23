// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#include "SaveGameSubsystem.h"

#include "SaveGameFunctionLibrary.h"
#include "SaveGameObject.h"
#include "SaveGameSystem.h"

#include "EngineUtils.h"
#include "PlatformFeatures.h"
#include "SaveGameVersion.h"
#include "Serialization/NameAsStringProxyArchive.h"

#if WITH_TEXT_ARCHIVE_SUPPORT
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#endif

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

template<bool bIsLoading>
struct TSaveGameProxyArchive final : public FNameAsStringProxyArchive
{
	TSaveGameProxyArchive(FArchive& InInnerArchive)
		: FNameAsStringProxyArchive(InInnerArchive)
	{
		ArIsSaveGame = true;
	}

	void AddRedirect(const FSoftObjectPath& From, const FSoftObjectPath& To)
	{
		if (From != To)
		{
			Redirects.Add(From, To);
		}
	}

	virtual FArchive& operator<<(FSoftObjectPath& Value) override
	{
		Value.SerializePath(*this);
		
		// If we have a defined core redirect, make sure that it's applied
		if (bIsLoading && !Value.IsNull())
		{
			Value.FixupCoreRedirects();
		}

		if (bIsLoading && Redirects.Contains(Value))
		{
			Value = Redirects[Value];
		}
		
		return *this;
	}

	virtual FArchive& operator<<(FSoftObjectPtr& Value) override
	{
		FSoftObjectPath Path;

		if (!bIsLoading)
		{
			Path = Value.ToSoftObjectPath();
		}

		*this << Path;

		if (bIsLoading)
		{
			Value = FSoftObjectPtr(Path);
		}

		return *this;
	}

	virtual FArchive& operator<<(UObject*& Value) override
	{
		return SerializeObject(Value);
	}
	
	virtual FArchive& operator<<(FWeakObjectPtr& Value) override
	{
		return SerializeObject(Value);
	}
	
	virtual FArchive& operator<<(FObjectPtr& Value) override
	{
		return SerializeObject(Value);
	}

private:
	TMap<FSoftObjectPath, FSoftObjectPath> Redirects;

	template<typename ObjectType>
	static FSoftObjectPath ToSoftObjectPath(const ObjectType& Value)
	{
		return FSoftObjectPath(Value);
	}
	static FSoftObjectPath ToSoftObjectPath(const FWeakObjectPtr& Value)
	{
		return FSoftObjectPath(Value.Get());
	}

	template<typename ObjectType>
	FArchive& SerializeObject(ObjectType& Value)
	{
		FSoftObjectPath Path;

		if (!bIsLoading)
		{
			Path = ToSoftObjectPath(Value);
		}

		*this << Path;

		if (bIsLoading)
		{
			UObject* Object = Path.ResolveObject();
			Value = Object;

			if (!IsValid(Object) && !Path.IsNull())
			{
				Value = Path.TryLoad();
			}
		}

		return *this;
	}
};

class FSaveGameSerializer :  public TSharedFromThis<FSaveGameSerializer>
{
public:
	virtual ~FSaveGameSerializer() = default;
};

template<bool bIsLoading, bool bIsTextFormat = false>
class TSaveGameSerializer final : public FSaveGameSerializer
{
	using FSaveGameMemoryArchive = typename TChooseClass<bIsLoading, FMemoryReader, FMemoryWriter>::Result;

	static_assert(!bIsLoading || !bIsTextFormat, "This serializer hasn't been implemented for text based loading, only saving!");
	static_assert(WITH_TEXT_ARCHIVE_SUPPORT || !bIsTextFormat, "Engine isn't compiled with text archive support, cannot use text based TSaveGameSerializer");
	
	using FSaveGameFormatter = typename TChooseClass<bIsTextFormat && WITH_TEXT_ARCHIVE_SUPPORT,
		typename TChooseClass<bIsLoading, FBinaryArchiveFormatter, FJsonArchiveOutputFormatter>::Result,
		FBinaryArchiveFormatter>::Result;
	
public:
	TSaveGameSerializer(USaveGameSubsystem* InSaveGameSubsystem)
		: SaveGameSubsystem(InSaveGameSubsystem)
		, Archive(Data)
		, ProxyArchive(Archive)
		, Formatter(ProxyArchive)
		, StructuredArchive(Formatter)
		, RootSlot(StructuredArchive.Open())
		, RootRecord(RootSlot.EnterRecord())
	{
		static_cast<FArchive&>(ProxyArchive).SetIsTextFormat(bIsTextFormat);
	}
	
	bool Save();
	bool Load();

private:
	static FString GetSaveName();
	
	void OnMapLoad(UWorld* World);
	void SerializeHeader();
	void SerializeActors();
	void SerializeDestroyedActors();
	void SerializeVersions();

	void SerializeActor(FStructuredArchive::FMap& ActorMap, AActor*& Actor, TFunction<void(const FString&, const FSoftClassPath&, const FGuid&, FStructuredArchive::FSlot&)>&& BodyFunction);
	
	const TWeakObjectPtr<USaveGameSubsystem> SaveGameSubsystem;
	TArray<uint8> Data;
	FSaveGameMemoryArchive Archive;
	TSaveGameProxyArchive<bIsLoading> ProxyArchive;
	FSaveGameFormatter Formatter;
	FStructuredArchive StructuredArchive;
	
	FStructuredArchive::FSlot RootSlot;
	FStructuredArchive::FRecord RootRecord;

	FString MapName;
	uint64 VersionOffset;
};

template <bool bIsLoading, bool bIsTextFormat>
bool TSaveGameSerializer<bIsLoading, bIsTextFormat>::Save()
{
	check(!bIsLoading);
	
	if (ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem())
	{
		SerializeHeader();
		SerializeActors();
		SerializeDestroyedActors();
		SerializeVersions();

		if (!bIsTextFormat)
		{
			// We've likely updated our header, let's rewrite it
			Archive.Seek(0);
			SerializeHeader();
		}

		// Be sure to close this, as you'll be missing closed braces for JSON archives
		StructuredArchive.Close();
		
		if (!bIsTextFormat && !bIsLoading)
		{
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

	SerializeActors();
	SerializeDestroyedActors();

	SaveGameSubsystem->OnLoadCompleted();
}

template <bool bIsLoading, bool bIsTextFormat>
void TSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeHeader()
{
	if (!bIsLoading && MapName.IsEmpty())
	{
		check(SaveGameSubsystem.IsValid());
		const UWorld* World = SaveGameSubsystem->GetWorld();

		MapName = World->GetOutermost()->GetLoadedPath().GetPackageName();
	}
	
	RootRecord << SA_VALUE(TEXT("Map"), MapName);

	if (!bIsTextFormat)
	{
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
		// Actually serialize the actor data
		if (bIsLoading && !bIsTextFormat)
		{
			Archive.Seek(ActorsPosition);
		}
		
		FStructuredArchive::FMap ActorMap = RootRecord.EnterMap(ActorsFieldName, NumActors);

		auto ActorsIt = SaveGameSubsystem->SaveGameActors.CreateConstIterator();
		
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

	Archive.UsingCustomVersion(FSaveGameVersion::GUID);
	
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
			FString ActorSubPath = (*DestroyedActorsIt).GetSubPathString();
			ActorSubPath.RemoveFromStart(LEVEL_SUBPATH_PREFIX);
			ActorName = *ActorSubPath;

			++DestroyedActorsIt;
		}
		
		DestroyedActorsArray.EnterElement() << ActorName;

		if (bIsLoading)
		{
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
		VersionContainer = Archive.GetCustomVersions();
		
		if (!bIsTextFormat)
		{
			VersionOffset = Archive.Tell();
		}
	}

	VersionContainer.Serialize(RootRecord.EnterField(TEXT("Versions")));

	if (bIsLoading)
	{
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

	if (TOptional<FStructuredArchive::FSlot> ClassSlot = ActorSlot.TryEnterAttribute(TEXT("Class"), !Class.IsNull()))
	{
		ClassSlot.GetValue() << Class;
	}

	TOptional<FStructuredArchive::FSlot> GuidSlot = ActorSlot.TryEnterAttribute(TEXT("GUID"), SpawnID.IsValid());
	if (GuidSlot.IsSet())
	{
		GuidSlot.GetValue() << SpawnID;
	}

	uint64 DataSize;

	if (!bIsTextFormat)
	{
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

			// Store the amount of data we've serialized, back before the actual data
			Archive.Seek(BeginDataPosition - sizeof(DataSize));
			Archive << DataSize;

			// Go back to our current position
			Archive.Seek(EndDataPosition);
		}
	}
}

void USaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::OnWorldInitialized);
	FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &ThisClass::OnActorsInitialized);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);

	// This example doesn't handle streaming levels, but if we did, we'd use a combination of
	// FWorldDelegates::LevelAddedToWorld and FWorldDelegates::PreLevelRemovedFromWorld
	// In these, we'd store the current state of actors within that level

	OnWorldInitialized(GetWorld(), UWorld::InitializationValues());
}

void USaveGameSubsystem::Deinitialize()
{
	FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);
	FWorldDelegates::OnWorldInitializedActors.RemoveAll(this);
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::PreLevelRemovedFromWorld.RemoveAll(this);
}

bool USaveGameSubsystem::Save()
{
	TSaveGameSerializer<false> BinarySerializer(this);
	bool bSuccess = BinarySerializer.Save();

#if WITH_TEXT_ARCHIVE_SUPPORT
	TSaveGameSerializer<false, true> TextSerializer(this);
	bSuccess &= TextSerializer.Save();
#endif
	
	return bSuccess;
}

bool USaveGameSubsystem::Load()
{
	const TSharedRef<TSaveGameSerializer<true>> BinarySerializer = MakeShared<TSaveGameSerializer<true>>(this); 
	CurrentSerializer = BinarySerializer.ToSharedPtr();
	return BinarySerializer->Load();
}

bool USaveGameSubsystem::IsLoadingSaveGame() const
{
	return CurrentSerializer.IsValid();
}

void USaveGameSubsystem::OnWorldInitialized(UWorld* World, const UWorld::InitializationValues)
{
	if (!IsValid(World) || GetWorld() != World)
	{
		return;
	}
	
	World->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::OnActorPreSpawn));
	World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ThisClass::OnActorDestroyed));
}

void USaveGameSubsystem::OnActorsInitialized(const FActorsInitializedParams& Params)
{
	if (!IsValid(Params.World) || GetWorld() != Params.World)
	{
		return;
	}
	
	for (TActorIterator<AActor> It(Params.World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && Actor->Implements<USaveGameObject>())
		{
			SaveGameActors.Add(Actor);
		}
	}
}

void USaveGameSubsystem::OnWorldCleanup(UWorld* World, bool, bool)
{
	if (!IsValid(World) || GetWorld() != World)
	{
		return;
	}
	
	SaveGameActors.Reset();
	DestroyedLevelActors.Reset();
}

void USaveGameSubsystem::OnActorPreSpawn(AActor* Actor)
{
	if (IsValid(Actor) && Actor->Implements<USaveGameObject>())
	{
		SaveGameActors.Add(Actor);
	}
}

void USaveGameSubsystem::OnActorDestroyed(AActor* Actor)
{
	SaveGameActors.Remove(Actor);

	if (USaveGameFunctionLibrary::WasObjectLoaded(Actor))
	{
		FString ActorPath = Actor->GetPathName();

#if WITH_EDITOR
		const UWorld* World = GetWorld();
		if (World->IsPlayInEditor())
		{
			ActorPath = UWorld::StripPIEPrefixFromPackageName(ActorPath, World->StreamingLevelsPrefix);
		}
#endif
		
		DestroyedLevelActors.Add(ActorPath);
	}
}

void USaveGameSubsystem::OnLoadCompleted()
{
	CurrentSerializer = nullptr;
}
