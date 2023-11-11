#include "RenderDevice.h"

#include <iostream>
#include <fstream>

#include <SDL.h>
#include <SDL_vulkan.h>

#include "RenderTypes.h"
#include "RenderUtilities.h"

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

using namespace std;
#define VK_CHECK(x)                                                    \
	do                                                                 \
	{                                                                  \
		VkResult err = x;                                              \
		if (err)                                                       \
		{                                                              \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                   \
		}                                                              \
	} while (0)

namespace Moon
{
	void RenderDevice::init()
	{
		// Initialize SDL 
		SDL_Init(SDL_INIT_VIDEO);
		SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
		_window = SDL_CreateWindow(
			"Vulkan Engine",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			_windowExtent.width,
			_windowExtent.height,
			window_flags
		);

		initVulkan();
		initSwapchain();
		initCommands();
		initSyncStructures();
		initDescriptors();
		initPipelines();

		//everything went fine
		_isInitialized = true;
	}
	void RenderDevice::cleanup()
	{
		if (_isInitialized)
		{
			vkDeviceWaitIdle(_device);
			_mainDeletionQueue.flush();

			//destroy swapchain resources
			vkDestroySwapchainKHR(_device, _swapchain, nullptr);
			for (int i = 0; i < _swapchainImageViews.size(); i++) {

				vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			}

			vkDestroyDevice(_device, nullptr);
			vkDestroySurfaceKHR(_instance, _surface, nullptr);
			vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
			vkDestroyInstance(_instance, nullptr);

			SDL_DestroyWindow(_window);
		}
	}

