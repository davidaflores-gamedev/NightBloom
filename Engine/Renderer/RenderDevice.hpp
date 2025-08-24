//------------------------------------------------------------------------------
// RenderDevice.hpp
//
// Abstract base class for rendering backends (Vulkan, DX12, etc.)
// This defines the interface that all rendering implementations must follow
//------------------------------------------------------------------------------

#pragma once

#include <memory>
#include <vector>
#include <string>

// Use the pipeline interface for shared enums
#include "Engine/Renderer/PipelineInterface.hpp"

namespace Nightbloom
{
	class Buffer;
	class Texture;
	class Shader;
	//class RenderTarget;
	class Pipeline;
	class CommandBuffer;
	class RenderPass;
	class Framebuffer;
	class IPipelineManager;

	enum class BufferType
	{
		Vertex,
		Index,
		Uniform,
		Storage,
		Staging  // For transfer operations
	};

	enum class TextureFormat
	{
		// Color formats
		RGBA8,
		BGRA8,
		RGB8,
		R8,
		RG8,

		// HDR formats
		R32F,
		RG32F,
		RGB32F,
		RGBA32F,
		R16F,
		RG16F,
		RGB16F,
		RGBA16F,

		// Depth/Stencil formats
		Depth24Stencil8,
		Depth32F,
		Depth16,

		// Compressed formats (future)
		BC1_RGB,
		BC1_RGBA,
		BC3_RGBA,
		BC7_RGBA
	};

	enum class TextureUsage
	{
		Sampled = 0x01,
		Storage = 0x02,
		RenderTarget = 0x04,
		DepthStencil = 0x08,
		Transfer = 0x10
	};

	// Allow bitwise operations for TextureUsage
	inline TextureUsage operator|(TextureUsage a, TextureUsage b)
	{
		return static_cast<TextureUsage>(static_cast<int>(a) | static_cast<int>(b));
	}

	inline TextureUsage operator&(TextureUsage a, TextureUsage b)
	{
		return static_cast<TextureUsage>(static_cast<int>(a) & static_cast<int>(b));
	}

	// ============================================================================
	// Descriptors
	// ============================================================================

	struct BufferDesc
	{
		BufferType type = BufferType::Vertex;
		size_t size = 0;
		void* initialData = nullptr;
		bool dynamic = false; // Can be updated frequently
		bool hostVisible = false;  // CPU accessible
	};

	struct TextureDesc
	{
		uint32_t width = 1;
		uint32_t height = 1;
		uint32_t depth = 1; // For 3D textures
		uint32_t mipLevels = 1;
		uint32_t arrayLayers = 1;  // For texture arrays
		TextureFormat format = TextureFormat::RGBA8;
		TextureUsage usage = TextureUsage::Sampled;
		void* initialData = nullptr;  // Optional initial data
		size_t initialDataSize = 0;
		bool isRenderTarget = false; // is this necessary?
		bool isDepthStencil = false;
	};

	struct ShaderDesc
	{
		ShaderStage stage = ShaderStage::Vertex;
		const void* code = nullptr; // SPIR-V bytecode for Vulkan
		size_t codeSize = 0; // may remove
		std::string entryPoint = "main";
		std::string sourcePath;  // Optional: for debugging/hot reload
	};

	// RenderPass and Framebuffer descriptors (simplified for now)
	struct RenderPassDesc
	{
		std::vector<TextureFormat> colorFormats;
		TextureFormat depthFormat = TextureFormat::Depth24Stencil8;
		bool hasDepth = true;
		// Add more as needed (load/store ops, etc.)
	};

	struct FramebufferDesc
	{
		RenderPass* renderPass = nullptr;
		std::vector<Texture*> colorAttachments;
		Texture* depthAttachment = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	// ============================================================================
	// RenderDevice Interface
	// ============================================================================

	//Abstract base class for render devices
	class RenderDevice
	{
	public:
		RenderDevice() = default;
		virtual ~RenderDevice() = default;

		// Initialization and shutdown
		virtual bool Initialize(void* windowHandle, uint32_t width, uint32_t height) = 0;
		virtual void Shutdown() = 0;

		// Resource creation
		virtual Buffer* CreateBuffer(const BufferDesc& desc) = 0;
		virtual Texture* CreateTexture(const TextureDesc& desc) = 0;
		virtual Shader* CreateShader(const ShaderDesc& desc) = 0;
		virtual RenderPass* CreateRenderPass(const RenderPassDesc& desc) = 0;
		virtual Framebuffer* CreateFramebuffer(const FramebufferDesc& desc) = 0;
		virtual CommandBuffer* CreateCommandBuffer() = 0;

