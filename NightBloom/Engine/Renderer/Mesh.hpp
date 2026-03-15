//------------------------------------------------------------------------------
// Mesh.hpp
//
// GPU-resident mesh data (vertex buffer + index buffer)
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/RenderDevice.hpp"  // For Buffer base class
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace Nightbloom
{
	class Material;

	class Mesh
	{
	public:
		Mesh() = default;
		Mesh(const std::string& name) : m_Name(name) {}
		~Mesh() = default;

		// Mesh cannot be copied (owns GPU resources)
		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;

		// Mesh can be moved
		Mesh(Mesh&&) = default;
		Mesh& operator=(Mesh&&) = default;

		// Getters
		const std::string& GetName() const { return m_Name; }
		Buffer* GetVertexBuffer() const { return m_VertexBuffer.get(); }
		Buffer* GetIndexBuffer() const { return m_IndexBuffer.get(); }
		uint32_t GetIndexCount() const { return m_IndexCount; }
		uint32_t GetVertexCount() const { return m_VertexCount; }
		Material* GetMaterial() const { return m_Material; }

		// Bounds
		const glm::vec3& GetBoundsMin() const { return m_BoundsMin; }
		const glm::vec3& GetBoundsMax() const { return m_BoundsMax; }
		glm::vec3 GetCenter() const { return (m_BoundsMin + m_BoundsMax) * 0.5f; }
		glm::vec3 GetExtents() const { return (m_BoundsMax - m_BoundsMin) * 0.5f; }

		// Setters
		void SetName(const std::string& name) { m_Name = name; }
		void SetVertexBuffer(std::unique_ptr<Buffer> buffer) { m_VertexBuffer = std::move(buffer); }
		void SetIndexBuffer(std::unique_ptr<Buffer> buffer) { m_IndexBuffer = std::move(buffer); }
		void SetIndexCount(uint32_t count) { m_IndexCount = count; }
		void SetVertexCount(uint32_t count) { m_VertexCount = count; }
		void SetMaterial(Material* material) { m_Material = material; }
		void SetBounds(const glm::vec3& min, const glm::vec3& max) { m_BoundsMin = min; m_BoundsMax = max; }

		// Validity check
		bool IsValid() const { return m_VertexBuffer != nullptr && m_IndexCount > 0; }

	private:
		std::string m_Name;

		// GPU resources (owned)
		std::unique_ptr<Buffer> m_VertexBuffer;
		std::unique_ptr<Buffer> m_IndexBuffer;

		// Counts
		uint32_t m_IndexCount = 0;
		uint32_t m_VertexCount = 0;

		// Material (non-owning - Model or ResourceManager owns materials)
		Material* m_Material = nullptr;

		// Bounding box
		glm::vec3 m_BoundsMin = glm::vec3(0.0f);
		glm::vec3 m_BoundsMax = glm::vec3(0.0f);
	};

} // namespace Nightbloom