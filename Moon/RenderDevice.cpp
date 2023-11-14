#include "RenderDevice.h"

#include <iostream>
#include <fstream>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

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
		m_window = SDL_CreateWindow(
			"Vulkan Engine",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			m_windowExtent.width,
			m_windowExtent.height,
			window_flags
		);

		initVulkan();
		initSwapchain();
		initCommands();
		initSyncStructures();
		initDescriptors();
		initPipelines();
		initRayTracing();
		initImgui();

		//Temp
		m_gradientPipelinePushConstant.data1 = glm::vec4(1.0, 0.0, 0.0, 0.0);
		m_gradientPipelinePushConstant.data2 = glm::vec4(0.0, 0.0, 1.0, 0.0);

		//everything went fine
		m_isInitialized = true;
	}
	void RenderDevice::cleanup()
	{
		if (m_isInitialized)
		{
			vkDeviceWaitIdle(m_device);
			m_mainDeletionQueue.flush();

			//destroy swapchain resources
			vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
			for (int i = 0; i < m_swapchainImageViews.size(); i++) {

				vkDestroyImageView(m_device, m_swapchainImageViews[i], nullptr);
			}

			vkDestroyDevice(m_device, nullptr);
			vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
			vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);
			vkDestroyInstance(m_instance, nullptr);

			SDL_DestroyWindow(m_window);
		}
	}

	void RenderDevice::draw()
	{
		FrameData& frame = getCurrentFrame();
		VK_CHECK(vkWaitForFences(m_device, 1, &frame.renderFence, true, 1000000000));
		VK_CHECK(vkResetFences(m_device, 1, &frame.renderFence));
		frame.deletionQueue.flush();

		uint32_t swapchainImageIndex;
		VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, 1000000000, frame.presentSemaphore, nullptr, &swapchainImageIndex));

		VK_CHECK(vkResetCommandBuffer(frame.mainCommandBuffer, 0));

		VkCommandBuffer cmd = frame.mainCommandBuffer;
		VkCommandBufferBeginInfo cmdBeginInfo = Moon::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
		{
			transitionImage(cmd, m_drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

			drawMain(cmd);

			transitionImage(cmd, m_drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			transitionImage(cmd, m_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkExtent3D extent;
			extent.height = m_windowExtent.height;
			extent.width = m_windowExtent.width;
			extent.depth = 1;
			copyImageToImage(cmd, m_drawImage.image, m_swapchainImages[swapchainImageIndex], extent);
			transitionImage(cmd, m_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			drawImgui(cmd, m_swapchainImageViews[swapchainImageIndex]);
			transitionImage(cmd, m_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		}
		VK_CHECK(vkEndCommandBuffer(cmd));

		VkCommandBufferSubmitInfo cmdinfo = commandBufferSubmitInfo(cmd);
		VkSemaphoreSubmitInfo waitInfo = semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, frame.presentSemaphore);
		VkSemaphoreSubmitInfo signalInfo = semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, frame.renderSemaphore);
		VkSubmitInfo2 submit = submitInfo(&cmdinfo, &signalInfo, &waitInfo);
		VK_CHECK(vkQueueSubmit2(m_graphicsQueue, 1, &submit, frame.renderFence));

		VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.pSwapchains = &m_swapchain;
		presentInfo.swapchainCount = 1;
		presentInfo.pWaitSemaphores = &frame.renderSemaphore;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pImageIndices = &swapchainImageIndex;
		VK_CHECK(vkQueuePresentKHR(m_graphicsQueue, &presentInfo));

		m_frameNumber++;
	}

	void RenderDevice::drawMain(VkCommandBuffer cmd)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_gradientPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_gradientPipelineLayout, 0, 1, &m_drawImageDescriptorSet, 0, nullptr);	
		vkCmdPushConstants(cmd, m_gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &m_gradientPipelinePushConstant);
		vkCmdDispatch(cmd, (uint32_t)std::ceil(m_windowExtent.width / 16.0), (uint32_t)std::ceil(m_windowExtent.height / 16.0), 1);
	}

	void RenderDevice::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView)
	{
		VkRenderingAttachmentInfo colorAttachment = Moon::attachmentInfo(targetImageView, nullptr, VK_IMAGE_LAYOUT_GENERAL);
		VkRenderingInfo renderInfo = Moon::renderingInfo(m_windowExtent, &colorAttachment, nullptr);

		vkCmdBeginRendering(cmd, &renderInfo);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
		vkCmdEndRendering(cmd);
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

				//send SDL event to imgui for handling
				ImGui_ImplSDL2_ProcessEvent(&e);
			}

			// imgui new frame
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplSDL2_NewFrame(m_window);
			ImGui::NewFrame();

			//some imgui UI to test
			{
				if(!ImGui::Begin("ColorGradient"))
				{
					ImGui::End();
				}
				else
				{
					ImGui::Text("Top Color:");
					ImGui::SameLine();
					ImGui::ColorEdit4("TopColor", (float*)&m_gradientPipelinePushConstant.data1, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

					ImGui::Text("Bottom Color:");
					ImGui::SameLine();
					ImGui::ColorEdit4("BottomColor", (float*)&m_gradientPipelinePushConstant.data2, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

					ImGui::End();
				}
			}

			//make imgui calculate internal draw structures
			ImGui::Render();

			draw();
		}
	}

	void RenderDevice::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
	{
		VK_CHECK(vkResetFences(m_device, 1, &m_immFence));
		VK_CHECK(vkResetCommandBuffer(m_immCommandBuffer, 0));

		VkCommandBuffer cmd = m_immCommandBuffer;
		VkCommandBufferBeginInfo cmdBeginInfo = Moon::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
		function(cmd);
		VK_CHECK(vkEndCommandBuffer(cmd));

		VkCommandBufferSubmitInfo cmdinfo = Moon::commandBufferSubmitInfo(cmd);
		VkSubmitInfo2 submit = Moon::submitInfo(&cmdinfo, nullptr, nullptr);
		VK_CHECK(vkQueueSubmit2(m_graphicsQueue, 1, &submit, m_immFence));
		VK_CHECK(vkWaitForFences(m_device, 1, &m_immFence, true, 9999999999));
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
		m_instance = vkb_inst.instance;
		m_debugMessenger = vkb_inst.debug_messenger;

		SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface);

		VkPhysicalDeviceVulkan13Features features{};
		features.dynamicRendering = true;
		features.synchronization2 = true;

		vkb::PhysicalDeviceSelector selector{ vkb_inst };
		vkb::PhysicalDevice physicalDevice = selector
			.set_minimum_version(1, 3)
			.set_required_features_13(features)
			.set_surface(m_surface)
			.select()
			.value();

		vkb::DeviceBuilder deviceBuilder{ physicalDevice };
		vkb::Device vkbDevice = deviceBuilder
			.build()
			.value();
		m_device = vkbDevice.device;
		m_physicalDevice = physicalDevice.physical_device;

		m_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
		m_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

		vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(m_device, "vkCmdBeginRenderingKHR"));
		vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(m_device, "vkCmdEndRenderingKHR"));

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = m_physicalDevice;
		allocatorInfo.device = m_device;
		allocatorInfo.instance = m_instance;
		vmaCreateAllocator(&allocatorInfo, &m_allocator);
		m_mainDeletionQueue.push_function([&]() 
			{
				vmaDestroyAllocator(m_allocator);
			});
	}

	void RenderDevice::initSwapchain()
	{
		VkSurfaceFormatKHR desiredFormat{ VK_FORMAT_B8G8R8A8_UNORM , VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

		vkb::SwapchainBuilder swapchainBuilder{ m_physicalDevice,m_device,m_surface };
		vkb::Swapchain vkbSwapchain = swapchainBuilder
			.set_desired_format(desiredFormat)
			.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
			.set_desired_extent(m_windowExtent.width, m_windowExtent.height)
			.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.build()
			.value();

		m_swapchain = vkbSwapchain.swapchain;
		m_swapchainImages = vkbSwapchain.get_images().value();
		m_swapchainImageViews = vkbSwapchain.get_image_views().value();
		m_swapchainImageFormat = vkbSwapchain.image_format;

		VkExtent3D drawImageExtent = { m_windowExtent.width, m_windowExtent.height, 1 };
		m_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

		VkImageUsageFlags drawImageUsages{};
		drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
		VkImageCreateInfo rimg_info = imageCreateInfo(m_drawImage.imageFormat, drawImageUsages, drawImageExtent);

		VmaAllocationCreateInfo rimg_allocinfo = {};
		rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vmaCreateImage(m_allocator, &rimg_info, &rimg_allocinfo, &m_drawImage.image, &m_drawImage.allocation, nullptr);

		VkImageViewCreateInfo rview_info = imageviewCreateInfo(m_drawImage.imageFormat, m_drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(m_device, &rview_info, nullptr, &m_drawImage.imageView));

		m_mainDeletionQueue.push_function([=]()
			{
				vkDestroyImageView(m_device, m_drawImage.imageView, nullptr);
				vmaDestroyImage(m_allocator, m_drawImage.image, m_drawImage.allocation);
			});
	}

	void RenderDevice::initCommands()
	{
		VkCommandPoolCreateInfo commandPoolInfo = Moon::commandPoolCreateInfo(m_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			VK_CHECK(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_frames[i].commandPool));

			VkCommandBufferAllocateInfo cmdAllocInfo = Moon::commandBufferAllocateInfo(m_frames[i].commandPool, 1);
			VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_frames[i].mainCommandBuffer));

			m_mainDeletionQueue.push_function([=]() 
				{
					vkDestroyCommandPool(m_device, m_frames[i].commandPool, nullptr);
				});
		}

		// Immediate submit related
		VK_CHECK(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_immCommandPool));
		VkCommandBufferAllocateInfo cmdAllocInfo = Moon::commandBufferAllocateInfo(m_immCommandPool, 1);
		VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_immCommandBuffer));
		m_mainDeletionQueue.push_function([=]() 
			{
				vkDestroyCommandPool(m_device, m_immCommandPool, nullptr);
			});
	}

	void RenderDevice::initSyncStructures()
	{
		VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_frames[i].renderFence));

			VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_frames[i].presentSemaphore));
			VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_frames[i].renderSemaphore));

			m_mainDeletionQueue.push_function([=]()
				{
					vkDestroySemaphore(m_device, m_frames[i].renderSemaphore, nullptr);
					vkDestroySemaphore(m_device, m_frames[i].presentSemaphore, nullptr);
					vkDestroyFence(m_device, m_frames[i].renderFence, nullptr);
				});
		}

		VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_immFence));
		m_mainDeletionQueue.push_function([=]() { vkDestroyFence(m_device, m_immFence, nullptr); });
	}

	void RenderDevice::initDescriptors()
	{
		std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
		};
		m_globalDescriptorAllocator.initPool(m_device, 10, sizes);
		m_mainDeletionQueue.push_function([=]()
			{
				m_globalDescriptorAllocator.clearDescriptors(m_device);
				m_globalDescriptorAllocator.destroyPool(m_device);
			});

		{
			DescriptorLayoutBuilder builder;
			builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
			m_drawImageDescriptorLayout = builder.build(m_device, VK_SHADER_STAGE_COMPUTE_BIT);
			m_drawImageDescriptorSet = m_globalDescriptorAllocator.allocate(m_device, m_drawImageDescriptorLayout);
			m_mainDeletionQueue.push_function([=]()
				{
					vkDestroyDescriptorSetLayout(m_device, m_drawImageDescriptorLayout, nullptr);
				});

			VkSampler blockySampler;
			VkSamplerCreateInfo samplerInfo = samplerCreateInfo(VK_FILTER_NEAREST);
			vkCreateSampler(m_device, &samplerInfo, nullptr, &blockySampler);
			m_mainDeletionQueue.push_function([=]()
				{
					vkDestroySampler(m_device, blockySampler, nullptr);
				});

			VkDescriptorImageInfo imageBufferInfo;
			imageBufferInfo.sampler = blockySampler;
			imageBufferInfo.imageView = m_drawImage.imageView;
			imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			VkWriteDescriptorSet texture1 = writeDescriptorImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_drawImageDescriptorSet, &imageBufferInfo, 0);
			vkUpdateDescriptorSets(m_device, 1, &texture1, 0, nullptr);
		}
	}

	void RenderDevice::initPipelines()
	{
		VkPushConstantRange pushConstantRange;
		pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(ComputePushConstants);

		VkPipelineLayoutCreateInfo computeLayout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		computeLayout.pSetLayouts = &m_drawImageDescriptorLayout;
		computeLayout.setLayoutCount = 1;
		computeLayout.pushConstantRangeCount = 1;
		computeLayout.pPushConstantRanges = &pushConstantRange;
		VK_CHECK(vkCreatePipelineLayout(m_device, &computeLayout, nullptr, &m_gradientPipelineLayout));

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
		computePipelineCreateInfo.layout = m_gradientPipelineLayout;
		computePipelineCreateInfo.stage = stageinfo;
		VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &m_gradientPipeline));

		vkDestroyShaderModule(m_device, computeDrawShader, nullptr);
		m_mainDeletionQueue.push_function([&]()
			{
				vkDestroyPipelineLayout(m_device, m_gradientPipelineLayout, nullptr);
				vkDestroyPipeline(m_device, m_gradientPipeline, nullptr);
			});
	}

	void RenderDevice::initRayTracing()
	{
		m_physicalDeviceProperties.pNext = &m_rtProperties;
		vkGetPhysicalDeviceProperties2(m_physicalDevice, &m_physicalDeviceProperties);
	}

	void RenderDevice::initImgui()
	{
		// 1: create descriptor pool for IMGUI
		//  the size of the pool is very oversize, but it's copied from imgui demo
		//  itself.
		VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000;
		pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		VkDescriptorPool imguiPool;
		VK_CHECK(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &imguiPool));

		// 2: initialize imgui library
		// this initializes the core structures of imgui
		ImGui::CreateContext();

		// this initializes imgui for SDL
		ImGui_ImplSDL2_InitForVulkan(m_window);

		// this initializes imgui for Vulkan
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = m_instance;
		init_info.PhysicalDevice = m_physicalDevice;
		init_info.Device = m_device;
		init_info.Queue = m_graphicsQueue;
		init_info.DescriptorPool = imguiPool;
		init_info.MinImageCount = 3;
		init_info.ImageCount = 3;
		init_info.UseDynamicRendering = true;
		init_info.ColorAttachmentFormat = m_swapchainImageFormat;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);

		// execute a gpu command to upload imgui font textures
		immediateSubmit([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });

		// clear font textures from cpu data
		ImGui_ImplVulkan_DestroyFontUploadObjects();
		// add the destroy the imgui created structures
		m_mainDeletionQueue.push_function([=]()
			{
				vkDestroyDescriptorPool(m_device, imguiPool, nullptr);
				ImGui_ImplVulkan_Shutdown();
			});
	}

	FrameData& RenderDevice::getCurrentFrame()
	{
		return m_frames[m_frameNumber % FRAME_OVERLAP];
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
		if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
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
