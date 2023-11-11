#pragma once
#include "RenderTypes.h"

namespace Moon
{
	void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
}