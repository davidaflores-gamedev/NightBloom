//------------------------------------------------------------------------------
// Model.hpp
//
// Collection of meshes loaded from a file
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Mesh.hpp"
#include "Engine/Renderer/Material.hpp"
#include "Engine/Renderer/GLTFLoader.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>
#include <string>

namespace Nightbloom
{
	class ResourceManager;
	class VulkanDescriptorManager;

	class Model
	{
	public:
		Model() = default;
		Model(const std::string& name) : m_Name(name) {}
		~Model() = default;

		// Model cannot be copied
		Model(const Model&) = delete;
		Model& operator=(const Model&) = delete;

		// Model can be moved
		Model(Model&&) = default;
		Model& operator=(Model&&) = default;

		// Load from file (uses GLTFLoader internally)
		bool LoadFromFile(const std::string& filepath,
			ResourceManager* resourceManager,
			VulkanDescriptorManager* descriptorManager);

		// Load from already-parsed ModelData
		bool LoadFromData(const ModelData& data,
			ResourceManager* resourceManager,
			VulkanDescriptorManager* descriptorManager);

		// Getters
		const std::string& GetName() const { return m_Name; }
		const std::string& GetSourcePath() const { return m_SourcePath; }

		const std::vector<std::unique_ptr<Mesh>>& GetMeshes() const { return m_Meshes; }
		size_t GetMeshCount() const { return m_Meshes.size(); }
		Mesh* GetMesh(size_t index) const { return index < m_Meshes.size() ? m_Meshes[index].get() : nullptr; }

		const std::vector<std::unique_ptr<Material>>& GetMaterials() const { return m_Materials; }
		size_t GetMaterialCount() const { return m_Materials.size(); }
		Material* GetMaterial(size_t index) const { return index < m_Materials.size() ? m_Materials[index].get() : nullptr; }

		// Transform
		const glm::mat4& GetTransform() const { return m_Transform; }
		void SetTransform(const glm::mat4& transform) { m_Transform = transform; }

		void SetPosition(const glm::vec3& position);
		void SetRotation(const glm::vec3& eulerAngles);  // In radians
		void SetScale(const glm::vec3& scale);
		void SetScale(float uniformScale);

		glm::vec3 GetPosition() const { return m_Position; }
		glm::vec3 GetRotation() const { return m_Rotation; }
		glm::vec3 GetScale() const { return m_Scale; }

		// Bounds (world space, considering transform)
		glm::vec3 GetBoundsMin() const { return m_BoundsMin; }
		glm::vec3 GetBoundsMax() const { return m_BoundsMax; }

		// Statistics
		size_t GetTotalVertexCount() const { return m_TotalVertices; }
		size_t GetTotalIndexCount() const { return m_TotalIndices; }

	private:
		void CalculateBounds();

		std::string m_Name;
		std::string m_SourcePath;

		// Owned resources
		std::vector<std::unique_ptr<Mesh>> m_Meshes;
		std::vector<std::unique_ptr<Material>> m_Materials;

		// Transform
		glm::mat4 m_Transform = glm::mat4(1.0f);
		glm::vec3 m_Position = glm::vec3(0.0f);
		glm::vec3 m_Rotation = glm::vec3(0.0f);
		glm::vec3 m_Scale = glm::vec3(1.0f);

		// Bounds
		glm::vec3 m_BoundsMin = glm::vec3(0.0f);
		glm::vec3 m_BoundsMax = glm::vec3(0.0f);

		// Statistics
		size_t m_TotalVertices = 0;
		size_t m_TotalIndices = 0;
	};

} // namespace Nightbloom