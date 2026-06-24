//------------------------------------------------------------------------------
// FireflySystem.hpp
//
// GPU boids/flocking simulation rendered as glowing camera-facing billboards.
// Ported from a Forge-engine tech demo (.claude/firefly code/) — same agent
// data layout and flocking algorithm (separation/alignment/cohesion + wander
// + blink pulse), scoped down to a single configurable swarm volume and no
// special-fly (queen/predator) interaction.
//
// Agents live in a single GPU storage buffer, updated every frame by a raw
// compute pipeline (owned directly here, not via PipelineType — same pattern
// as NoiseTextureGenerator, since this is a continuous simulation rather than
// a one-shot utility or a generic per-frame draw). Rendering goes through the
// normal PipelineType::Firefly graphics pipeline, reading the same buffer by
// instance index.
//
// Usage:
//   FireflySystem fireflies;
//   fireflies.Initialize(renderer, agentCount, center, extents);
//   // Per frame, before the main render pass begins:
//   fireflies.DispatchCompute(cmd, dispatcher, frameIndex, deltaTime);
//   // (Renderer is responsible for the compute->vertex barrier afterward)
//   fireflies.SubmitDraw(drawList);
//   fireflies.Shutdown();
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/DrawCommandSystem.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Nightbloom
{
	class Renderer;
	class ResourceManager;
	class VulkanDescriptorManager;
	class VulkanBuffer;
	class ComputeDispatcher;

	// 4x vec4 = 64 bytes, matches the original Forge demo's AgentData layout
	struct FireflyAgentData
	{
		glm::vec3 position = glm::vec3(0.0f);
		float     brightness = 0.0f;

		glm::vec3 velocity = glm::vec3(0.0f);
		float     scale = 2.5f;

		float blinkPhase = 0.0f;
		float blinkSpeed = 1.0f;
		float minBrightness = 0.1f;
		float maxBrightness = 1.0f;

		glm::vec3 color = glm::vec3(1.0f);
		float     personality = 0.5f;
	};

	// Uploaded to the params UBO every frame. Swarm bounds are a positionable
	// box (center + extents) rather than a symmetric cube around the origin,
	// so a second independent swarm later is just another FireflySystem
	// instance with different bounds — not a redesign.
	struct FireflyParamsData
	{
		glm::vec4 params1 = glm::vec4(1.5f, 0.1f, 0.1f, 0.0f);   // separation, alignment, cohesion, deltaTime
		glm::vec4 params2 = glm::vec4(4.0f, 1.5f, 1.0f, 10.0f);  // perceptionRadius, separationRadius, minSpeed, maxSpeed
		glm::vec4 params3 = glm::vec4(0.0f, 0.35f, 2.0f, 0.0f);  // totalTime, wanderStrength, wanderForceScale, unused
		glm::vec4 params4 = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);   // globalBlinkSpeedScale, globalBlinkAmplitude, agentCount, unused
		glm::vec4 boundsCenter = glm::vec4(0.0f);                // xyz = swarm center
		glm::vec4 boundsExtent = glm::vec4(50.0f, 20.0f, 50.0f, 0.0f); // xyz = swarm half-extents
	};

	class FireflySystem
	{
	public:
		FireflySystem() = default;
		~FireflySystem() = default;

		FireflySystem(const FireflySystem&) = delete;
		FireflySystem& operator=(const FireflySystem&) = delete;

		bool Initialize(Renderer* renderer, uint32_t agentCount,
			const glm::vec3& center, const glm::vec3& extents);
		void Shutdown();

		// Uploads this frame's params (with deltaTime/totalTime baked in),
		// binds the compute pipeline + descriptor sets, and dispatches.
		// Caller (Renderer) is responsible for the compute->vertex barrier
		// afterward, since it owns the command buffer and barrier helpers.
		void DispatchCompute(VkCommandBuffer cmd, ComputeDispatcher* dispatcher,
			uint32_t frameIndex, float deltaTime);

		void SubmitDraw(DrawList& drawList) const;

		bool IsReady() const { return m_Ready; }

		// Live-tunable params — panel writes directly into this each frame
		FireflyParamsData& GetParams() { return m_Params; }
		const FireflyParamsData& GetParams() const { return m_Params; }

		// For Renderer's compute->vertex barrier
		VkBuffer GetAgentBuffer() const;
		VkDeviceSize GetAgentBufferSize() const { return m_AgentCount * sizeof(FireflyAgentData); }

	private:
		bool CreateComputePipeline();

		Renderer* m_Renderer = nullptr;
		ResourceManager* m_Resources = nullptr;
		VulkanDescriptorManager* m_DescriptorManager = nullptr;

		// GPU resources
		VulkanBuffer* m_AgentBuffer = nullptr;      // owned by ResourceManager's named buffer cache
		VulkanBuffer* m_ParamsBuffers[2] = { nullptr, nullptr }; // double-buffered, owned by ResourceManager

		VkDescriptorSet m_StorageDescriptorSet = VK_NULL_HANDLE;

		// Compute pipeline — owned directly (continuous simulation, not a
		// generic per-frame graphics draw), same pattern as NoiseTextureGenerator
		VkPipeline       m_ComputePipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_ComputePipelineLayout = VK_NULL_HANDLE;

		FireflyParamsData m_Params;
		uint32_t m_AgentCount = 0;
		float m_TotalTime = 0.0f;
		bool m_Ready = false;
	};

} // namespace Nightbloom
