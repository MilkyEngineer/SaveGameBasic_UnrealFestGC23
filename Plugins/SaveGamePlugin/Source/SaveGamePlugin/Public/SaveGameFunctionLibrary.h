// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "SaveGameObject.h"

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SaveGameFunctionLibrary.generated.h"

UCLASS()
class SAVEGAMEPLUGIN_API USaveGameFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Check if an object was loaded from an asset (i.e. a Static Mesh, Actor from a level, etc.)
	 * 
	 * @param Object The object to check if loaded
	 * @return true if object was loaded from an asset
	 */
	UFUNCTION(BlueprintCallable, Category="SaveGamePlugin|Utilities")
	static bool WasObjectLoaded(const UObject* Object);

	/**
	 * Check to see if the current save game is loading the archive.
	 * 
	 * @param Archive The archive that the save game is serializing
	 * @return true if save game is loading, false if save game is saving
	 */
	UFUNCTION(BlueprintPure, Category="SaveGamePlugin|Utilities")
	static bool IsLoading(const FSaveGameArchive& Archive);

	/**
	 * Helper method to serialize an actor's transform if the actor is movable.
	 * If loading, will set the actor's transform.
	 * 
	 * @param Archive The archive that the save game is serializing
	 * @param Actor The actor whose transform will be serialized
	 * @return true if the transform was serialized
	 */
	UFUNCTION(BlueprintCallable, Category="SaveGamePlugin|Serialize", meta=(DefaultToSelf="Actor"))
	static bool SerializeActorTransform(UPARAM(ref) FSaveGameArchive& Archive, AActor* Actor);

	/**
	 * Serialize a property to/from the specified archive.
	 * 
	 * OnSave: Store the value of the property to the archive (if bSave is true)
	 * OnLoad: Read the archive, if the value exists, load by reference into the property connected to Value 
	 * 
	 * @param Archive The archive that the save game is serializing
	 * @param Value The property that will be serialized (by reference)
	 * @param bSave If true, will save this property, otherwise not if false. Not used when loading.
	 * @return true if the property was serialized
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category="SaveGamePlugin|Serialize", meta=(CustomStructureParam="Value", AdvancedDisplay="bSave"))
	static bool SerializeItem(UPARAM(ref) FSaveGameArchive& Archive, UPARAM(ref) int32& Value, bool bSave = true);
	DECLARE_FUNCTION(execSerializeItem);

	/**
	 * Serializes the specified version.
	 *
	 * OnSave: Stores the latest value of the version to the archive.
	 * OnLoad: Reads the version from the save game archive (if any)
	 * 
	 * @param Archive The archive that the save game is serializing
	 * @param VersionEnum The enum of the version we want to serialize
	 * @return The version that was serialized (-1 if not exist or no version)
	 */
	UFUNCTION(BlueprintCallable, Category="SaveGamePlugin|Serialize")
	static int32 UseCustomVersion(UPARAM(ref) FSaveGameArchive& Archive, const UEnum* VersionEnum);
};
