#pragma once
#include <RenderTypes.h>

namespace Moon
{
	VkCommandPoolCreateInfo commandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0);

	VkCommandBufferAllocateInfo commandBufferAllocateInfo(VkCommandPool pool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask);

	VkSemaphoreSubmitInfo semaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);

	VkCommandBufferSubmitInfo commandBufferSubmitInfo(VkCommandBuffer cmd);

	VkCommandBufferBeginInfo commandBufferBeginInfo(VkCommandBufferUsageFlagBits flag);

	VkSubmitInfo2 submitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo);

	VkImageCreateInfo imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);

	VkImageViewCreateInfo imageviewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);

	void copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent3D imageSize);

	VkSamplerCreateInfo samplerCreateInfo(VkFilter filters, VkSamplerAddressMode samplerAdressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

	VkWriteDescriptorSet writeDescriptorBuffer(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding);

	VkWriteDescriptorSet writeDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorImageInfo* imageInfo, uint32_t binding);

	VkRenderingAttachmentInfo attachmentInfo(VkImageView view, VkClearValue* clear, VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderingInfo(VkExtent2D windowExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthStencilAttachment);
}

