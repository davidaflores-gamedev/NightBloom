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

		LOG_INFO("Resource manager cleaned up");
	}

	VulkanBuffer* ResourceManager::CreateBuffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool hostVisible)
	{
		// Check if buffer already exists
		if (m_Buffers.find(name) != m_Buffers.end())
		{
			LOG_WARN("Buffer '{}' already exists, returning existing buffer", name);
			return m_Buffers[name].get();
		}

		// Create new buffer
		auto buffer = std::make_unique<VulkanBuffer>(m_Device, m_MemoryManager);

		bool success = false;

		if (hostVisible)
		{
			success = buffer->CreateHostVisible(size, usage);
		}
		else
		{
			success = buffer->CreateDeviceLocal(size, usage);
		}

		if (!success)
		{
			LOG_ERROR("Failed to create buffer '{}'", name);
			return nullptr;
		}

		// Store and return
		VulkanBuffer* ptr = buffer.get();
		m_Buffers[name] = std::move(buffer);

		LOG_INFO("Created buffer '{}' (size: {} bytes)", name, size);
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

	bool ResourceManager::CreateTestCube()
	{
		// Define cube vertices (8 unique vertices)

			std::vector<VertexPCU> vertices = {
			// Front face (red tones)
			{{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
			{{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.5f, 0.0f}, {1.0f, 0.0f}},
			{{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.5f}, {1.0f, 1.0f}},
			{{-0.5f,  0.5f,  0.5f}, {1.0f, 0.5f, 0.5f}, {0.0f, 1.0f}},

			// Back face (blue tones)
			{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
			{{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.5f, 1.0f}, {1.0f, 0.0f}},
			{{ 0.5f,  0.5f, -0.5f}, {0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}},
			{{-0.5f,  0.5f, -0.5f}, {0.5f, 0.5f, 1.0f}, {0.0f, 1.0f}}
		};

		// Define cube indices (12 triangles, 36 indices)
		std::vector<uint32_t> indices = {
			// Front face
			0, 1, 2,  2, 3, 0,
			// Back face
			4, 6, 5,  6, 4, 7,
			// Left face
			4, 0, 3,  3, 7, 4,
			// Right face
			1, 5, 6,  6, 2, 1,
			// Top face
			3, 2, 6,  6, 7, 3,
			// Bottom face
			4, 5, 1,  1, 0, 4
		};

		// Create vertex buffer
		VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
		VulkanBuffer* vertexBuffer = CreateBuffer(
			"TestCubeVertices",
			vertexBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			false  // Device local for performance
		);

		if (!vertexBuffer)
		{
			LOG_ERROR("Failed to create vertex buffer for test cube");
			return false;
		}

		// Upload vertex data
		if (!UploadBufferData(vertexBuffer, vertices.data(), vertexBufferSize))
		{
			LOG_ERROR("Failed to upload vertex data");
			DestroyBuffer("TestCubeVertices");
			return false;
		}

		// Create index buffer
		VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
		VulkanBuffer* indexBuffer = CreateBuffer(
			"TestCubeIndices",
			indexBufferSize,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			false  // Device local for performance
		);

		if (!indexBuffer)
		{
			LOG_ERROR("Failed to create index buffer for test cube");
			DestroyBuffer("TestCubeVertices");
			return false;
		}

		// Upload index data
		if (!UploadBufferData(indexBuffer, indices.data(), indexBufferSize))
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

	bool ResourceManager::UploadBufferData(VulkanBuffer* buffer, const void* data, size_t size)
	{
		if (!buffer || !data || size == 0)
		{
			LOG_ERROR("Invalid parameters for buffer upload");
			return false;
		}

		// Use the buffer's built-in upload functionality
		return buffer->UploadData(data, size, m_TransferCommandPool.get());
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
	//
	//VkCommandBuffer ResourceManager::BeginSingleTimeCommands()
	//{
	//	VkCommandBufferAllocateInfo allocInfo{};
	//	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	//	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	//	allocInfo.commandPool = m_TransferCommandPool->GetPool();
	//	allocInfo.commandBufferCount = 1;
	//
	//	VkCommandBuffer commandBuffer;
	//	vkAllocateCommandBuffers(m_Device->GetDevice(), &allocInfo, &commandBuffer);
	//
	//	VkCommandBufferBeginInfo beginInfo{};
	//	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	//	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	//
	//	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	//	return commandBuffer;
	//}
	//
	//void ResourceManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
	//{
	//	vkEndCommandBuffer(commandBuffer);
	//
	//	VkSubmitInfo submitInfo{};
	//	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	//	submitInfo.commandBufferCount = 1;
	//	submitInfo.pCommandBuffers = &commandBuffer;
	//
	//	vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
	//	vkQueueWaitIdle(m_Device->GetGraphicsQueue());
	//
	//	vkFreeCommandBuffers(m_Device->GetDevice(), m_TransferCommandPool->GetPool(),
	//		1, &commandBuffer);
	//}
	//
} // namespace Nightbloom