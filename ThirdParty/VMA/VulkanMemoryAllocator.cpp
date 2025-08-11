//------------------------------------------------------------------------------
// VulkanMemoryAllocatorImpl.cpp
//
// This file provides the implementation for VMA (Vulkan Memory Allocator)
// VMA is a header-only library that requires exactly ONE compilation unit
// to define VMA_IMPLEMENTATION before including the header.
//------------------------------------------------------------------------------

// Define the implementation macro BEFORE including the header
#define VMA_IMPLEMENTATION

// Include Vulkan headers first
#include <vulkan/vulkan.h>

// Include VMA header with implementation
#include "vk_mem_alloc.h"