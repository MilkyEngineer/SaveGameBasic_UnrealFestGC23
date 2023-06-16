// Copyright Alex Stevens (@MilkyEngineer). All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

class SAVEGAMEPLUGIN_API FSaveGameVersion
{
public:
	enum Type
	{
		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
	
	const static FGuid GUID;
};
