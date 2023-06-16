// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SaveGameSettings.generated.h"

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct FSaveGameVersionInfo
{
	GENERATED_BODY()

public:
	FSaveGameVersionInfo()
		: ID(FGuid::NewGuid())
		, Enum(nullptr)
	{}

	UPROPERTY(VisibleAnywhere, AdvancedDisplay)
	FGuid ID;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UEnum> Enum;
};

/**
 * 
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Save Game"))
class SAVEGAMEPLUGIN_API USaveGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	FGuid GetVersionId(const UEnum* VersionEnum) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UPROPERTY(EditAnywhere, Config, Category=Version)
	TArray<FSaveGameVersionInfo> Versions;

private:
	mutable TMap<TObjectPtr<UEnum>, FGuid> CachedVersions;
};
