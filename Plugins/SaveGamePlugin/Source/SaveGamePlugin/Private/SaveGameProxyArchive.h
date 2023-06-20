// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "Serialization/NameAsStringProxyArchive.h"

/**
 * A proxy archive that ensures that all object reference types are stored as a SoftObjectPath.
 * Also has a utility for redirecting those references (used for redirecting spawned actors).
 */
template<bool bIsLoading>
struct TSaveGameProxyArchive final : public FNameAsStringProxyArchive
{
	TSaveGameProxyArchive(FArchive& InInnerArchive)
		: FNameAsStringProxyArchive(InInnerArchive)
	{
		// Setting this hints a Serialize method to only serialize SaveGame properties
		ArIsSaveGame = true;
	}

	/** Allows the archive to redirect any object (used for redirecting spawned actors). */
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
			// Actually perform the redirect
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
