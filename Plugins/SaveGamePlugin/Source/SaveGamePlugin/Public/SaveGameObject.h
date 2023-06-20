// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SaveGameObject.generated.h"

/**
 * The blueprint representation of the structured record we're writing to.
 *
 * When serializing a binary archive, FSaveGameArchive on construction will store its initial position that it started
 * serializing from. Once FSaveGameArchive loses scope and calls its destructor, it will then serialize all of the
 * field names and their offsets, if loading, it will automatically seek to the very end of the archive. The initial
 * position and stored offsets can be used for out-of-order seeking to each of the archive's serialized fields.
 *
 * Additionally, when loading, these field names are checked against CoreRedirects and redirected if needed.
 */
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct SAVEGAMEPLUGIN_API FSaveGameArchive
{
	GENERATED_BODY()

public:
	FSaveGameArchive()
		: Record(nullptr)
		, Object(nullptr)
		, StartPosition(0)
		, EndPosition(0)
	{}

	FSaveGameArchive(class FStructuredArchive::FRecord& InRecord, UObject* InObject);
	~FSaveGameArchive();

	bool IsValid() const
	{
		return Record != nullptr;
	}
	
	class FStructuredArchive::FRecord& GetRecord() const
	{
		return *Record;
	}

	/**
	 * Serializes a field with a custom lambda function. If a binary format, stores its offset for out-of-order reading.
	 * @param FieldName Name of the field that's being serialized
	 * @param SerializeFunction Lambda function to do the actual serialization, provides a structured slot
	 * @return true if the field was serialized
	 */
	template<typename FSerializeFunc>
	bool SerializeField(FName FieldName, FSerializeFunc SerializeFunction)
	{
		if (!IsValid())
		{
			return false;
		}

		FArchive& Archive = Record->GetUnderlyingArchive();

		if (Archive.IsSaving() && Fields.Contains(FieldName))
		{
			// We don't want to double up on saving the same property
			return false;
		}

		// Text formats don't deal with seeking very well
		if (!Archive.IsTextFormat())
		{
			if (Archive.IsLoading())
			{
				if (!Fields.Contains(FieldName))
				{
					return false;
				}

				Archive.Seek(StartPosition + Fields[FieldName]);
			}
			else
			{
				// Use an offset, in case we need to shuffle data around later!
				Fields.Add(FieldName, Archive.Tell() - StartPosition);
			}
		}

		SerializeFunction(Record->EnterField(*FieldName.ToString()));

		return true;
	}
	
private:
	FSaveGameArchive(FSaveGameArchive&) = delete;
	
	class FStructuredArchive::FRecord* Record;
	TWeakObjectPtr<> Object;
	uint64 StartPosition;
	uint64 EndPosition;

	/** This serialized fields and their offsets from the start of this archive */
	TMap<FName, uint64> Fields;
};

// Ensure that our archive can't be copied
template<>
struct TStructOpsTypeTraits<FSaveGameArchive> : public TStructOpsTypeTraitsBase2<FSaveGameArchive>
{
	enum
	{
		WithCopy = false
	};
};

UINTERFACE(MinimalAPI)
class USaveGameObject : public UInterface
{
	GENERATED_BODY()
};

/**
 * If an object implements this interface, it should be saved.
 */
class SAVEGAMEPLUGIN_API ISaveGameObject
{
	GENERATED_BODY()

public:
	/**
	 * Called after an object's SaveGame properties are serialized. Useful for serializing fields that can't be marked
	 * with the SaveGame specifier (i.e. engine properties like transforms, velocity, etc). This method can also be
	 * implemented as a "PostSerialize" event for this object.
	 * 
	 * @param Archive The archive that fields will be serialized to/from
	 * @param bIsLoading true if loading a save game, false if saving
	 * @return Not used, but necessary to not turn this method into an event (useful for SerializeItem and local vars)
	 */
	UFUNCTION(BlueprintNativeEvent, Category=SaveGame)
	bool OnSerialize(UPARAM(ref) FSaveGameArchive& Archive, bool bIsLoading);
};

UINTERFACE(MinimalAPI)
class USaveGameSpawnActor : public UInterface
{
	GENERATED_BODY()
};

/**
 * Used on an actor to provide the save game system a unique SpawnID for serializing actor data to spawned actors that
 * aren't (or can't be) spawned by the save game system.
 *
 * For example, a player's character is spawned by the game mode before the save game system has a chance to spawn it.
 * So the save game system then gets the already spawned character's SpawnID, matches it with the data's SpawnID,
 * and then serializes that data to the character.
 */
class SAVEGAMEPLUGIN_API ISaveGameSpawnActor
{
	GENERATED_BODY()

public:
	/** Returns a unique Spawn ID for this Actor */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="SaveGame|Spawn")
	const FGuid GetSpawnID() const;

	/** Assigns a new SpawnID to this actor */
	UFUNCTION(BlueprintNativeEvent, Category="SaveGame|Spawn")
	bool SetSpawnID(const FGuid& NewID);
};