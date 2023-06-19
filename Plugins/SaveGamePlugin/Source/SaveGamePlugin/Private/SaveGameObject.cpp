// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#include "SaveGameObject.h"

FSaveGameArchive::FSaveGameArchive(FStructuredArchive::FRecord& InRecord, UObject* InObject)
	: Record(&InRecord)
	, Object(InObject)
	, StartPosition(0)
	, EndPosition(0)
{
	FArchive& Archive = Record->GetUnderlyingArchive();

	// This is useful if proxy archives override this for scoping purposes
	// i.e. serializing nested sub-objects
	Archive.MarkScriptSerializationStart(Object.Get());
	
	if (Archive.IsTextFormat())
	{
		return;
	}

	StartPosition = Archive.Tell();

	// If saving, pre-fill this so that we can fill it on destruct
	// If loading, use it to immediately serialize our Fields map
	uint64 FieldsOffset;
	Archive << FieldsOffset;

	if (Archive.IsLoading())
	{
		// Go to our fields
		Archive.Seek(StartPosition + FieldsOffset);

		// Serialize them in
		Archive << Fields;

		// Store our true end position, so that when we destruct, we can fall off the end gracefully
		EndPosition = Archive.Tell();

		// If we have any properties that were redirected in CoreRedirects, fix them here
		for (auto It = Fields.CreateIterator(); It; ++It)
		{
			TPair<FName, uint64>& Field = *It;
					
			for (UStruct* CheckStruct = Object->GetClass(); CheckStruct; CheckStruct = CheckStruct->GetSuperStruct())
			{
				FName NewProperty = FProperty::FindRedirectedPropertyName(CheckStruct, Field.Key);
					
				if (!NewProperty.IsNone())
				{
					Field.Key = NewProperty;
					break;
				}
			}
		}
	}
}

FSaveGameArchive::~FSaveGameArchive()
{
	// We're going out of scope, let's serialize our fields map
	if (!IsValid())
	{
		return;
	}
		
	FArchive& Archive = Record->GetUnderlyingArchive();

	ON_SCOPE_EXIT
	{
		Archive.MarkScriptSerializationEnd(Object.Get());
	};

	if (Archive.IsTextFormat())
	{
		return;
	}
	
	if (Archive.IsSaving())
	{
		uint64 FieldsOffset = Archive.Tell() - StartPosition;
		
		// Store our accrued list of fields and their offsets
		Archive << Fields;

		EndPosition = Archive.Tell();

		// Store the offset to our fields map
		Archive.Seek(StartPosition);
		Archive << FieldsOffset;
	}

	// If we had any ordering changes or removals of fields, be sure to continue on from the very end
	Archive.Seek(EndPosition);
}
