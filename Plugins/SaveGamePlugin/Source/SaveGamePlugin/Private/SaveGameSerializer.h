// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#if WITH_TEXT_ARCHIVE_SUPPORT
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#endif

#include "SaveGameProxyArchive.h"

class USaveGameSubsystem;

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
	TSaveGameSerializer(USaveGameSubsystem* InSaveGameSubsystem);

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
