//------------------------------------------------------------------------------
// Material.hpp
//
// PBR Material definition for rendering
// API-agnostic - uses abstract Texture interface
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/PipelineInterface.hpp"
#include "Engine/Renderer/RenderDevice.hpp"  // For Texture base class
#include <glm/glm.hpp>
#include <string>

namespace Nightbloom
{
	class Material
	{
	public:
		Material() = default;
		Material(const std::string& name) : m_Name(name) {}
		~Material() = default;

		// Getters
		const std::string& GetName() const { return m_Name; }
		const glm::vec4& GetAlbedoColor() const { return m_AlbedoColor; }
		Texture* GetAlbedoTexture() const { return m_AlbedoTexture; }
		Texture* GetNormalTexture() const { return m_NormalTexture; }
		float GetRoughness() const { return m_Roughness; }
		float GetMetallic() const { return m_Metallic; }
		PipelineType GetPipeline() const { return m_Pipeline; }
		bool IsDoubleSided() const { return m_DoubleSided; }

		// Setters
		void SetName(const std::string& name) { m_Name = name; }
		void SetAlbedoColor(const glm::vec4& color) { m_AlbedoColor = color; }
		void SetAlbedoTexture(Texture* texture) { m_AlbedoTexture = texture; }
		void SetNormalTexture(Texture* texture) { m_NormalTexture = texture; }
		void SetRoughness(float roughness) { m_Roughness = roughness; }
		void SetMetallic(float metallic) { m_Metallic = metallic; }
		void SetPipeline(PipelineType pipeline) { m_Pipeline = pipeline; }
		void SetDoubleSided(bool doubleSided) { m_DoubleSided = doubleSided; }

		// Check if material has a texture
		bool HasAlbedoTexture() const { return m_AlbedoTexture != nullptr; }
		bool HasNormalTexture() const { return m_NormalTexture != nullptr; }

	private:
		std::string m_Name = "Default";

		// PBR properties
		glm::vec4 m_AlbedoColor = glm::vec4(1.0f);
		float m_Roughness = 0.5f;
		float m_Metallic = 0.0f;

		// Textures (non-owning pointers - ResourceManager owns these)
		// Using abstract Texture* instead of VulkanTexture*
		Texture* m_AlbedoTexture = nullptr;
		Texture* m_NormalTexture = nullptr;

		// Rendering properties
		PipelineType m_Pipeline = PipelineType::Mesh;
		bool m_DoubleSided = false;
	};

} // namespace Nightbloom