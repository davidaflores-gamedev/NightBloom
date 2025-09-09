//------------------------------------------------------------------------------
// TextureLoader.cpp
//
// Image loading implementation using stb_image
//------------------------------------------------------------------------------

#include "Engine/Renderer/TextureLoader.hpp"
#include "Engine/Core/Logger/Logger.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb/stb_image.h"

#include <algorithm>
#include <cstring>

namespace Nightbloom
{
	ImageData TextureLoader::LoadImageRGBA(const std::string& filepath, bool forceRGBA)
	{
		ImageData data;

		int width, height, channels;
		int desiredChannels = forceRGBA ? STBI_rgb_alpha : STBI_default;

		// Check if HDR
		if (stbi_is_hdr(filepath.c_str()))
		{
			float* pixels = stbi_loadf(filepath.c_str(), &width, &height, &channels, desiredChannels);
			if (pixels)
			{
				data.isHDR = true;
				data.width = width;
				data.height = height;
				data.channels = forceRGBA ? 4 : channels;
				data.pixelSize = data.channels * sizeof(float);

				size_t dataSize = width * height * data.pixelSize;
				data.pixels.resize(dataSize);
				memcpy(data.pixels.data(), pixels, dataSize);

				stbi_image_free(pixels);
				LOG_INFO("Loaded HDR texture: {} ({}x{}, {} channels)",
					filepath, width, height, data.channels);
			}
			else
			{
				LOG_ERROR("Failed to load HDR image: {}", filepath);
			}
		}
		else
		{
			// Load LDR image
			unsigned char* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, desiredChannels);
			if (pixels)
			{
				data.isHDR = false;
				data.width = width;
				data.height = height;
				data.channels = forceRGBA ? 4 : channels;
				data.pixelSize = data.channels;

				size_t dataSize = width * height * data.pixelSize;
				data.pixels.resize(dataSize);
				memcpy(data.pixels.data(), pixels, dataSize);

				stbi_image_free(pixels);
				LOG_INFO("Loaded texture: {} ({}x{}, {} channels)",
					filepath, width, height, data.channels);
			}
			else
			{
				LOG_ERROR("Failed to load image: {} - {}", filepath, stbi_failure_reason());
			}
		}

		return data;
	}

	void TextureLoader::FreeImage(ImageData& data)
	{
		data.pixels.clear();
		data.width = 0;
		data.height = 0;
		data.channels = 0;
		data.pixelSize = 0;
	}

	bool TextureLoader::GenerateMipmaps(ImageData& data)
	{
		// Simple box filter mipmap generation
		// This is a basic implementation - could be improved with better filtering
		if (data.pixels.empty() || data.isHDR)
		{
			LOG_WARN("Cannot generate mipmaps for empty or HDR image");
			return false;
		}

		// For now, just return true - implement actual mipmap generation later
		LOG_WARN("Mipmap generation not yet implemented");
		return true;
	}

	void TextureLoader::FlipVertical(ImageData& data)
	{
		if (data.pixels.empty()) return;

		size_t rowSize = data.width * data.pixelSize;
		std::vector<uint8_t> tempRow(rowSize);

		for (uint32_t y = 0; y < data.height / 2; ++y)
		{
			uint32_t topRow = y;
			uint32_t bottomRow = data.height - 1 - y;

			uint8_t* topPtr = data.pixels.data() + topRow * rowSize;
			uint8_t* bottomPtr = data.pixels.data() + bottomRow * rowSize;

			// Swap rows
			memcpy(tempRow.data(), topPtr, rowSize);
			memcpy(topPtr, bottomPtr, rowSize);
			memcpy(bottomPtr, tempRow.data(), rowSize);
		}
	}

	ImageData TextureLoader::CreateSolidColor(uint32_t width, uint32_t height,
		uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		ImageData data;
		data.width = width;
		data.height = height;
		data.channels = 4;
		data.pixelSize = 4;
		data.isHDR = false;

		size_t dataSize = width * height * 4;
		data.pixels.resize(dataSize);

		for (uint32_t i = 0; i < width * height; ++i)
		{
			data.pixels[i * 4 + 0] = r;
			data.pixels[i * 4 + 1] = g;
			data.pixels[i * 4 + 2] = b;
			data.pixels[i * 4 + 3] = a;
		}

		return data;
	}

	ImageData TextureLoader::CreateCheckerboard(uint32_t width, uint32_t height, uint32_t checkSize)
	{
		ImageData data;
		data.width = width;
		data.height = height;
		data.channels = 4;
		data.pixelSize = 4;
		data.isHDR = false;

		size_t dataSize = width * height * 4;
		data.pixels.resize(dataSize);

		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				bool isWhite = ((x / checkSize) + (y / checkSize)) % 2 == 0;
				uint8_t color = isWhite ? 255 : 0;

				uint32_t idx = (y * width + x) * 4;
				data.pixels[idx + 0] = color;
				data.pixels[idx + 1] = color;
				data.pixels[idx + 2] = color;
				data.pixels[idx + 3] = 255;
			}
		}

		return data;
	}
}