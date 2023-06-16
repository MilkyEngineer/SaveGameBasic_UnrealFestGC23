// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SaveGameObject.generated.h"

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct FSaveGameArchive
{
	GENERATED_BODY()

public:
	FSaveGameArchive()
		: Record(nullptr)
	{}

	FSaveGameArchive(class FStructuredArchive::FRecord& InRecord)
		: Record(&InRecord)
	{}

	bool IsValid() const
	{
		return Record != nullptr;
	}
	
	class FStructuredArchive::FRecord& GetRecord() const
	{
		return *Record;
	}

private:
	FSaveGameArchive(FSaveGameArchive&) = delete;

	class FStructuredArchive::FRecord* Record;
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