	void RenderDevice::draw()
	{
		FrameData& frame = getCurrentFrame();
		VK_CHECK(vkWaitForFences(_device, 1, &frame.renderFence, true, 1000000000));
		VK_CHECK(vkResetFences(_device, 1, &frame.renderFence));
		frame.deletionQueue.flush();

		uint32_t swapchainImageIndex;
		VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, frame.presentSemaphore, nullptr, &swapchainImageIndex));

		VK_CHECK(vkResetCommandBuffer(frame.mainCommandBuffer, 0));

		VkCommandBuffer cmd = frame.mainCommandBuffer;
		VkCommandBufferBeginInfo cmdBeginInfo = {};
		cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
		{
			transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

			drawMain(cmd);

			transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkExtent3D extent;
			extent.height = _windowExtent.height;
			extent.width = _windowExtent.width;
			extent.depth = 1;
			copyImageToImage(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], extent);

			transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		}
		VK_CHECK(vkEndCommandBuffer(cmd));

		VkCommandBufferSubmitInfo cmdinfo = commandBufferSubmitInfo(cmd);
		VkSemaphoreSubmitInfo waitInfo = semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, frame.presentSemaphore);
		VkSemaphoreSubmitInfo signalInfo = semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, frame.renderSemaphore);
		VkSubmitInfo2 submit = submitInfo(&cmdinfo, &signalInfo, &waitInfo);
		VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, frame.renderFence));

		VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.pSwapchains = &_swapchain;
		presentInfo.swapchainCount = 1;
		presentInfo.pWaitSemaphores = &frame.renderSemaphore;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pImageIndices = &swapchainImageIndex;
		VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

		_frameNumber++;
	}

	void RenderDevice::drawMain(VkCommandBuffer cmd)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptorSet, 0, nullptr);
		vkCmdDispatch(cmd, (uint32_t)std::ceil(_windowExtent.width / 16.0), (uint32_t)std::ceil(_windowExtent.height / 16.0), 1);
	}

	void RenderDevice::run()
	{
		SDL_Event e;
		bool bQuit = false;

		//main loop
		while (!bQuit)
		{
			//Handle events on queue
			while (SDL_PollEvent(&e) != 0)
			{		
				if (e.type == SDL_QUIT) bQuit = true;
				if (e.key.keysym.sym == SDLK_ESCAPE && e.key.state == SDL_PRESSED) bQuit = true;
			}

			draw();
		}
	}

	void RenderDevice::initVulkan()
	{
		vkb::InstanceBuilder builder;
		auto inst_ret = builder.set_app_name("Moon Engine")
			.request_validation_layers(true)
			.require_api_version(1, 3, 0)
			.use_default_debug_messenger()
			.build();

		vkb::Instance vkb_inst = inst_ret.value();
		_instance = vkb_inst.instance;
		_debug_messenger = vkb_inst.debug_messenger;

		SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

		VkPhysicalDeviceVulkan13Features features{};
		features.dynamicRendering = true;
		features.synchronization2 = true;

		vkb::PhysicalDeviceSelector selector{ vkb_inst };
		vkb::PhysicalDevice physicalDevice = selector
			.set_minimum_version(1, 3)
			.set_required_features_13(features)
			.set_surface(_surface)
			.select()
			.value();

		vkb::DeviceBuilder deviceBuilder{ physicalDevice };
		vkb::Device vkbDevice = deviceBuilder
			.build()
			.value();
		_device = vkbDevice.device;
		_chosenGPU = physicalDevice.physical_device;

		_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
		_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

		vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(_device, "vkCmdBeginRenderingKHR"));
		vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(_device, "vkCmdEndRenderingKHR"));

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = _chosenGPU;
		allocatorInfo.device = _device;
		allocatorInfo.instance = _instance;
		vmaCreateAllocator(&allocatorInfo, &_allocator);
		_mainDeletionQueue.push_function([&]() 
			{
				vmaDestroyAllocator(_allocator);
			});
	}

	void RenderDevice::initSwapchain()
	{
		VkSurfaceFormatKHR desiredFormat{ VK_FORMAT_B8G8R8A8_UNORM , VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

		vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };
		vkb::Swapchain vkbSwapchain = swapchainBuilder
			.set_desired_format(desiredFormat)
			.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
			.set_desired_extent(_windowExtent.width, _windowExtent.height)
			.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.build()
			.value();

		_swapchain = vkbSwapchain.swapchain;
		_swapchainImages = vkbSwapchain.get_images().value();
		_swapchainImageViews = vkbSwapchain.get_image_views().value();
		_swapchainImageFormat = vkbSwapchain.image_format;

		VkExtent3D drawImageExtent = { _windowExtent.width, _windowExtent.height, 1 };
		_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

		VkImageUsageFlags drawImageUsages{};
		drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
		VkImageCreateInfo rimg_info = imageCreateInfo(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

		VmaAllocationCreateInfo rimg_allocinfo = {};
		rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

		VkImageViewCreateInfo rview_info = imageviewCreateInfo(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

		_mainDeletionQueue.push_function([=]()
			{
				vkDestroyImageView(_device, _drawImage.imageView, nullptr);
				vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
			});
	}

	void RenderDevice::initCommands()
	{
		VkCommandPoolCreateInfo commandPoolInfo = Moon::commandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i].commandPool));

			VkCommandBufferAllocateInfo cmdAllocInfo = Moon::commandBufferAllocateInfo(_frames[i].commandPool, 1);
			VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));

			_mainDeletionQueue.push_function([=]() 
				{
					vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
				});
		}
	}

	void RenderDevice::initSyncStructures()
	{
		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));

			VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].presentSemaphore));
			VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].renderSemaphore));

			_mainDeletionQueue.push_function([=]()
				{
					vkDestroySemaphore(_device, _frames[i].renderSemaphore, nullptr);
					vkDestroySemaphore(_device, _frames[i].presentSemaphore, nullptr);
					vkDestroyFence(_device, _frames[i].renderFence, nullptr);
				});
		}
	}

	void RenderDevice::initDescriptors()
	{
		std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
		};
		globalDescriptorAllocator.initPool(_device, 10, sizes);
		_mainDeletionQueue.push_function([=]()
			{
				globalDescriptorAllocator.clearDescriptors(_device);
				globalDescriptorAllocator.destroyPool(_device);
			});

		{
			DescriptorLayoutBuilder builder;
			builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
			_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
			_drawImageDescriptorSet = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);
			_mainDeletionQueue.push_function([=]()
				{
					vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
				});

			VkSampler blockySampler;
			VkSamplerCreateInfo samplerInfo = samplerCreateInfo(VK_FILTER_NEAREST);
			vkCreateSampler(_device, &samplerInfo, nullptr, &blockySampler);
			_mainDeletionQueue.push_function([=]()
				{
					vkDestroySampler(_device, blockySampler, nullptr);
				});

			VkDescriptorImageInfo imageBufferInfo;
			imageBufferInfo.sampler = blockySampler;
			imageBufferInfo.imageView = _drawImage.imageView;
			imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			VkWriteDescriptorSet texture1 = writeDescriptorImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _drawImageDescriptorSet, &imageBufferInfo, 0);
			vkUpdateDescriptorSets(_device, 1, &texture1, 0, nullptr);
		}
	}

	void RenderDevice::initPipelines()
	{
		VkPipelineLayoutCreateInfo computeLayout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
		computeLayout.setLayoutCount = 1;
		VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

		VkShaderModule computeDrawShader;
		if (!loadShaderModule("../../Shaders/colorGradient.comp.spv", &computeDrawShader))
		{
			std::cout << "Error when building the compute shader" << std::endl;
		}

		VkPipelineShaderStageCreateInfo stageinfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageinfo.module = computeDrawShader;
		stageinfo.pName = "main";

		VkComputePipelineCreateInfo computePipelineCreateInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		computePipelineCreateInfo.layout = _gradientPipelineLayout;
		computePipelineCreateInfo.stage = stageinfo;
		VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_gradientPipeline));

		vkDestroyShaderModule(_device, computeDrawShader, nullptr);
		_mainDeletionQueue.push_function([&]()
			{
				vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
				vkDestroyPipeline(_device, _gradientPipeline, nullptr);
			});
	}

	FrameData& RenderDevice::getCurrentFrame()
	{
		return _frames[_frameNumber % FRAME_OVERLAP];
	}

	bool RenderDevice::loadShaderModule(const char* filePath, VkShaderModule* outShaderModule)
	{
		std::ifstream file(filePath, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			return false;
		}

		size_t fileSize = (size_t)file.tellg();
		std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
		file.seekg(0);
		file.read((char*)buffer.data(), fileSize);
		file.close();

		VkShaderModule shaderModule;
		VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		createInfo.codeSize = buffer.size() * sizeof(uint32_t);
		createInfo.pCode = buffer.data();
		if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			return false;
		}
		*outShaderModule = shaderModule;
		return true;
	}

	void DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type)
	{
		VkDescriptorSetLayoutBinding newbind{};
		newbind.binding = binding;
		newbind.descriptorCount = 1;
		newbind.descriptorType = type;
		bindings.push_back(newbind);
	}

	void DescriptorLayoutBuilder::clear()
	{
		bindings.clear();
	}

	VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages)
	{
		for (auto& b : bindings)
		{
			b.stageFlags |= shaderStages;
		}

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
		std::vector<VkDescriptorPoolSize> poolSizes;
		for (PoolSizeRatio ratio : poolRatios)
		{
			poolSizes.push_back(VkDescriptorPoolSize{
				.type = ratio.type,
				.descriptorCount = uint32_t(ratio.ratio * maxSets)
				});
		}

		VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pool_info.maxSets = maxSets;
		pool_info.poolSizeCount = (uint32_t)poolSizes.size();
		pool_info.pPoolSizes = poolSizes.data();
		vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
	}

	void DescriptorAllocator::clearDescriptors(VkDevice device)
	{
		vkResetDescriptorPool(device, pool, 0);
	}

	void DescriptorAllocator::destroyPool(VkDevice device)
	{
		vkDestroyDescriptorPool(device, pool, nullptr);
	}

	VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
	{
		VkDescriptorSet descriptorSet;
		VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.descriptorPool = pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;
		vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
		return descriptorSet;
	}
}
