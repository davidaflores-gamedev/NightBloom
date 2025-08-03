//------------------------------------------------------------------------------
// Renderer.hpp
//
// Abstraction layer for graphics rendering
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include <memory>
#include <string>

namespace Nightbloom
{
	class Renderer
	{
	private:
		struct RendererData;
		std::unique_ptr<RendererData> m_Data;

	public:
		Renderer();
		~Renderer();

		bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
		void Shutdown();

		void BeginFrame();
		void EndFrame();

		void Clear(float r = 1.0f, float g = 0.0f, float b = 1.0f, float a = 1.0f);

		// These will be implemented in steps
		void DrawTriangle();
		bool IsInitialized() const;

		// Additional rendering methods can be added here
		//const std::string& GetRendererName() const;
	};
}