		// Resource destruction
		virtual void DestroyBuffer(Buffer* buffer) = 0;
		virtual void DestroyTexture(Texture* texture) = 0;
		virtual void DestroyShader(Shader* shader) = 0;
		virtual void DestroyRenderPass(RenderPass* renderPass) = 0;
		virtual void DestroyFramebuffer(Framebuffer* framebuffer) = 0;
		virtual void DestroyCommandBuffer(CommandBuffer* commandBuffer) = 0;

		// Frame operations
		virtual void BeginFrame() = 0;
		virtual void EndFrame() = 0;
		virtual void Present() = 0;

		// Command submission
		virtual void SubmitCommandBuffer(CommandBuffer* commandBuffer) = 0;

		// Sync operations
		virtual void WaitForIdle() = 0;

		// Capabilities query
		virtual bool SupportsFeature(const std::string& featureName) const;
		//virtual std::vector<std::string> GetSupportedFeatures() const { return {}; }
		virtual size_t GetMinUniformBufferAlignment() const { return 256; }
		virtual size_t GetMinStorageBufferAlignment() const { return 256; }
		virtual uint32_t GetMaxTextureSize() const { return 4096; }
		virtual uint32_t GetMaxFramebufferSize() const { return 4096; }

	protected:
		// prevent copying and assignment
		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;
	};

	// ============================================================================
	// Resource Base Classes
	// ============================================================================

	class Buffer
	{
	public:
		virtual ~Buffer() = default;

		virtual size_t GetSize() const = 0;
		virtual BufferType GetType() const = 0;

		// CPU access (only for host-visible buffers)
		virtual void* Map() = 0; // Map buffer for CPU access
		virtual void Unmap() = 0; // Unmap buffer after access
		virtual void Update(const void* data, size_t size, size_t offset = 0) = 0; // Update buffer contents

		// For debugging
		virtual bool IsMapped() const = 0;
		virtual bool IsHostVisible() const = 0;
	};

	class Texture
	{
	public:
		virtual ~Texture() = default;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetDepth() const = 0;
		virtual uint32_t GetMipLevels() const = 0;
		virtual uint32_t GetArrayLayers() const = 0;
		virtual TextureFormat GetFormat() const = 0;
		virtual TextureUsage GetUsage() const = 0;
	};

	class Shader
	{
	public:
		virtual ~Shader() = default;
		virtual ShaderStage GetStage() const = 0;
		virtual const std::string& GetEntryPoint() const = 0;
		virtual const std::string& GetSourcePath() const = 0;  // For debugging/reload
	};

	class Pipeline
	{
	public:
		virtual ~Pipeline() = default;
		// Internal to the backend implementation
	};

	class RenderPass
	{
	public:
		virtual ~RenderPass() = default;
		// Internal to the backend implementation
	};

	class Framebuffer
	{
	public:
		virtual ~Framebuffer() = default;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
	};

	class CommandBuffer
	{
	public:
		virtual ~CommandBuffer() = default;

		//Recording commands
		virtual void Begin() = 0;
		virtual void End() = 0;
		virtual void Reset() = 0;

		// Basic rendering commands
		virtual void BeginRenderPass(RenderPass* renderPass, Framebuffer* framebuffer) = 0;
		virtual void EndRenderPass() = 0;

		// Pipeline and state
		virtual void SetPipeline(Pipeline* pipeline) = 0;
		virtual void SetViewport(float x, float y, float width, float height,
			float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
		virtual void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

		// Resource binding
		virtual void SetVertexbuffer(Buffer* buffer, uint32_t binding = 0) = 0;
		virtual void SetIndexBuffer(Buffer* buffer, uint32_t offset = 0) = 0;
		virtual void SetUniformBuffer(Buffer* buffer, uint32_t set, uint32_t binding = 0) = 0;
		virtual void SetTexture(Texture* texture, uint32_t set, uint32_t binding) = 0;

		virtual void Draw(	uint32_t vertexCount, uint32_t instanceCount = 1, 
							uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

		virtual void DrawIndexed(	uint32_t indexCount, uint32_t instanceCount = 1, 
									uint32_t firstIndex = 0, int32_t vertexOffset = 0, 
									uint32_t firstInstance = 0) = 0;

		// Clear commands
		virtual void ClearColor(float r, float g, float b, float a) = 0;
		virtual void ClearDepth(float depth) = 0;
		virtual void ClearStencil(uint32_t stencil) = 0;

		// Transfer operations
		virtual void CopyBuffer(Buffer* src, Buffer* dst, size_t size,
			size_t srcOffset = 0, size_t dstOffset = 0) = 0;
		virtual void CopyTexture(Texture* src, Texture* dst) = 0;
	};
}