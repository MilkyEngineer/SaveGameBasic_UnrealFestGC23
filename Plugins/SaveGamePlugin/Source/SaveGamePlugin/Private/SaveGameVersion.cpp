#include "SaveGameVersion.h"

const FGuid FSaveGameVersion::GUID(0xD7535CE7, 0x72F742B5, 0x9D1183D4, 0x33B49065);

FDevVersionRegistration GRegisterSaveGameVersion(FSaveGameVersion::GUID, FSaveGameVersion::LatestVersion, TEXT("SaveGame"));