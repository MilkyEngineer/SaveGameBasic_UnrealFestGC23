// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SaveGameObject.generated.h"

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
 * 
 */
class SAVEGAMEPLUGIN_API ISaveGameObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category=SaveGame)
	bool OnSerialize(UPARAM(ref) FSaveGameArchive& Archive, bool bIsLoading);
};

UINTERFACE(MinimalAPI)
class USaveGameSpawnActor : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SAVEGAMEPLUGIN_API ISaveGameSpawnActor
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="SaveGame|Spawn")
	const FGuid GetSpawnID() const;

	UFUNCTION(BlueprintNativeEvent, Category="SaveGame|Spawn")
	bool SetSpawnID(const FGuid& NewID);
};