//------------------------------------------------------------------------------
// ResourceManager.cpp
//
// Implementation of GPU resource management
//------------------------------------------------------------------------------

#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/AssetManager.hpp" 
#include "Engine/Renderer/TextureLoader.hpp"
#include "Engine/Renderer/Vertex.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <vector>

namespace Nightbloom
{
	bool ResourceManager::Initialize(VulkanDevice* device, VulkanMemoryManager* memoryManager)
	{
		m_Device = device;
		m_MemoryManager = memoryManager;

		// Create transfer command pool for staging uploads
		m_TransferCommandPool = std::make_unique<VulkanCommandPool>(device);

		auto queueFamilies = device->GetQueueFamilyIndices();
		if (!m_TransferCommandPool->Initialize(
			queueFamilies.graphicsFamily.value(),
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT))
		{
			LOG_ERROR("Failed to create transfer command pool");
			return false;
		}

		LOG_INFO("Resource manager initialized");
		return true;
	}

	void ResourceManager::Cleanup()
	{
		// DestroyAllResources
		DestroyAllTextures();
		DestroyAllShaders();

		// Destroy all buffers
		for (auto& [name, buffer] : m_Buffers)
		{
			LOG_INFO("Destroying buffer: {}", name);
			buffer.reset();  // VulkanBuffer destructor handles cleanup
		}
		m_Buffers.clear();

		// Destroy command pool
		if (m_TransferCommandPool)
		{
			m_TransferCommandPool->Shutdown();
			m_TransferCommandPool.reset();
		}

		m_Device = nullptr;
		m_MemoryManager = nullptr;
		m_DescriptorManager = nullptr;

		LOG_INFO("Resource manager cleaned up");
	}

	VulkanBuffer* ResourceManager::CreateVertexBuffer(const std::string& name, size_t size, bool hostVisible)
	{
		BufferDesc desc;
		desc.usage = BufferUsage::Vertex;
		desc.memoryAccess = hostVisible ? MemoryAccess::CpuToGpu : MemoryAccess::GpuOnly;
		desc.size = size;
		desc.debugName = name;

		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);
		if (!buffer->Initialize(desc))
			return nullptr;

