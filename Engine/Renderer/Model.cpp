//------------------------------------------------------------------------------
// Model.cpp
//
// Implementation of Model loading and management
//------------------------------------------------------------------------------

#include "Engine/Renderer/Model.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Core/Logger/Logger.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>        // For glm::quat
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>        // For glm::mat4_cast (it's in gtx, not gtc)

namespace Nightbloom
{
	bool Model::LoadFromFile(const std::string& filepath,
		ResourceManager* resourceManager,
		VulkanDescriptorManager* descriptorManager)
	{
		GLTFLoader loader;
		auto modelData = loader.Load(filepath);

		if (!modelData)
		{
			LOG_ERROR("Failed to load model from file: {}", filepath);
			return false;
		}

		return LoadFromData(*modelData, resourceManager, descriptorManager);
	}

	bool Model::LoadFromData(const ModelData& data,
		ResourceManager* resourceManager,
		VulkanDescriptorManager* descriptorManager)
	{
		if (!resourceManager)
		{
			LOG_ERROR("ResourceManager is null");
			return false;
		}

		m_Name = data.name;
		m_SourcePath = data.sourcePath;

		LOG_INFO("Loading model '{}' with {} meshes and {} materials",
			m_Name, data.meshes.size(), data.materials.size());

		// Load materials first
		m_Materials.reserve(data.materials.size());
		for (size_t i = 0; i < data.materials.size(); ++i)
		{
			const auto& matData = data.materials[i];
			auto material = std::make_unique<Material>(matData.name);

			material->SetAlbedoColor(matData.baseColorFactor);
			material->SetRoughness(matData.roughnessFactor);
			material->SetMetallic(matData.metallicFactor);
			material->SetDoubleSided(matData.doubleSided);

			// Load albedo texture if specified
			if (!matData.baseColorTexturePath.empty())
			{
				std::string texName = m_Name + "_albedo_" + std::to_string(i);
				VulkanTexture* texture = resourceManager->LoadTexture(texName, matData.baseColorTexturePath);

				if (texture)
				{
					// Create descriptor set for the texture
					if (descriptorManager && !texture->HasDescriptorSet())
					{
						texture->CreateDescriptorSet(descriptorManager);
					}
					material->SetAlbedoTexture(texture);
					LOG_INFO("  Loaded albedo texture for material '{}'", matData.name);
				}
				else
				{
					LOG_WARN("  Failed to load albedo texture: {}", matData.baseColorTexturePath);
				}
			}

			// Load normal texture if specified
			if (!matData.normalTexturePath.empty())
			{
				std::string texName = m_Name + "_normal_" + std::to_string(i);
				VulkanTexture* texture = resourceManager->LoadTexture(texName, matData.normalTexturePath);

				if (texture)
				{
					if (descriptorManager && !texture->HasDescriptorSet())
					{
						texture->CreateDescriptorSet(descriptorManager);
					}
					material->SetNormalTexture(texture);
				}
			}

			m_Materials.push_back(std::move(material));
		}

		// Load meshes
		m_Meshes.reserve(data.meshes.size());
		for (size_t i = 0; i < data.meshes.size(); ++i)
		{
			const auto& meshData = data.meshes[i];
			auto mesh = std::make_unique<Mesh>(meshData.name);

			// Create vertex buffer
			std::string vbName = m_Name + "_vb_" + std::to_string(i);
			VkDeviceSize vbSize = meshData.vertices.size() * sizeof(VertexPNT);

			auto vertexBuffer = resourceManager->CreateVertexBufferUnique(vbName, vbSize, false);
			if (!vertexBuffer)
			{
				LOG_ERROR("Failed to create vertex buffer for mesh '{}'", meshData.name);
				continue;
			}

			// Upload vertex data
			if (!vertexBuffer->UploadData(meshData.vertices.data(), vbSize, 0,
				resourceManager->GetTransferCommandPool()))
			{
				LOG_ERROR("Failed to upload vertex data for mesh '{}'", meshData.name);
				continue;
			}

			// Create index buffer
			std::string ibName = m_Name + "_ib_" + std::to_string(i);
			VkDeviceSize ibSize = meshData.indices.size() * sizeof(uint32_t);

			auto indexBuffer = resourceManager->CreateIndexBufferUnique(ibName, ibSize, false);
			if (!indexBuffer)
			{
				LOG_ERROR("Failed to create index buffer for mesh '{}'", meshData.name);
				continue;
			}

			// Upload index data
			if (!indexBuffer->UploadData(meshData.indices.data(), ibSize, 0,
				resourceManager->GetTransferCommandPool()))
			{
				LOG_ERROR("Failed to upload index data for mesh '{}'", meshData.name);
				continue;
			}

			mesh->SetVertexBuffer(std::move(vertexBuffer));
			mesh->SetIndexBuffer(std::move(indexBuffer));
			mesh->SetVertexCount(static_cast<uint32_t>(meshData.vertices.size()));
			mesh->SetIndexCount(static_cast<uint32_t>(meshData.indices.size()));
			mesh->SetBounds(meshData.boundsMin, meshData.boundsMax);

			// Assign material
			if (meshData.materialIndex >= 0 && meshData.materialIndex < static_cast<int32_t>(m_Materials.size()))
			{
				mesh->SetMaterial(m_Materials[meshData.materialIndex].get());
			}

			m_TotalVertices += meshData.vertices.size();
			m_TotalIndices += meshData.indices.size();

			LOG_INFO("  Created mesh '{}': {} vertices, {} indices",
				meshData.name, meshData.vertices.size(), meshData.indices.size());

			m_Meshes.push_back(std::move(mesh));
		}

		CalculateBounds();

		LOG_INFO("Model '{}' loaded: {} meshes, {} total vertices, {} total indices",
			m_Name, m_Meshes.size(), m_TotalVertices, m_TotalIndices);

		return !m_Meshes.empty();
	}

	void Model::SetPosition(const glm::vec3& position)
	{
		m_Position = position;
		m_Transform = glm::translate(glm::mat4(1.0f), m_Position) *
			glm::mat4_cast(glm::quat(m_Rotation)) *
			glm::scale(glm::mat4(1.0f), m_Scale);
	}

	void Model::SetRotation(const glm::vec3& eulerAngles)
	{
		m_Rotation = eulerAngles;
		m_Transform = glm::translate(glm::mat4(1.0f), m_Position) *
			glm::mat4_cast(glm::quat(m_Rotation)) *
			glm::scale(glm::mat4(1.0f), m_Scale);
	}

	void Model::SetScale(const glm::vec3& scale)
	{
		m_Scale = scale;
		m_Transform = glm::translate(glm::mat4(1.0f), m_Position) *
			glm::mat4_cast(glm::quat(m_Rotation)) *
			glm::scale(glm::mat4(1.0f), m_Scale);
	}

	void Model::SetScale(float uniformScale)
	{
		SetScale(glm::vec3(uniformScale));
	}

	void Model::CalculateBounds()
	{
		if (m_Meshes.empty())
		{
			m_BoundsMin = glm::vec3(0.0f);
			m_BoundsMax = glm::vec3(0.0f);
			return;
		}

		m_BoundsMin = glm::vec3(FLT_MAX);
		m_BoundsMax = glm::vec3(-FLT_MAX);

		for (const auto& mesh : m_Meshes)
		{
			m_BoundsMin = glm::min(m_BoundsMin, mesh->GetBoundsMin());
			m_BoundsMax = glm::max(m_BoundsMax, mesh->GetBoundsMax());
		}
	}

} // namespace Nightbloom