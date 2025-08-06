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

	enum class BufferType
	{
		Vertex,
		Index,
		Uniform,
		Storage,
	};

	enum class TextureFormat
	{
		RGBA8,
		BGRA8,
		R32F,
		RGBA32F,
		Depth24Stencil8,
		Depth32F
	};

	enum class ShaderStage
	{
		Vertex,
		Fragment,
		Compute,
		Geometry,
		TessControl,
		TessEval
	};

	struct BufferDesc
	{
		BufferType type;
		size_t size;
		void* initialData = nullptr;
		bool dynamic = false; // Can be updated frequently
	};

	struct TextureDesc
	{
		uint32_t width;
		uint32_t height;
		uint32_t depth = 1; // For 3D textures
		uint32_t mipLevels = 1;
		TextureFormat format;
		bool isRenderTarget = false;
		bool isDepthStencil = false;	
		void* initialData = nullptr; // Optional initial data
	};

	struct ShaderDesc
	{
		ShaderStage stage;
		const void* code; // SPIR-V bytecode for Vulkan
		size_t codeSize; // may remove
		std::string entryPoint = "main";
	};

	struct PipelineDesc
	{
		// preliminary structure, will need to be expanded upon further for features such as vertex layour, blend states, rasterizer states, etc.
		std::vector<Shader*> shaders;
		RenderPass* renderPass = nullptr;
	};

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
		virtual Pipeline* CreatePipeline(const PipelineDesc& desc) = 0;
		virtual CommandBuffer* CreateCommandBuffer() = 0;

		// Resource destruction
		virtual void DestroyBuffer(Buffer* buffer) = 0;
		virtual void DestroyTexture(Texture* texture) = 0;
		virtual void DestroyShader(Shader* shader) = 0;
		virtual void DestroyPipeline(Pipeline* pipeline) = 0;
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
		virtual bool SupportsFeature(const std::string& featureName) const { return false; };
		//virtual std::vector<std::string> GetSupportedFeatures() const { return {}; }
		virtual size_t GetMinUniformBufferAlignment() const { return 256; } // Default alignment, can be overridden by specific implementations

	protected:
		// prevent copying and assignment
		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;
	};

	// Base classes for resources ( to be implemented by specific backends )
	class Buffer
	{
	public:
		virtual ~Buffer() = default;
		virtual size_t GetSize() const = 0;
		virtual BufferType GetType() const = 0;
		virtual void* Map() = 0; // Map buffer for CPU access
		virtual void Unmap() = 0; // Unmap buffer after access
		virtual void Update(const void* data, size_t size, size_t offset = 0) = 0; // Update buffer contents
	};

	class Texture
	{
	public:
		virtual ~Texture() = default;
		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetDepth() const = 0; // For 3D textures
		virtual TextureFormat GetFormat() const = 0;
		//virtual void* GetNativeHandle() const = 0; // Return native handle for API-specific operations
	};

	class Shader
	{
	public:
		virtual ~Shader() = default;
		virtual ShaderStage GetStage() const = 0;
		//virtual const std::string& GetEntryPoint() const = 0; // Return entry point name
		//virtual void* GetNativeHandle() const = 0; // Return native handle for API-specific operations
	};

	class Pipeline
	{
	public:
		virtual ~Pipeline() = default;
		// Pipeline state is mostly internal to the implementation
	};

	class CommandBuffer
	{
	public:
		virtual ~CommandBuffer() = default;

		//Recording commands
		virtual void Begin() = 0;
		virtual void End() = 0;

		// Basic rendering commands
		virtual void BeginRenderPass(RenderPass* renderPass, Framebuffer* framebuffer) = 0;
		virtual void EndRenderPass() = 0;

		virtual void SetPipeline(Pipeline* pipeline) = 0;
		virtual void SetVertexbuffer(Buffer* buffer, uint32_t binding = 0) = 0;
		virtual void SetIndexBuffer(Buffer* buffer) = 0;
		virtual void SetUniformBuffer(Buffer* buffer, uint32_t set, uint32_t binding = 0) = 0;

		virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
		virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

		// Clear commands
		virtual void ClearColor(float r, float g, float b, float a) = 0;
		virtual void ClearDepth(float depth) = 0;
	};
}