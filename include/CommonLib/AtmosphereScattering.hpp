// Copyright (C) 2025 Jérôme "SirLynix" Leclercq (lynix680@gmail.com)
// This file is part of the "This Space Of Mine" project
// For conditions of distribution and use, see copyright notice in LICENSE

#pragma once

#ifndef TSOM_COMMONLIB_ATMOSPHERESCATTERING_HPP
#define TSOM_COMMONLIB_ATMOSPHERESCATTERING_HPP

#include <Nazara/Math/Vector3.hpp>
#include <NazaraUtils/Prerequisites.hpp>

namespace tsom
{
	struct AtmosphereScattering
	{
		Nz::Vector3f sunDir = Nz::Vector3f(0.852868497f, 0.5f, 0.150383770f);
		Nz::Vector3f sunIntensity = Nz::Vector3f(40.f);
		float atmosphereRadius = 150.f;
		float planetRadius = 100.f;

		Nz::Vector3f rayleighBeta = Nz::Vector3f(0.0000331f, 0.000135f, 0.00058f);
		Nz::Vector3f mieBeta = Nz::Vector3f(0.000021f);
		Nz::Vector3f ambientBeta = Nz::Vector3f(0.f);
		Nz::Vector3f absorptionBeta = Nz::Vector3f(0.000204f, 0.0000497f, 0.00000195f);
		float mieScattering = 0.9f;

		float rayleighHeight = 50.f;
		float mieHeight = 60.f;
		float heightAbsorption = 100.f;
		float absorptionFalloff = 10.f;

		Nz::Int32 primarySteps = 32;
		Nz::Int32 lightSteps = 8;
	};
}

#endif // TSOM_COMMONLIB_ATMOSPHERESCATTERING_HPP
