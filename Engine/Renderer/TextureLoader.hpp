//------------------------------------------------------------------------------
// TextureLoader.hpp
//
// Handles loading image files using stb_image
//------------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Nightbloom
{
	struct ImageData
	{
		std::vector<uint8_t> pixels;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t channels = 0;
		bool isHDR = false;
		size_t pixelSize = 0; // bytes per pixel
	};

	class TextureLoader
	{
	public:
		// Load image from file
		static ImageData LoadImageRGBA(const std::string& filepath, bool forceRGBA = true);

		// Free image data
		static void FreeImage(ImageData& data);

		// Utility functions
		static bool GenerateMipmaps(ImageData& data);
		static void FlipVertical(ImageData& data);

		// Create solid color image
		static ImageData CreateSolidColor(uint32_t width, uint32_t height,
			uint8_t r, uint8_t g, uint8_t b, uint8_t a);

		// Create checkerboard pattern (useful for debugging UVs)
		static ImageData CreateCheckerboard(uint32_t width, uint32_t height,
			uint32_t checkSize = 8);
	};
}