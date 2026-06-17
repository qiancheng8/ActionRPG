// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Items/RPGItem.h"
#include "RPGArmorItem.generated.h"

/** Native base class for armor, should be blueprinted */
UCLASS()
class ACTIONRPG_API URPGArmorItem : public URPGItem
{
	GENERATED_BODY()

public:
	/** Constructor */
	URPGArmorItem()
	{
		ItemType = URPGAssetManager::ArmorItemType;
	}
};