		// Store and return
		VulkanBuffer* ptr = buffer.get();
		m_Buffers[name] = std::move(buffer);
		return ptr;
	}

	VulkanBuffer* ResourceManager::CreateIndexBuffer(const std::string& name, size_t size, bool hostVisible)
	{
		BufferDesc desc;
		desc.usage = BufferUsage::Index;
		desc.memoryAccess = hostVisible ? MemoryAccess::CpuToGpu : MemoryAccess::GpuOnly;
		desc.size = size;
		desc.debugName = name;

		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);
		if (!buffer->Initialize(desc))
			return nullptr;

		// Store and return
		VulkanBuffer* ptr = buffer.get();
		m_Buffers[name] = std::move(buffer);
		return ptr;
	}

	VulkanBuffer* ResourceManager::CreateUniformBuffer(const std::string& name, size_t size)
	{
		BufferDesc desc;
		desc.usage = BufferUsage::Uniform;
		desc.memoryAccess = MemoryAccess::CpuToGpu;
		desc.size = size;
		desc.persistentMap = true;  // KEY: Keep mapped for performance
		desc.debugName = name;

		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);
		if (!buffer->Initialize(desc))
			return nullptr;

		VulkanBuffer* ptr = buffer.get();
		m_Buffers[name] = std::move(buffer);
		return ptr;
	}

	VulkanBuffer* ResourceManager::CreateStorageBuffer(const std::string& name, size_t size, bool hostVisible)
	{
		BufferDesc desc;
		desc.usage = BufferUsage::Storage;
		desc.memoryAccess = hostVisible ? MemoryAccess::CpuToGpu : MemoryAccess::GpuOnly;
		desc.size = size;
		desc.debugName = name;

		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);
		if (!buffer->Initialize(desc))
			return nullptr;

		// Store and return
		VulkanBuffer* ptr = buffer.get();
		m_Buffers[name] = std::move(buffer);
		return ptr;
	}

	VulkanBuffer* ResourceManager::GetBuffer(const std::string& name)
	{
		auto it = m_Buffers.find(name);
		if (it != m_Buffers.end())
		{
			return it->second.get();
		}
		return nullptr;
	}

	void ResourceManager::DestroyBuffer(const std::string& name)
	{
		auto it = m_Buffers.find(name);
		if (it != m_Buffers.end())
		{
			LOG_INFO("Destroying buffer: {}", name);
			m_Buffers.erase(it);
		}
		else
		{
			LOG_WARN("Attempted to destroy non-existent buffer: {}", name);
		}
	}

	std::unique_ptr<VulkanBuffer> ResourceManager::CreateVertexBufferUnique(const std::string& name, size_t size, bool hostVisible)
	{
		BufferDesc desc;
		desc.usage = BufferUsage::Vertex;
		desc.memoryAccess = hostVisible ? MemoryAccess::CpuToGpu : MemoryAccess::GpuOnly;
		desc.size = size;
		desc.debugName = name;

		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);
		if (!buffer->Initialize(desc))
			return nullptr;

		return buffer;
	}

	std::unique_ptr<VulkanBuffer> ResourceManager::CreateIndexBufferUnique(const std::string& name, size_t size, bool hostVisible)
	{
		BufferDesc desc;
		desc.usage = BufferUsage::Index;
		desc.memoryAccess = hostVisible ? MemoryAccess::CpuToGpu : MemoryAccess::GpuOnly;
		desc.size = size;
		desc.debugName = name;

		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);
		if (!buffer->Initialize(desc))
			return nullptr;

		return buffer;
	}

	VulkanShader* ResourceManager::LoadShader(const std::string& name, ShaderStage stage,
		const std::string& filename)
	{
		// Check if already loaded
		auto it = m_Shaders.find(name);
		if (it != m_Shaders.end())
		{
			LOG_WARN("Shader '{}' already loaded, returning existing", name);
			return it->second.get();
		}

		// Load shader binary through AssetManager
		auto shaderCode = AssetManager::Get().LoadShaderBinary(filename);
		if (shaderCode.empty())
		{
			LOG_ERROR("Failed to load shader file: {}", filename);
			return nullptr;
		}

		// Create VulkanShader
		auto shader = std::make_unique<VulkanShader>(m_Device, stage);
		if (!shader->CreateFromSpirV(shaderCode, "main"))
		{
			LOG_ERROR("Failed to create shader from SPIR-V: {}", filename);
			return nullptr;
		}

		VulkanShader* ptr = shader.get();
		m_Shaders[name] = std::move(shader);

		LOG_INFO("Loaded shader '{}' from {}", name, filename);
		return ptr;
	}

	VulkanShader* ResourceManager::GetShader(const std::string& name)
	{
		auto it = m_Shaders.find(name);
		if (it != m_Shaders.end())
		{
			return it->second.get();
		}
		return nullptr;
	}

	void ResourceManager::DestroyShader(const std::string& name)
	{
		auto it = m_Shaders.find(name);
		if (it != m_Shaders.end())
		{
			LOG_INFO("Destroying shader: {}", name);
			m_Shaders.erase(it);
		}
	}

	void ResourceManager::DestroyAllShaders()
	{
		LOG_INFO("Destroying all {} shaders", m_Shaders.size());
		m_Shaders.clear();
	}

	VulkanTexture* ResourceManager::LoadTexture(const std::string& name, const std::string& filepath)
	{
		// Check if texture already exists
		if (m_Textures.find(name) != m_Textures.end())
		{
			LOG_WARN("Texture '{}' already exists, returning existing texture", name);
			return m_Textures[name].get();
		}

		std::string fullPath;

		// Check if filepath is already absolute
		std::filesystem::path p(filepath);
		if (p.is_absolute())
		{
			fullPath = filepath;  // Use as-is
		}
		else
		{
			fullPath = AssetManager::Get().GetTexturePath(filepath);  // Prepend base path
		}

		// Load image data
		ImageData imageData = TextureLoader::LoadImageRGBA(fullPath);
		if (imageData.pixels.empty())
		{
			LOG_ERROR("Failed to load texture file: {}", fullPath);
			return nullptr;
		}

		// Create texture description
		TextureDesc desc;
		desc.width = imageData.width;
		desc.height = imageData.height;
		desc.format = imageData.channels == 4 ? TextureFormat::RGBA8 : TextureFormat::RGB8;
		desc.mipLevels = 1; // TODO: Calculate mip levels
		desc.usage = TextureUsage::Sampled | TextureUsage::Transfer;

		// Create VulkanTexture
		auto texture = std::make_unique<VulkanTexture>(m_Device, m_MemoryManager);
		if (!texture->Initialize(desc))
		{
			LOG_ERROR("Failed to create texture '{}'", name);
			return nullptr;
		}

		// Upload image data
		if (!texture->UploadData(imageData.pixels.data(), imageData.pixels.size(), m_TransferCommandPool.get()))
		{
			LOG_ERROR("Failed to upload texture data for '{}'", name);
			return nullptr;
		}

		if (m_DescriptorManager)
		{
			if (!texture->CreateDescriptorSet(m_DescriptorManager))
			{
				LOG_WARN("Failed to create descriptor set for texture '{}' - rendering may fail", name);
			}
		}

		// Store and return
		VulkanTexture* ptr = texture.get();
		m_Textures[name] = std::move(texture);

		LOG_INFO("Loaded texture '{}' from {} ({}x{}, {} channels)",
			name, filepath, imageData.width, imageData.height, imageData.channels);

		return ptr;
	}

	VulkanTexture* ResourceManager::CreateTexture(const std::string& name, const TextureDesc& desc)
	{
		// Check if texture already exists
		if (m_Textures.find(name) != m_Textures.end())
		{
			LOG_WARN("Texture '{}' already exists, returning existing texture", name);
			return m_Textures[name].get();
		}

		// Create VulkanTexture
		auto texture = std::make_unique<VulkanTexture>(m_Device, m_MemoryManager);
		if (!texture->Initialize(desc))
		{
			LOG_ERROR("Failed to create texture '{}'", name);
			return nullptr;
		}

		// Store and return
		VulkanTexture* ptr = texture.get();
		m_Textures[name] = std::move(texture);

		LOG_INFO("Created texture '{}' ({}x{}, format: {})",
			name, desc.width, desc.height, static_cast<int>(desc.format));

		return ptr;
	}

	VulkanTexture* ResourceManager::CreateTextureFromMemory(const std::string& name, const void* data,
		size_t size, const TextureDesc& desc)
	{
		// Create the texture
		VulkanTexture* texture = CreateTexture(name, desc);
		if (!texture)
		{
			return nullptr;
		}

		// Upload the data
		if (!texture->UploadData(data, size, m_TransferCommandPool.get()))
		{
			LOG_ERROR("Failed to upload data to texture '{}'", name);
			DestroyTexture(name);
			return nullptr;
		}

		if (m_DescriptorManager)
		{
			if (!texture->CreateDescriptorSet(m_DescriptorManager))
			{
				LOG_WARN("Failed to create descriptor set for texture '{}' - rendering may fail", name);
			}
		}

		return texture;
	}

	VulkanTexture* ResourceManager::GetTexture(const std::string& name)
	{
		auto it = m_Textures.find(name);
		if (it != m_Textures.end())
		{
			return it->second.get();
		}
		return nullptr;
	}

	void ResourceManager::DestroyTexture(const std::string& name)
	{
		auto it = m_Textures.find(name);
		if (it != m_Textures.end())
		{
			LOG_INFO("Destroying texture: {}", name);
			m_Textures.erase(it);
		}
		else
		{
			LOG_WARN("Attempted to destroy non-existent texture: {}", name);
		}
	}

	void ResourceManager::DestroyAllTextures()
	{
		LOG_INFO("Destroying all {} textures", m_Textures.size());
		m_Textures.clear();
	}

	bool ResourceManager::CreateTestCube()
	{
		// Define cube vertices (8 unique vertices)

			std::vector<VertexPCU> vertices = {
				// FRONT (z = +0.5), normal ~ (0,0,1)
			{{-0.5f,-0.5f, 0.5f},{1,0,0},{0,0}},
			{{ 0.5f,-0.5f, 0.5f},{1,0.5f,0},{1,0}},
			{{ 0.5f, 0.5f, 0.5f},{1,0,0.5f},{1,1}},
			{{-0.5f, 0.5f, 0.5f},{1,0.5f,0.5f},{0,1}},

			// BACK (z = -0.5), normal ~ (0,0,-1)
			// Flip winding or UV to avoid mirror; this mapping keeps checker upright.
			{{ 0.5f,-0.5f,-0.5f},{0,0,1},{0,0}},
			{{-0.5f,-0.5f,-0.5f},{0,0.5f,1},{1,0}},
			{{-0.5f, 0.5f,-0.5f},{0.5f,0,1},{1,1}},
			{{ 0.5f, 0.5f,-0.5f},{0.5f,0.5f,1},{0,1}},

			// LEFT (x = -0.5), normal ~ (-1,0,0)   map z->u, y->v
			{{-0.5f,-0.5f,-0.5f},{0,1,0},{0,0}},
			{{-0.5f,-0.5f, 0.5f},{0,1,0.5f},{1,0}},
			{{-0.5f, 0.5f, 0.5f},{0.5f,1,0.5f},{1,1}},
			{{-0.5f, 0.5f,-0.5f},{0.5f,1,0},{0,1}},

			// RIGHT (x = +0.5), normal ~ (1,0,0)   map z->u, y->v
			{{ 0.5f,-0.5f, 0.5f},{0,1,0},{0,0}},
			{{ 0.5f,-0.5f,-0.5f},{0,1,0.5f},{1,0}},
			{{ 0.5f, 0.5f,-0.5f},{0.5f,1,0.5f},{1,1}},
			{{ 0.5f, 0.5f, 0.5f},{0.5f,1,0},{0,1}},

			// TOP (y = +0.5), normal ~ (0,1,0)    map x->u, z->v
			{{-0.5f, 0.5f, 0.5f},{1,1,1},{0,0}},
			{{ 0.5f, 0.5f, 0.5f},{1,1,1},{1,0}},
			{{ 0.5f, 0.5f,-0.5f},{1,1,1},{1,1}},
			{{-0.5f, 0.5f,-0.5f},{1,1,1},{0,1}},

			// BOTTOM (y = -0.5), normal ~ (0,-1,0) map x->u, z->v
			{{-0.5f,-0.5f,-0.5f},{1,1,1},{0,0}},
			{{ 0.5f,-0.5f,-0.5f},{1,1,1},{1,0}},
			{{ 0.5f,-0.5f, 0.5f},{1,1,1},{1,1}},
			{{-0.5f,-0.5f, 0.5f},{1,1,1},{0,1}},
		};

		// Define cube indices (12 triangles, 36 indices)
			std::vector<uint32_t> indices;

			indices.reserve(6 * 6); // 6 faces * 2 tris * 3 idx
			for (uint32_t f = 0; f < 6; ++f) {
				uint32_t base = f * 4;
				// CCW winding for front-face
				indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
				indices.push_back(base + 2); indices.push_back(base + 3); indices.push_back(base + 0);
			}

		// Create vertex buffer
		VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
		VulkanBuffer* vertexBuffer = CreateVertexBuffer(
			"TestCubeVertices",
			vertexBufferSize,
			false  // Device local for performance
		);

		if (!vertexBuffer)
		{
			LOG_ERROR("Failed to create vertex buffer for test cube");
			return false;
		}

		// Upload vertex data
		if (!vertexBuffer->UploadData(vertices.data(), vertexBufferSize, 0, m_TransferCommandPool.get()))
		{
			LOG_ERROR("Failed to upload vertex data");
			DestroyBuffer("TestCubeVertices");
			return false;
		}

		// Create index buffer
		VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
		VulkanBuffer* indexBuffer = CreateIndexBuffer(
			"TestCubeIndices",
			indexBufferSize,
			false  // Device local for performance
		);

		if (!indexBuffer)
		{
			LOG_ERROR("Failed to create index buffer for test cube");
			DestroyBuffer("TestCubeVertices");
			return false;
		}

		// Upload index data
		if (!indexBuffer->UploadData(indices.data(), indexBufferSize, 0, m_TransferCommandPool.get()))
		{
			LOG_ERROR("Failed to upload index data");
			DestroyBuffer("TestCubeVertices");
			DestroyBuffer("TestCubeIndices");
			return false;
		}

		m_TestIndexCount = static_cast<uint32_t>(indices.size());

		LOG_INFO("Created test cube with {} vertices and {} indices",
			vertices.size(), indices.size());
		return true;
	}

	Buffer* ResourceManager::GetTestVertexBuffer() const
	{
		auto it = m_Buffers.find("TestCubeVertices");
		if (it != m_Buffers.end())
		{
			return static_cast<Buffer*>(it->second.get());
		}
		return nullptr;
	}

	Buffer* ResourceManager::GetTestIndexBuffer() const
	{
		auto it = m_Buffers.find("TestCubeIndices");
		if (it != m_Buffers.end())
		{
			return static_cast<Buffer*>(it->second.get());
		}
		return nullptr;
	}

	bool ResourceManager::CreateDefaultTextures()
	{
		LOG_INFO("Creating default textures");

		// Create white texture (2x2)
		{
			ImageData whiteData = TextureLoader::CreateSolidColor(2, 2, 255, 255, 255, 255);
			TextureDesc desc;
			desc.width = 2;
			desc.height = 2;
			desc.format = TextureFormat::RGBA8;
			desc.usage = TextureUsage::Sampled | TextureUsage::Transfer;

			if (!CreateTextureFromMemory("default_white", whiteData.pixels.data(),
				whiteData.pixels.size(), desc))
			{
				LOG_ERROR("Failed to create default white texture");
				return false;
			}
		}

		// Create black texture (2x2)
		{
			ImageData blackData = TextureLoader::CreateSolidColor(2, 2, 0, 0, 0, 255);
			TextureDesc desc;
			desc.width = 2;
			desc.height = 2;
			desc.format = TextureFormat::RGBA8;
			desc.usage = TextureUsage::Sampled | TextureUsage::Transfer;

			if (!CreateTextureFromMemory("default_black", blackData.pixels.data(),
				blackData.pixels.size(), desc))
			{
				LOG_ERROR("Failed to create default black texture");
				return false;
			}
		}

		// Create default normal map (flat normal pointing up)
		{
			// Normal map: R=128 (x=0), G=128 (y=0), B=255 (z=1), A=255
			ImageData normalData = TextureLoader::CreateSolidColor(2, 2, 128, 128, 255, 255);
			TextureDesc desc;
			desc.width = 2;
			desc.height = 2;
			desc.format = TextureFormat::RGBA8;
			desc.usage = TextureUsage::Sampled | TextureUsage::Transfer;

			if (!CreateTextureFromMemory("default_normal", normalData.pixels.data(),
				normalData.pixels.size(), desc))
			{
				LOG_ERROR("Failed to create default normal texture");
				return false;
			}
		}

		// Create UV debug checkerboard (64x64, 8x8 checks)
		{
			ImageData checkerData = TextureLoader::CreateCheckerboard(64, 64, 8);
			TextureDesc desc;
			desc.width = 64;
			desc.height = 64;
			desc.format = TextureFormat::RGBA8;
			desc.usage = TextureUsage::Sampled | TextureUsage::Transfer;

			if (!CreateTextureFromMemory("uv_checker", checkerData.pixels.data(),
				checkerData.pixels.size(), desc))
			{
				LOG_ERROR("Failed to create UV checker texture");
				return false;
			}
		}

		LOG_INFO("Created {} default textures", m_Textures.size());
		return true;
	}
	
	size_t ResourceManager::GetTotalBufferMemory() const
	{
		size_t total = 0;
		for (const auto& [name, buffer] : m_Buffers)
		{
			total += buffer->GetSize();
		}
		return total;
	}
	
	size_t ResourceManager::GetTotalTextureMemory() const
	{
		size_t total = 0;
		for (const auto& [name, texture] : m_Textures)
		{
			// Estimate based on dimensions and format
			uint32_t bytesPerPixel = 4; // Assume RGBA8 for now
			total += texture->GetWidth() * texture->GetHeight() * bytesPerPixel;
		}
		return total;
	}
} // namespace Nightbloom