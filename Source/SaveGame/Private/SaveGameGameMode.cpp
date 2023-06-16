// Copyright Epic Games, Inc. All Rights Reserved.

#include "SaveGameGameMode.h"
#include "SaveGameCharacter.h"
#include "UObject/ConstructorHelpers.h"

ASaveGameGameMode::ASaveGameGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

}
