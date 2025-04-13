// Copyright (C) 2025 Jérôme "SirLynix" Leclercq (lynix680@gmail.com)
// This file is part of the "This Space Of Mine" project
// For conditions of distribution and use, see copyright notice in LICENSE

#include <ServerLib/ServerConfig.hpp>
#include <ServerLib/Database/ServerDatabase.hpp>
#include <CommonLib/Utility/JsonSerialization.hpp>
#include <fmt/color.h>

namespace tsom
{
	ServerConfig ServerConfig::Load(const ServerDatabase& database)
	{
		ServerConfig serverConfig;

		database.GetAllConfigs([&](Database::Config&& config)
		{
			if (config.name == "default_spawnpoint")
			{
				serverConfig.defaultSpawnpoint.planetId = config.value["planet_id"];
				serverConfig.defaultSpawnpoint.position = config.value["position"];
				serverConfig.defaultSpawnpoint.rotation = config.value.value("rotation", Nz::Quaternionf::Identity());
			}
			else
				fmt::print(fg(fmt::color::yellow), "unknown database config \"{}\"\n", config.name);

			return true;
		});

		return serverConfig;
	}
}
