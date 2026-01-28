//------------------------------------------------------------------------------
// ModelLoader.cpp
//
// Implementation of glTF loading using cgltf
//------------------------------------------------------------------------------

#define CGLTF_IMPLEMENTATION
#include "ThirdParty/cgltf/cgltf.h"

#include "Engine/Renderer/GLTFLoader.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <filesystem>

namespace Nightbloom
{
	std::unique_ptr<ModelData> GLTFLoader::Load(const std::string& filepath)
	{
		LOG_INFO("LoadinggLTF: {}", filepath);

		// Store base path for resolving relative texture paths
		m_BasePath = std::filesystem::path(filepath).parent_path().string();
		if (!m_BasePath.empty() && m_BasePath.back() != '/' && m_BasePath.back() != '\\')
		{
			m_BasePath += '/';
		}
	
		// Parse the file
		cgltf_options options = {};
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
		if (result != cgltf_result_success)
		{
			m_LastError = "Failed to parse gLTF file: " + filepath;
			LOG_ERROR("{}", m_LastError);
			return nullptr;
		}

		// Load buffers (required for accessing vertex/index data)
		result = cgltf_load_buffers(&options, data, filepath.c_str());
		if (result != cgltf_result_success)
		{
			m_LastError = "Failed to load glTF buffers";
			LOG_ERROR("{}", m_LastError);
			cgltf_free(data);
			return nullptr;
		}

		// Create model data
		auto modelData = std::make_unique<ModelData>();
		modelData->name = std::filesystem::path(filepath).stem().string();
		modelData->sourcePath = filepath;

		// Parse materials first (meshes reference them by index)
		modelData->materials.reserve(data->materials_count);
		for (size_t i = 0; i < data->materials_count; ++i)
		{
			MaterialData matData;
			if (ParseMaterial(&data->materials[i], data, matData))
			{
				modelData->materials.push_back(std::move(matData));
			}
		}
		LOG_INFO("  Loaded {} materials", modelData->materials.size());

		for (size_t i = 0; i < data->meshes_count; ++i)
		{
			cgltf_mesh* gltfMesh = &data->meshes[i];

			for (size_t p = 0; p < gltfMesh->primitives_count; ++p)
			{
				MeshData meshData;
				meshData.name = gltfMesh->name ? gltfMesh->name : "Mesh " + std::to_string(i);

				if (gltfMesh->primitives_count > 1)
				{
					meshData.name += "_prim" + std::to_string(p);
				}

				cgltf_primitive* primitive = &gltfMesh->primitives[p];

				// Only support triangles for now TODO: add quad and other support
				if (primitive->type != cgltf_primitive_type_triangles)
				{
					LOG_WARN("    Skipping non-triangle promitive in mesh {}", meshData.name);
					continue;
				}

				// Read vertex attributes
				std::vector<glm::vec3> positions;
				std::vector<glm::vec3> normals;
				std::vector<glm::vec2> texCoords;

				for (size_t a = 0; a < primitive->attributes_count; ++a)
				{
					cgltf_attribute* attr = &primitive->attributes[a];

					switch (attr->type)
					{
						case cgltf_attribute_type_position:
							ReadPositions(attr->data, data, positions);
							break;
						case cgltf_attribute_type_normal:
							ReadNormals(attr->data, data, normals);
							break;
						case cgltf_attribute_type_texcoord:
							if (attr->index == 0)
							{
								ReadTexCoords(attr->data, data, texCoords);
							}
							break;
						default:
							//Ignore tangents, colors, etc. for now TODO: Implement loading for this.
							break;
					}
				}

				// Validate we have positions
				if (positions.empty())
				{
					LOG_WARN("  Mesh '{}' has no positions, skipping", meshData.name);
					continue;
				}

				// Generate normals if missing
				if (normals.empty())
				{
					LOG_WARN("  Mesh '{}' has no normals, using default up vector", meshData.name);
					// for now we are just assuming normals are going to be up if we dont hvae any
					normals.resize(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
				}

				// Generate texcoords if missing
				if (texCoords.empty())
				{
					// top right corner
					texCoords.resize(positions.size(), glm::vec2(0.0f));
				}

				// Build vertex buffer
				meshData.vertices.reserve(positions.size());
				for (size_t v = 0; v < positions.size(); ++v)
				{
					VertexPNT vertex;
					vertex.position = positions[v];
					vertex.normal = v < normals.size() ? normals [v] : glm::vec3(0, 1, 0);
					vertex.texCoord = v < texCoords.size() ? texCoords[v] : glm::vec2(0);

					meshData.vertices.push_back(vertex);

					// Update bounds
					meshData.boundsMin = glm::min(meshData.boundsMin, vertex.position);
					meshData.boundsMax = glm::max(meshData.boundsMax, vertex.position);
				}

				// Read indives
				if (primitive->indices)
				{
					ReadIndices(primitive->indices, data, meshData.indices);
				}
				else
				{
					// No indices - generate sequential indices
					meshData.indices.reserve(positions.size());
					for (size_t v = 0; v < positions.size(); ++v)
					{
						meshData.indices.push_back(static_cast<uint32_t>(v));
					}
				}

				if (primitive->material)
				{
					// Calculate index by pointer arithmetic
					meshData.materialIndex = static_cast<int32_t>(primitive->material - data->materials);
				}
				else
				{
					meshData.materialIndex = -1;  // No material assigned
				}

				// Update statistics
				modelData->totalVertices += meshData.vertices.size();
				modelData->totalIndices += meshData.indices.size();

				LOG_INFO("  Mesh '{}': {} vertices, {} indices, materialIndex {}",
					meshData.name, meshData.vertices.size(), meshData.indices.size(), meshData.materialIndex);

				modelData->meshes.push_back(std::move(meshData));
			}
		}
		// Cleanup cgltf
		cgltf_free(data);

		LOG_INFO("Loaded model '{}': {} meshes, {} total vertices, {} total indices",
			modelData->name, modelData->meshes.size(),
			modelData->totalVertices, modelData->totalIndices);

		return modelData;
	}

	bool GLTFLoader::ReadPositions(void* accessor, void* gltfData, std::vector<glm::vec3>& outPositions)
	{
		cgltf_accessor* acc = static_cast<cgltf_accessor*>(accessor);

		if (acc->type != cgltf_type_vec3)
		{
			LOG_ERROR("Position accessor is not vec3");
			return false;
		}

		outPositions.resize(acc->count);

		for (size_t i = 0; i < acc->count; ++i)
		{
			cgltf_accessor_read_float(acc, i, &outPositions[i].x, 3);
		}

		return true;
	}

	bool GLTFLoader::ReadNormals(void* accessor, void* gltfData, std::vector<glm::vec3>& outNormals)
	{
		cgltf_accessor* acc = static_cast<cgltf_accessor*>(accessor);

		if (acc->type != cgltf_type_vec3)
		{
			LOG_ERROR("Normal accessor is not vec3");
			return false;
		}

		outNormals.resize(acc->count);

		for (size_t i = 0; i < acc->count; ++i)
		{
			cgltf_accessor_read_float(acc, i, &outNormals[i].x, 3);
		}

		return true;
	}

	bool GLTFLoader::ReadTexCoords(void* accessor, void* gltfData, std::vector<glm::vec2>& outTexCoords)
	{
		cgltf_accessor* acc = static_cast<cgltf_accessor*>(accessor);

		if (acc->type != cgltf_type_vec2)
		{
			LOG_ERROR("TexCoord accessor is not vec2");
			return false;
		}

		outTexCoords.resize(acc->count);

		for (size_t i = 0; i < acc->count; ++i)
		{
			cgltf_accessor_read_float(acc, i, &outTexCoords[i].x, 2);
		}

		return true;
	}

	bool GLTFLoader::ReadIndices(void* accessor, void* gltfData, std::vector<uint32_t>& outIndices)
	{
		cgltf_accessor* acc = static_cast<cgltf_accessor*>(accessor);

		outIndices.resize(acc->count);

		for (size_t i = 0; i < acc->count; ++i)
		{
			outIndices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(acc, i));
		}

		return true;
	}

