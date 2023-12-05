#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <Volk/volk.h>

#include <vk_mem_alloc.h>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Moon
{
    struct AllocatedBuffer
    {
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo info;
    };

    struct AllocatedImage
    {
        VkImage image;
        VkImageView imageView;
        VkExtent3D imageExtent;
        VkFormat imageFormat;
        VmaAllocation allocation;
    };
}