#include "Descriptor.h"

namespace Moon
{
	void DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags shaderStages)
	{
		VkDescriptorSetLayoutBinding newbind{};
		newbind.binding = binding;
		newbind.descriptorCount = 1;
		newbind.descriptorType = type;
		newbind.stageFlags = shaderStages;
		bindings.push_back(newbind);
	}

	void DescriptorLayoutBuilder::clear()
	{
		bindings.clear();
	}

	VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device)
	{
		VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		info.pBindings = bindings.data();
		info.bindingCount = (uint32_t)bindings.size();
		info.flags = 0;

		VkDescriptorSetLayout set;
		VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));
		return set;
	}

	void DescriptorAllocator::initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
	{
		ratios.clear();

		for (auto r : poolRatios)
		{
			ratios.push_back(r);
		}

		VkDescriptorPool newPool = createPool(device, maxSets, poolRatios);
		setsPerPool = (uint32_t)(maxSets * 1.5);
		readyPools.push_back(newPool);
	}

	void DescriptorAllocator::clearDescriptors(VkDevice device)
	{
		for (auto p : readyPools)
		{
			vkResetDescriptorPool(device, p, 0);
		}
		for (auto p : fullPools)
		{
			vkResetDescriptorPool(device, p, 0);
			readyPools.push_back(p);
		}
		fullPools.clear();
	}

	void DescriptorAllocator::destroyPool(VkDevice device)
	{
		for (auto p : readyPools)
		{
			vkDestroyDescriptorPool(device, p, nullptr);
		}
		readyPools.clear();
		for (auto p : fullPools)
		{
			vkDestroyDescriptorPool(device, p, nullptr);
		}
		fullPools.clear();
	}

	VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
	{
		VkDescriptorPool poolToUse = getPool(device);

		VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.descriptorPool = poolToUse;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet ds;
		VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

		//allocation failed. Try again
		if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
		{
			fullPools.push_back(poolToUse);
			poolToUse = getPool(device);
			allocInfo.descriptorPool = poolToUse;
			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
		}

		readyPools.push_back(poolToUse);
		return ds;
	}

	VkDescriptorPool DescriptorAllocator::getPool(VkDevice device)
	{
		VkDescriptorPool newPool;
		if (readyPools.size() != 0)
		{
			newPool = readyPools.back();
			readyPools.pop_back();
		}
		else
		{
			//need to create a new pool
			newPool = createPool(device, setsPerPool, ratios);

			setsPerPool = (uint32_t)(setsPerPool * 1.5);
			if (setsPerPool > 4092)
			{
				setsPerPool = 4092;
			}
		}

		return newPool;
	}

	VkDescriptorPool DescriptorAllocator::createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios)
	{
		std::vector<VkDescriptorPoolSize> poolSizes;
		for (PoolSizeRatio ratio : poolRatios)
		{
			poolSizes.push_back(VkDescriptorPoolSize
				{
				.type = ratio.type,
				.descriptorCount = uint32_t(ratio.ratio * setCount)
				});
		}

		VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pool_info.maxSets = setCount;
		pool_info.poolSizeCount = (uint32_t)poolSizes.size();
		pool_info.pPoolSizes = poolSizes.data();

		VkDescriptorPool newPool;
		vkCreateDescriptorPool(device, &pool_info, nullptr, &newPool);
		return newPool;
	}

	void DescriptorWriter::writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type)
	{
		VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo{
				.sampler = sampler,
				.imageView = image,
				.imageLayout = layout
			});

		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstBinding = binding;
		write.dstSet = VK_NULL_HANDLE;
		write.descriptorCount = 1;
		write.descriptorType = type;
		write.pImageInfo = &info;

		writes.push_back(write);
	}

	void DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type)
	{
		VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
				.buffer = buffer,
				.offset = offset,
				.range = size
			});

		VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstBinding = binding;
		write.dstSet = VK_NULL_HANDLE;
		write.descriptorCount = 1;
		write.descriptorType = type;
		write.pBufferInfo = &info;

		writes.push_back(write);
	}

	void DescriptorWriter::clear()
	{
		imageInfos.clear();
		writes.clear();
		bufferInfos.clear();
	}

	void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set)
	{
		for (VkWriteDescriptorSet& write : writes)
			write.dstSet = set;
		vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
	}
}