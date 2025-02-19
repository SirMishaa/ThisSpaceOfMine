// Copyright (C) 2025 Jérôme "SirLynix" Leclercq (lynix680@gmail.com)
// This file is part of the "This Space Of Mine" project
// For conditions of distribution and use, see copyright notice in LICENSE

#include <ClientLib/Systems/NetworkMovementInterpolationSystem.hpp>
#include <ClientLib/Components/NetworkInterpolationComponent.hpp>
#include <Nazara/Core/Components/DisabledComponent.hpp>
#include <Nazara/Core/Components/NodeComponent.hpp>

namespace tsom
{
	NetworkMovementInterpolationSystem::NetworkMovementInterpolationSystem(entt::registry& registry, Nz::Time movementTickDuration, std::size_t targetMovementPointsy) :
	m_interpolatedObserver(registry, entt::collector.group<Nz::NodeComponent, NetworkInterpolationComponent>(entt::exclude<Nz::DisabledComponent>)),
	m_targetMovementPoints(targetMovementPointsy),
	m_movementTickDuration(movementTickDuration),
	m_registry(registry)
	{
	}

	NetworkMovementInterpolationSystem::~NetworkMovementInterpolationSystem()
	{
		m_interpolatedObserver.disconnect();
	}

	void NetworkMovementInterpolationSystem::Update(Nz::Time elapsedTime)
	{
		// Setup new entities
		m_interpolatedObserver.each([&](entt::entity entity)
		{
			auto& entityNode = m_registry.get<Nz::NodeComponent>(entity);
			auto& entityInterpolation = m_registry.get<NetworkInterpolationComponent>(entity);

			entityInterpolation.Fill(m_targetMovementPoints, entityNode.GetPosition(), entityNode.GetRotation());
		});

		float deltaIncrement = elapsedTime.AsSeconds() / m_movementTickDuration.AsSeconds();

		// Interpolation
		auto view = m_registry.view<Nz::NodeComponent, NetworkInterpolationComponent>(entt::exclude<Nz::DisabledComponent>);
		for (auto&& [entity, entityNode, entityInterpolation] : view.each())
		{
			Nz::Quaternionf rotation;
			Nz::Vector3f position;

			if NAZARA_LIKELY(entityInterpolation.Advance(deltaIncrement, m_targetMovementPoints, &position, &rotation))
			{
				entityNode.SetTransform(position, rotation);
			}
		}
	}
}
