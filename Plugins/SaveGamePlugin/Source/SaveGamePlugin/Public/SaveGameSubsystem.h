// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SaveGameSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class SAVEGAMEPLUGIN_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="SaveGamePlugin|Save")
	bool Save();

	UFUNCTION(BlueprintCallable, Category="SaveGamePlugin|Load")
	bool Load();
	
	UFUNCTION(BlueprintCallable, Category="SaveGamePlugin|Load")
	bool IsLoadingSaveGame() const;

protected:
	void OnWorldInitialized(UWorld* World, const UWorld::InitializationValues);
	void OnActorsInitialized(const FActorsInitializedParams& Params);
	void OnWorldCleanup(UWorld* World, bool, bool);
	
	void OnActorPreSpawn(AActor* Actor);
	void OnActorDestroyed(AActor* Actor);

	void OnLoadCompleted();

private:
	template<bool, bool> friend class TSaveGameSerializer;

	TSharedPtr<class FSaveGameSerializer, ESPMode::ThreadSafe> CurrentSerializer;
	
	TSet<TWeakObjectPtr<AActor>> SaveGameActors;
	TSet<FSoftObjectPath> DestroyedLevelActors;
};
