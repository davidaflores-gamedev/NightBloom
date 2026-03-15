//------------------------------------------------------------------------------
// ModelLoader.hpp
//
// Loads glTF/glb files and extracts geometry data
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Vertex.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Nightbloom
{
	// Raw mesh data extracted from glTF (CPU-side, ready for GPU upload)
	struct MeshData
	{
		std::string name;
		std::vector<VertexPNT> vertices;
		std::vector<uint32_t> indices;

		// Bounding box for culling
		glm::vec3 boundsMin = glm::vec3(FLT_MAX);
		glm::vec3 boundsMax = glm::vec3(-FLT_MAX);

		// Material index (-1 if no material)
		int32_t materialIndex = -1;
	};

	// Raw material data extracted from glTF
	struct MaterialData
	{
		std::string name;

		// PBR Metallic-Roughness
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;

		// Texture paths (empty if no texture)
		std::string baseColorTexturePath;
		std::string metallicRoughnessTexturePath;
		std::string normalTexturePath;
		std::string emissiveTexturePath;

		// Emissive
		glm::vec3 emissiveFactor = glm::vec3(0.0f);

		// Rendering flags
		bool doubleSided = false;
		enum class AlphaMode { Opaque, Mask, Blend } alphaMode = AlphaMode::Opaque;
		float alphaCutoff = 0.5f;
	};

	// Complete loaded model data
	struct ModelData
	{
		std::string name;
		std::string sourcePath;
		std::vector<MeshData> meshes;
		std::vector<MaterialData> materials;

		// Statistics
		size_t totalVertices = 0;
		size_t totalIndices = 0;
	};

	class GLTFLoader
	{
	public:
		GLTFLoader() = default;
		~GLTFLoader() = default;

		// Load a glTF or glb file
		// Returns nullptr on failure
		std::unique_ptr<ModelData> Load(const std::string& filepath);

		// Get last error message
		const std::string& GetLastError() const { return m_LastError; }

	private:
		std::string m_LastError;
		std::string m_BasePath;  // Directory containing the gltf file

		// Internal parsing helpers
		bool ParseMesh(void* gltfMesh, void* gltfData, MeshData& outMesh);
		bool ParseMaterial(void* gltfMaterial, void* gltfData, MaterialData& outMaterial);

		// Accessor helpers
		bool ReadPositions(void* accessor, void* gltfData, std::vector<glm::vec3>& outPositions);
		bool ReadNormals(void* accessor, void* gltfData, std::vector<glm::vec3>& outNormals);
		bool ReadTexCoords(void* accessor, void* gltfData, std::vector<glm::vec2>& outTexCoords);
		bool ReadIndices(void* accessor, void* gltfData, std::vector<uint32_t>& outIndices);

		// Texture path resolution
		std::string ResolveTexturePath(void* gltfTexture, void* gltfData);
	};

} // namespace Nightbloom