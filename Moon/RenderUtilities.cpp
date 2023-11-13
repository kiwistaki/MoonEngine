#include "RenderUtilities.h"

VkCommandPoolCreateInfo Moon::commandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags /*= 0*/)
{
	VkCommandPoolCreateInfo info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	info.queueFamilyIndex = queueFamilyIndex;
	info.flags = flags;
	return info;
}

VkCommandBufferAllocateInfo Moon::commandBufferAllocateInfo(VkCommandPool pool, uint32_t count /*= 1*/, VkCommandBufferLevel level /*= VK_COMMAND_BUFFER_LEVEL_PRIMARY*/)
{
	VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	info.commandPool = pool;
	info.commandBufferCount = count;
	info.level = level;
	return info;
}

VkImageSubresourceRange Moon::imageSubresourceRange(VkImageAspectFlags aspectMask)
{
	VkImageSubresourceRange subImage{};
	subImage.aspectMask = aspectMask;
	subImage.baseMipLevel = 0;
	subImage.levelCount = 1;
	subImage.baseArrayLayer = 0;
	subImage.layerCount = 1;
	return subImage;
}

VkSemaphoreSubmitInfo Moon::semaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore)
{
	VkSemaphoreSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	submitInfo.semaphore = semaphore;
	submitInfo.stageMask = stageMask;
	submitInfo.deviceIndex = 0;
	submitInfo.value = 1;
	return submitInfo;
}

VkCommandBufferSubmitInfo Moon::commandBufferSubmitInfo(VkCommandBuffer cmd)
{
	VkCommandBufferSubmitInfo info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	info.commandBuffer = cmd;
	info.deviceMask = 0;
	return info;
}

VkCommandBufferBeginInfo Moon::commandBufferBeginInfo(VkCommandBufferUsageFlagBits flag)
{
	VkCommandBufferBeginInfo info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	info.flags = flag;
	return info;
}

VkSubmitInfo2 Moon::submitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo)
{
	VkSubmitInfo2 info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	info.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
	info.pWaitSemaphoreInfos = waitSemaphoreInfo;
	info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
	info.pSignalSemaphoreInfos = signalSemaphoreInfo;
	info.commandBufferInfoCount = 1;
	info.pCommandBufferInfos = cmd;
	return info;
}

VkImageCreateInfo Moon::imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent)
{
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent = extent;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usageFlags;
	return info;
}

VkImageViewCreateInfo Moon::imageviewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags)
{
	VkImageViewCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.image = image;
	info.format = format;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	info.subresourceRange.aspectMask = aspectFlags;
	return info;
}

void Moon::copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent3D imageSize)
{
	VkImageBlit2 blitRegion{ VK_STRUCTURE_TYPE_IMAGE_BLIT_2 };
	blitRegion.srcOffsets[1].x = imageSize.width;
	blitRegion.srcOffsets[1].y = imageSize.height;
	blitRegion.srcOffsets[1].z = 1;
	blitRegion.dstOffsets[1].x = imageSize.width;
	blitRegion.dstOffsets[1].y = imageSize.height;
	blitRegion.dstOffsets[1].z = 1;
	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;
	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{ VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2 };
	blitInfo.dstImage = destination;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = source;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_NEAREST;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;
	vkCmdBlitImage2(cmd, &blitInfo);
}

VkSamplerCreateInfo Moon::samplerCreateInfo(VkFilter filters, VkSamplerAddressMode samplerAdressMode)
{
	VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	info.magFilter = filters;
	info.minFilter = filters;
	info.addressModeU = samplerAdressMode;
	info.addressModeV = samplerAdressMode;
	info.addressModeW = samplerAdressMode;
	return info;
}

VkWriteDescriptorSet Moon::writeDescriptorBuffer(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding)
{
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pBufferInfo = bufferInfo;
	return write;
}

VkWriteDescriptorSet Moon::writeDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorImageInfo* imageInfo, uint32_t binding)
{
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = imageInfo;
	return write;
}

VkRenderingAttachmentInfo Moon::attachmentInfo(VkImageView view, VkClearValue* clear, VkImageLayout layout)
{
	VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	colorAttachment.imageView = view;
	colorAttachment.imageLayout = layout;
	colorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	if (clear)
	{
		colorAttachment.clearValue = *clear;
	}
	return colorAttachment;
}

VkRenderingInfo Moon::renderingInfo(VkExtent2D windowExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthStencilAttachment)
{
	VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	renderingInfo.renderArea = { 0, 0, windowExtent.width, windowExtent.height };
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = colorAttachment;
	renderingInfo.pDepthAttachment = depthStencilAttachment;
	renderingInfo.pStencilAttachment = depthStencilAttachment;
	return renderingInfo;
}
