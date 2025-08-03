//------------------------------------------------------------------------------
// Renderer.cpp
//
// Abstraction layer for graphics rendering
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Renderer.hpp"
#include "Core/Logger/Logger.hpp"
#include "Core/Assert.hpp"

namespace Nightbloom
{
	struct Renderer::RendererData
	{
		void* windowHandle = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
		bool initialized = false;

		//Vulkan stuff will go here later.
	};

	Renderer::Renderer() : m_Data(std::make_unique<RendererData>())
	{
		LOG_INFO("Renderer created");
	}

	Renderer::~Renderer()
	{
		if (m_Data->initialized)
		{
			Shutdown();
		}
		LOG_INFO("Renderer destroyed");
	}

	bool Renderer::Initialize(void* windowHandle, uint32_t width, uint32_t height)
	{
		LOG_INFO("Initializing Renderer with window handle: {}, width: {}, height: {}", windowHandle, width, height);

		if (m_Data->initialized)
		{
			LOG_WARN("Renderer is already initialized");
			//not sure what to return here
			return true;
		}

		if (!windowHandle || width == 0 || height == 0)
		{
			LOG_ERROR("Invalid parameters for Renderer initialization");
			return false;
		}

		m_Data->windowHandle = windowHandle;
		m_Data->width = width;
		m_Data->height = height;

		try
		{
			//TODO: Initialize Vulkan or other graphics API here
			//for now, just set initialized to true
			m_Data->initialized = true;

			LOG_INFO("Renderer initialized successfully");
			return true;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("Renderer initialization failed: {}", e.what());
			return false;
		}
	}

	void Renderer::Shutdown()
	{
		if (!m_Data->initialized)
		{
			LOG_WARN("Renderer is not initialized, cannot shutdown");
			return;
		}

		LOG_INFO("Shutting down Renderer");

		//TODO: Cleanup Vulkan or other graphics API resources here

		m_Data->initialized = false;
		m_Data->windowHandle = nullptr;
		//m_Data->width = 0;
		//m_Data->height = 0;

		LOG_INFO("Renderer shutdown complete");
	}

	void Renderer::BeginFrame()
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot begin frame");
			return;
		}

		LOG_TRACE("Beginning frame");
	}

	void Renderer::EndFrame()
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot end frame");
			return;
		}

		LOG_TRACE("Ending frame");
	}

	void Renderer::Clear(float r, float g, float b, float a)
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot clear");
			return;
		}

		LOG_TRACE("Clearing screen with color: ({}, {}, {}, {})", r, g, b, a);
	}

	void Renderer::DrawTriangle()
	{
		if (!m_Data->initialized)
		{
			LOG_ERROR("Renderer not initialized, cannot draw triangle");
			return;
		}

		LOG_INFO("Draw triangle");
	}

	bool Renderer::IsInitialized() const
	{
		return m_Data->initialized;
	}
}