	bool GLTFLoader::ParseMaterial(void* gltfMaterial, void* gltfData, MaterialData& outMaterial)
	{
		cgltf_material* mat = static_cast<cgltf_material*>(gltfMaterial);
		cgltf_data* data = static_cast<cgltf_data*>(gltfData);

		outMaterial.name = mat->name ? mat->name : "Material";

		// PBR Metallic-Roughness workflow
		if (mat->has_pbr_metallic_roughness)
		{
			auto& pbr = mat->pbr_metallic_roughness;

			outMaterial.baseColorFactor = glm::vec4(
				pbr.base_color_factor[0],
				pbr.base_color_factor[1],
				pbr.base_color_factor[2],
				pbr.base_color_factor[3]
				);

			outMaterial.metallicFactor = pbr.metallic_factor;
			outMaterial.roughnessFactor = pbr.roughness_factor;

			// Base color texture
			if (pbr.base_color_texture.texture)
			{
				outMaterial.baseColorTexturePath = ResolveTexturePath(
					pbr.base_color_texture.texture, data);
			}

			// Metallic-roughness texture
			if (pbr.metallic_roughness_texture.texture)
			{
				outMaterial.metallicRoughnessTexturePath = ResolveTexturePath(
					pbr.metallic_roughness_texture.texture, data);
			}
		}

		// Normal map
		if (mat->normal_texture.texture)
		{
			outMaterial.normalTexturePath = ResolveTexturePath(
				mat->normal_texture.texture, data);
		}

		// Emissive
		if (mat->emissive_texture.texture)
		{
			outMaterial.emissiveTexturePath = ResolveTexturePath(
				mat->emissive_texture.texture, data);
		}
		outMaterial.emissiveFactor = glm::vec3(
			mat->emissive_factor[0],
			mat->emissive_factor[1],
			mat->emissive_factor[2]
		);

		// Rendering flags
		outMaterial.doubleSided = mat->double_sided;
		outMaterial.alphaCutoff = mat->alpha_cutoff;

		switch (mat->alpha_mode)
		{
		case cgltf_alpha_mode_opaque:
			outMaterial.alphaMode = MaterialData::AlphaMode::Opaque;
			break;
		case cgltf_alpha_mode_mask:
			outMaterial.alphaMode = MaterialData::AlphaMode::Mask;
			break;
		case cgltf_alpha_mode_blend:
			outMaterial.alphaMode = MaterialData::AlphaMode::Blend;
			break;
		}

		LOG_INFO("    Material '{}': baseColor=({:.2f},{:.2f},{:.2f}), metallic={:.2f}, roughness={:.2f}",
			outMaterial.name,
			outMaterial.baseColorFactor.r, outMaterial.baseColorFactor.g, outMaterial.baseColorFactor.b,
			outMaterial.metallicFactor, outMaterial.roughnessFactor);

		return true;
	}

	std::string GLTFLoader::ResolveTexturePath(void* gltfTexture, void* gltfData)
	{
		cgltf_texture* tex = static_cast<cgltf_texture*>(gltfTexture);

		if (!tex || !tex->image)
		{
			return "";
		}

		// Check for embedded data (base64 or buffer view)
		if (tex->image->buffer_view)
		{
			// Embedded texture - would need special handling
			LOG_WARN("Embedded textures not yet supported");
			return "";
		}

		// External file reference
		if (tex->image->uri)
		{
			std::string uri = tex->image->uri;

			// Check if it's a data URI (embedded base64)
			if (uri.rfind("data:", 0) == 0)
			{
				LOG_WARN("Data URI textures not yet supported");
				return "";
			}

			// It's a relative file path
			return m_BasePath + uri;
		}

		return "";
	}
}