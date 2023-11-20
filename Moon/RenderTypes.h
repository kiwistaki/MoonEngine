#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <Volk/volk.h>

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