// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#include "SaveGameSettings.h"

FGuid USaveGameSettings::GetVersionId(const UEnum* VersionEnum) const
{
	if (CachedVersions.IsEmpty())
	{
		CachedVersions.Reserve(Versions.Num());
		
		for (const FSaveGameVersionInfo& VersionInfo : Versions)
		{
			if (VersionInfo.ID.IsValid() && VersionInfo.Enum)
			{
				CachedVersions.FindOrAdd(VersionInfo.Enum) = VersionInfo.ID;
			}
		}
	}

	if (CachedVersions.Contains(VersionEnum))
	{
		return CachedVersions[VersionEnum];
	}
	
	return FGuid();
}

#if WITH_EDITOR
void USaveGameSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(USaveGameSettings, Versions))
	{
		CachedVersions.Reset();
	}
}
#endif

