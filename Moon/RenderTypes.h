#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Moon
{
    struct AllocatedBuffer
    {
        VkBuffer buffer;
        VmaAllocation allocation;
    };

    struct AllocatedImage
    {
        VkImage image;
        VkImageView imageView;
        VkFormat imageFormat;
        VmaAllocation allocation;
    };
}