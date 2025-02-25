// Copyright (C) 2025 Jérôme "SirLynix" Leclercq (lynix680@gmail.com)
// This file is part of the "This Space Of Mine" project
// For conditions of distribution and use, see copyright notice in LICENSE

#include <Game/States/PlanetEditorState.hpp>
#include <ClientLib/ClientChunkEntities.hpp>
#include <ClientLib/EscapeMenu.hpp>
#include <ClientLib/RenderConstants.hpp>
#include <ClientLib/Components/VisualEntityComponent.hpp>
#include <CommonLib/Planet.hpp>
#include <Game/GameConfigAppComponent.hpp>
#include <Game/States/StateData.hpp>
#include <Nazara/Core/ApplicationBase.hpp>
#include <Nazara/Core/FilesystemAppComponent.hpp>
#include <Nazara/Core/PluginManagerAppComponent.hpp>
#include <Nazara/Core/Primitive.hpp>
#include <Nazara/Core/TaskSchedulerAppComponent.hpp>
#include <Nazara/Core/Components/NodeComponent.hpp>
#include <Nazara/Graphics/DirectionalLight.hpp>
#include <Nazara/Graphics/GraphicalMesh.hpp>
#include <Nazara/Graphics/Graphics.hpp>
#include <Nazara/Graphics/ImGuiPipelinePass.hpp>
#include <Nazara/Graphics/Material.hpp>
#include <Nazara/Graphics/MaterialInstance.hpp>
#include <Nazara/Graphics/Model.hpp>
#include <Nazara/Graphics/PipelinePassList.hpp>
#include <Nazara/Graphics/TextureAsset.hpp>
#include <Nazara/Graphics/Components/CameraComponent.hpp>
#include <Nazara/Graphics/Components/LightComponent.hpp>
#include <Nazara/Graphics/PropertyHandler/TexturePropertyHandler.hpp>
#include <Nazara/Graphics/PropertyHandler/UniformValuePropertyHandler.hpp>
#include <Nazara/Platform/Window.hpp>
#include <Nazara/Renderer/Plugins/ImGuiPlugin.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace tsom
{
	PlanetEditorState::PlanetEditorState(std::shared_ptr<StateData> stateDataPtr) :
	WidgetState(std::move(stateDataPtr)),
	m_cameraRotation(-45.f, 180.f, 0.f)
	{
		auto& stateData = GetStateData();

		auto& pluginManager = stateData.app->GetComponent<Nz::PluginManagerAppComponent>();
		m_imgui = &pluginManager.Load<Nz::ImGuiPlugin>();

		IMGUI_CHECKVERSION();
		m_imguiContext = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		m_imgui->SetupContext(m_imguiContext, *stateData.window);
		m_imgui->SetupRenderer(m_imguiContext, *stateData.swapchain);

		Nz::Graphics* graphics = Nz::Graphics::Instance();
		Nz::ImGuiPipelinePass::RegisterPass(graphics->GetFramePipelinePassRegistry(), *m_imgui, m_imguiContext);

		auto& filesystem = stateData.app->GetComponent<Nz::FilesystemAppComponent>();

		m_cameraEntity = CreateEntity();
		{
			auto& cameraNode = m_cameraEntity.emplace<Nz::NodeComponent>(Nz::Vector3f(0.f, 150.f, -75.f), m_cameraRotation);

			auto passList = filesystem.Load<Nz::PipelinePassList>("assets/3d_imgui.passlist");

			auto& cameraComponent = m_cameraEntity.emplace<Nz::CameraComponent>(stateData.renderTarget, std::move(passList));
			cameraComponent.EnableInfiniteZFar(true);
			cameraComponent.EnableReversedZ(true);
			cameraComponent.UpdateClearDepth(0.f);
			cameraComponent.UpdateRenderMask(tsom::Constants::RenderMask3D & ~tsom::Constants::RenderMaskLocalPlayer);
			cameraComponent.UpdateZNear(0.1f);
			cameraComponent.UpdateZFar(10000.f); //< when infinite zfar is enabled, zfar is used as a limit for directional lights
		}

		m_sunLightEntity = CreateEntity();
		{
			m_sunLightEntity.emplace<Nz::NodeComponent>(Nz::Vector3f::Zero(), Nz::EulerAnglesf(-30.f, 80.f, 0.f));

			auto& lightComponent = m_sunLightEntity.emplace<Nz::LightComponent>();
			auto& dirLight = lightComponent.AddLight<Nz::DirectionalLight>(tsom::Constants::RenderMask3D);
			dirLight.UpdateAmbientFactor(0.05f);
			dirLight.EnableShadowCasting(true);
			dirLight.UpdateShadowMapSize(4096);
			dirLight.UpdateEnergy(5.f);
		}

		m_skyboxEntity = CreateEntity();
		{
			// Create a new material (custom properties + shaders) for the skybox
			Nz::MaterialSettings skyboxSettings;
			skyboxSettings.AddValueProperty<Nz::Color>("BaseColor", Nz::Color::White());
			skyboxSettings.AddTextureProperty("BaseColorMap", Nz::ImageType::Cubemap);
			skyboxSettings.AddPropertyHandler(std::make_unique<Nz::TexturePropertyHandler>("BaseColorMap", "HasBaseColorTexture"));
			skyboxSettings.AddPropertyHandler(std::make_unique<Nz::UniformValuePropertyHandler>("BaseColor"));

			// Setup only a forward pass (using the SkyboxMaterial module)
			Nz::MaterialPass forwardPass;
			forwardPass.states.depthBuffer = true;
			forwardPass.states.depthCompare = Nz::RendererComparison::GreaterOrEqual;
			forwardPass.shaders.push_back(std::make_shared<Nz::UberShader>(nzsl::ShaderStageType::Fragment | nzsl::ShaderStageType::Vertex, "SkyboxMaterial"));
			skyboxSettings.AddPass("ForwardPass", forwardPass);

			// Finalize the material (using SkyboxMaterial module as a reference for shader reflection)
			std::shared_ptr<Nz::Material> skyboxMaterial = std::make_shared<Nz::Material>(std::move(skyboxSettings), "SkyboxMaterial");

			// Instantiate the material to use it, and configure it (texture + cull front faces as the render is from the inside)
			std::shared_ptr<Nz::MaterialInstance> skyboxMat = skyboxMaterial->Instantiate();
			skyboxMat->SetTextureProperty("BaseColorMap", filesystem.Open<Nz::TextureAsset>("assets/skybox-space.png", { .sRGB = true }, Nz::CubemapParams{}));
			skyboxMat->UpdatePassesStates([](Nz::RenderStates& states)
			{
				states.faceCulling = Nz::FaceCulling::Front;
				return true;
			});

			// Create a cube mesh with only position
			Nz::MeshParams meshPrimitiveParams;
			meshPrimitiveParams.vertexDeclaration = Nz::VertexDeclaration::Get(Nz::VertexLayout::XYZ);

			std::shared_ptr<Nz::GraphicalMesh> skyboxMeshGfx = Nz::GraphicalMesh::Build(Nz::Primitive::Box(Nz::Vector3f::Unit() * 10.f, Nz::Vector2ui(0u), Nz::Matrix4f::Identity(), Nz::Rectf(0.f, 0.f, 1.f, 1.f)), meshPrimitiveParams);

			// Setup the model (mesh + material instance)
			std::shared_ptr<Nz::Model> skyboxModel = std::make_shared<Nz::Model>(std::move(skyboxMeshGfx));
			skyboxModel->SetMaterial(0, skyboxMat);

			// Attach the model to the entity
			m_skyboxEntity.emplace<Nz::GraphicsComponent>(std::move(skyboxModel), tsom::Constants::RenderMask3D);

			// Setup entity position and attach it to the camera (position only, camera rotation does not impact skybox)
			auto& skyboxNode = m_skyboxEntity.emplace<Nz::NodeComponent>();
			skyboxNode.SetInheritRotation(false);
			skyboxNode.SetParent(m_cameraEntity);
		}

		m_onUnhandledKeyPressed.Connect(stateData.canvas->OnUnhandledKeyPressed, [this](const Nz::WindowEventHandler*, const Nz::WindowEvent::KeyEvent& event)
		{
			auto& stateData = GetStateData();

			switch (event.scancode)
			{
				case Nz::Keyboard::Scancode::Escape:
				{
					if (m_shouldFreeMouse)
						m_shouldFreeMouse = false;
					else if (m_escapeMenu->IsVisible())
						m_escapeMenu->Hide();
					else
						m_escapeMenu->Show();

					UpdateMouseLock();
					break;
				}

				case Nz::Keyboard::Scancode::F1:
				{
					m_shouldFreeMouse = !m_shouldFreeMouse;
					UpdateMouseLock();
					break;
				}

				case Nz::Keyboard::Scancode::F5:
				{
					RefreshPlanet();
					break;
				}

				default:
					break;
			}
		});

		m_escapeMenu = CreateWidget<EscapeMenu>();
		m_escapeMenu->OnWidgetVisibilityUpdated.Connect([&](const Nz::BaseWidget* /*widget*/, bool /*isVisible*/)
		{
			UpdateMouseLock();
		});

		m_escapeMenu->OnDisconnect.Connect([this](EscapeMenu* /*menu*/)
		{
			GetStateData().app->Quit();
		});

		m_escapeMenu->OnQuitApp.Connect([this](EscapeMenu* /*menu*/)
		{
			GetStateData().app->Quit();
		});

		m_planetParentEntity = stateData.world->CreateEntity();
		m_planetParentEntity.emplace<Nz::NodeComponent>();
		m_planetParentEntity.emplace<VisualEntityComponent>().visualEntity = m_planetParentEntity;

		m_planet = std::make_unique<Planet>(*stateData.app, 1.f, 16.f, 9.81f);
		for (std::size_t layerIndex = 0; layerIndex < m_planetEntities.size(); ++layerIndex)
		{
			if (!stateData.blockLibrary->IsValidLayer(layerIndex))
				continue;

			m_planetEntities[layerIndex] = std::make_unique<ClientChunkEntities>(*stateData.app, *stateData.world, *m_planet, *stateData.blockLibrary, layerIndex);
			m_planetEntities[layerIndex]->SetParentEntity(m_planetParentEntity);
			m_planetEntities[layerIndex]->EnableCollisionGeneration(false);
		}
	}

	PlanetEditorState::~PlanetEditorState()
	{
		auto& stateData = GetStateData();
		auto& taskScheduler = stateData.app->GetComponent<Nz::TaskSchedulerAppComponent>();

		m_imgui->ShutdownRenderer(m_imguiContext);
		m_imgui->ShutdownContext(m_imguiContext);
		ImGui::DestroyContext(m_imguiContext);

		// In case previous chunks were still generating
		taskScheduler.WaitForTasks();
	}

	void PlanetEditorState::Enter(Nz::StateMachine& fsm)
	{
		WidgetState::Enter(fsm);

		m_shouldFreeMouse = false;

		m_escapeMenu->Hide();

		auto& stateData = GetStateData();
		LayoutWidgets(Nz::Vector2f(stateData.renderTarget->GetSize()));

		auto& config = stateData.app->GetComponent<GameConfigAppComponent>().GetConfig();

		float mouseSensitivity = config.GetFloatValue<float>("Input.MouseSensitivity");
		m_mouseMovedSlot.Connect(stateData.canvas->OnUnhandledMouseMoved, [&, mouseSensitivity](const Nz::WindowEventHandler*, const Nz::WindowEvent::MouseMoveEvent& event)
		{
			if (!m_isMouseLocked)
				return;

			auto& stateData = GetStateData();

			float pitchMod = -event.deltaY * mouseSensitivity;
			float yawMod = -event.deltaX * mouseSensitivity;

			m_cameraRotation.pitch += pitchMod;
			m_cameraRotation.yaw += yawMod;
			m_cameraEntity.get<Nz::NodeComponent>().SetRotation(m_cameraRotation);
		});

		UpdateMouseLock();
		RefreshPlanet();
	}

	void PlanetEditorState::Leave(Nz::StateMachine& fsm)
	{
		WidgetState::Leave(fsm);

		if (Nz::Window* window = GetStateData().window)
			window->SetRelativeMouseMode(false);
	}

	bool PlanetEditorState::Update(Nz::StateMachine& fsm, Nz::Time elapsedTime)
	{
		if (!WidgetState::Update(fsm, elapsedTime))
			return false;

		for (auto& chunkEntitiesPtr : m_planetEntities)
		{
			if (chunkEntitiesPtr)
				chunkEntitiesPtr->Update();
		}

		auto& stateData = GetStateData();

		float cameraSpeed = 5.f * elapsedTime.AsSeconds();
		if (Nz::Keyboard::IsKeyPressed(Nz::Keyboard::VKey::LShift))
			cameraSpeed *= 10.f;

		auto& cameraNode = m_cameraEntity.get<Nz::NodeComponent>();
		Nz::Vector3f cameraPos = cameraNode.GetPosition();
		Nz::Quaternionf cameraRot = cameraNode.GetRotation();

		if (Nz::Keyboard::IsKeyPressed(Nz::Keyboard::Scancode::W))
			cameraPos += cameraRot * Nz::Vector3f::Forward() * cameraSpeed;

		if (Nz::Keyboard::IsKeyPressed(Nz::Keyboard::Scancode::S))
			cameraPos += cameraRot * Nz::Vector3f::Backward() * cameraSpeed;

		if (Nz::Keyboard::IsKeyPressed(Nz::Keyboard::Scancode::A))
			cameraPos += cameraRot * Nz::Vector3f::Left() * cameraSpeed;

		if (Nz::Keyboard::IsKeyPressed(Nz::Keyboard::Scancode::D))
			cameraPos += cameraRot * Nz::Vector3f::Right() * cameraSpeed;

		if (Nz::Keyboard::IsKeyPressed(Nz::Keyboard::Scancode::Space))
			cameraPos += Nz::Vector3f::Up() * cameraSpeed;

		if (Nz::Keyboard::IsKeyPressed(Nz::Keyboard::Scancode::LControl))
			cameraPos += Nz::Vector3f::Down() * cameraSpeed;

		cameraNode.SetPosition(cameraPos);

		m_imgui->NewFrame(m_imguiContext, elapsedTime);

		ImGui::NewFrame();
		if (ImGui::Begin("Planet settings"))
		{
			ImGui::Text("Press F1 to lock/unlock mouse");
			ImGui::Separator();

			ImGui::SliderFloat("Corner radius", &m_planetSettings.cornerRadius, 0.f, 250.f);

			int chunkCount[3] = {
				int(m_planetSettings.chunkCount.x),
				int(m_planetSettings.chunkCount.y),
				int(m_planetSettings.chunkCount.z)
			};
			if (ImGui::InputInt3("Chunk count", chunkCount))
				m_planetSettings.chunkCount = Nz::Vector3ui(std::max(chunkCount[0], 0), std::max(chunkCount[1], 0), std::max(chunkCount[2], 0));

			int seed = m_planetSettings.seed;
			if (ImGui::InputInt("Seed", &seed))
				m_planetSettings.seed = std::max(seed, 0);

			ImGui::InputText("Generator name", &m_planetSettings.scriptName);

			ImGui::Separator();

			if (ImGui::Button("Update planet"))
				RefreshPlanet();
		}
		ImGui::End();
		ImGui::Render();

		return true;
	}

	void PlanetEditorState::RefreshPlanet()
	{
		auto& stateData = GetStateData();
		auto& taskScheduler = stateData.app->GetComponent<Nz::TaskSchedulerAppComponent>();

		// In case previous chunks were still generating
		taskScheduler.WaitForTasks();

		m_planet->UpdateCornerRadius(m_planetSettings.cornerRadius);

		m_planet->ClearChunks();
		m_planet->AddChunks(*stateData.blockLibrary, m_planetSettings.chunkCount);
		m_planet->GenerateChunks(*stateData.blockLibrary, taskScheduler, m_planetSettings.seed, m_planetSettings.chunkCount, m_planetSettings.scriptName);
	}

	void PlanetEditorState::LayoutWidgets(const Nz::Vector2f& /*newSize*/)
	{
		m_escapeMenu->Center();
	}

	void PlanetEditorState::UpdateMouseLock()
	{
		m_isMouseLocked = !m_shouldFreeMouse && !m_escapeMenu->IsVisible();

		if (Nz::Window* window = GetStateData().window)
			window->SetRelativeMouseMode(m_isMouseLocked);
	}
}
