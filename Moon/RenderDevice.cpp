#include "RenderDevice.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <chrono>

#define VK_USE_PLATFORM_WIN32_KHR
#define VOLK_IMPLEMENTATION
#include <volk/volk.h>

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

#include <glm/gtx/transform.hpp>

static bool g_useValidationLayer = true;

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
		initDefaultData();

		m_mainCamera.velocity = glm::vec3(0.f);
		m_mainCamera.position = glm::vec3(30.f, 0.f, -85.f);
		m_mainCamera.pitch = 0;
		m_mainCamera.yaw = 0;

		//everything went fine
		m_isInitialized = true;
	}

	void RenderDevice::cleanup()
	{
		if (m_isInitialized)
		{
			vkDeviceWaitIdle(m_device);

			m_loadedScenes.clear();

			for (FrameData frameData : m_frames)
			{
				frameData.deletionQueue.flush();
			}

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
		updateScene();

		FrameData& frame = getCurrentFrame();
		VK_CHECK(vkWaitForFences(m_device, 1, &frame.renderFence, true, 1000000000));
		VK_CHECK(vkResetFences(m_device, 1, &frame.renderFence));
		
		frame.deletionQueue.flush();
		frame.frameDescriptors.clearDescriptors(m_device);

		uint32_t swapchainImageIndex;
		VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, 1000000000, frame.presentSemaphore, nullptr, &swapchainImageIndex));

		VK_CHECK(vkResetCommandBuffer(frame.mainCommandBuffer, 0));

		VkCommandBuffer cmd = frame.mainCommandBuffer;
		VkCommandBufferBeginInfo cmdBeginInfo = Moon::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
		{
			transitionImage(cmd, m_drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			drawImpl(cmd);

			transitionImage(cmd, m_drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
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

	void RenderDevice::drawImpl(VkCommandBuffer cmd)
	{
		transitionImage(cmd, m_depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		// Draw Mesh
		drawMeshes(cmd);
	}

	void RenderDevice::drawMeshes(VkCommandBuffer cmd)
	{
		//reset counters
		m_stats.drawcallCount = 0;
		m_stats.triangleCount = 0;
		//begin clock
		auto start = std::chrono::system_clock::now();

		//sort opaque draw objects per pipeline, access only by index
		std::vector<uint32_t> opaqueDraws;
		opaqueDraws.reserve(m_mainDrawContext.OpaqueSurfaces.size());
		for (uint32_t i = 0; i < m_mainDrawContext.OpaqueSurfaces.size(); i++)
			opaqueDraws.push_back(i);
		std::sort(opaqueDraws.begin(), opaqueDraws.end(), [&](const auto& iA, const auto& iB) {
			const RenderObject& A = m_mainDrawContext.OpaqueSurfaces[iA];
			const RenderObject& B = m_mainDrawContext.OpaqueSurfaces[iB];
			if (A.material == B.material)
			{
				return A.indexBuffer < B.indexBuffer;
			}
			else
			{
				return A.material < B.material;
			}
		});

		VkClearValue clearValue{ .color = VkClearColorValue {0.1f, 0.1f, 0.1f, 1.0f} };
		VkRenderingAttachmentInfo colorAttachment = Moon::attachmentInfo(m_drawImage.imageView, &clearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);//VK_IMAGE_LAYOUT_GENERAL?
		VkRenderingAttachmentInfo depthAttachment = Moon::depthAttachmentInfo(m_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
		VkRenderingInfo renderingInfo = Moon::renderingInfo(m_windowExtent, &colorAttachment, &depthAttachment);

		vkCmdBeginRendering(cmd, &renderingInfo);
		{
			VkViewport viewport = {};
			viewport.x = 0;
			viewport.y = 0;
			viewport.width = static_cast<float>(m_windowExtent.width);
			viewport.height = static_cast<float>(m_windowExtent.height);
			viewport.minDepth = 0.f;
			viewport.maxDepth = 1.f;
			vkCmdSetViewport(cmd, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.offset = VkOffset2D{ 0,0 };
			scissor.extent = m_windowExtent;
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			AllocatedBuffer gpuSceneDataBuffer = createBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
			getCurrentFrame().deletionQueue.pushFunction([=, this]()
				{
					destroyBuffer(gpuSceneDataBuffer);
				});


			GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
			*sceneUniformData = m_sceneData;

			VkDescriptorSet globalDescriptor = getCurrentFrame().frameDescriptors.allocate(m_device, m_gpuSceneDataDescriptorLayout);
			DescriptorWriter writer;
			writer.writeBuffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
			writer.updateSet(m_device, globalDescriptor);

			MaterialPipeline* lastPipeline = nullptr;
			MaterialInstance* lastMaterial = nullptr;
			VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

			auto draw = [&](const RenderObject& draw)
				{
					if (lastMaterial != draw.material)
					{
						lastMaterial = draw.material;

						if (lastPipeline != draw.material->pipeline)
						{
							lastPipeline = draw.material->pipeline;
							vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipeline);
							vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->layout, 0, 1, &globalDescriptor, 0, nullptr);
						}

						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->layout, 1, 1, &draw.material->materialSet, 0, nullptr);
					}

					if (lastIndexBuffer != draw.indexBuffer)
					{
						lastIndexBuffer = draw.indexBuffer;
						vkCmdBindIndexBuffer(cmd, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
					}

					GPUDrawPushConstants pushConstants;
					pushConstants.vertexBuffer = draw.vertexBufferAddress;
					pushConstants.worldMatrix = draw.transform;
					vkCmdPushConstants(cmd, draw.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

					vkCmdDrawIndexed(cmd, draw.indexCount, 1, draw.firstIndex, 0, 0);

					m_stats.drawcallCount++;
					m_stats.triangleCount += draw.indexCount / 3;
				};

			for (auto& r : opaqueDraws)
			{
				draw(m_mainDrawContext.OpaqueSurfaces[r]);
			}

			for (auto& r : m_mainDrawContext.TransparentSurfaces)
			{
				draw(r);
			}
		}
		vkCmdEndRendering(cmd);

		auto end = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		m_stats.meshDrawTime = elapsed.count() / 1000.f;
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
			//begin clock
			auto start = std::chrono::system_clock::now();

			//Handle events on queue
			while (SDL_PollEvent(&e) != 0)
			{		
				if (e.type == SDL_QUIT) bQuit = true;
				if (e.key.keysym.sym == SDLK_ESCAPE && e.key.state == SDL_PRESSED) bQuit = true;

				m_mainCamera.processSDLEvent(e);

				//send SDL event to imgui for handling
				ImGui_ImplSDL2_ProcessEvent(&e);
			}

			// imgui new frame
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplSDL2_NewFrame(m_window);
			ImGui::NewFrame();

			//some imgui UI to test
			{
				if(!ImGui::Begin("Stats"))
				{
					ImGui::End();
				}
				else
				{
					ImGui::Text("Frametime: %f ms", m_stats.frametime);
					ImGui::Text("Draw time: %f ms", m_stats.meshDrawTime);
					ImGui::Text("Update time: %f ms", m_stats.sceneUpdateTime);
					ImGui::Text("Triangles: %i", m_stats.triangleCount);
					ImGui::Text("Draws: %i", m_stats.drawcallCount);

					ImGui::End();
				}
			}

			//make imgui calculate internal draw structures
			ImGui::Render();

			draw();

			//end clock
			auto end = std::chrono::system_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
			m_stats.frametime = elapsed.count() / 1000.0f;
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

	AllocatedBuffer RenderDevice::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
	{
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = allocSize;
		bufferInfo.usage = usage;

		VmaAllocationCreateInfo vmaallocInfo = {};
		vmaallocInfo.usage = memoryUsage;
		vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		AllocatedBuffer newBuffer;
		VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
		return newBuffer;
	}

	AllocatedImage RenderDevice::createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage)
	{
		AllocatedImage newImage;
		newImage.imageFormat = format;
		newImage.imageExtent = size;
		VkImageCreateInfo img_info = imageCreateInfo(format, usage, size);

		VmaAllocationCreateInfo allocinfo = {};
		allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK(vmaCreateImage(m_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

		VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
		if (format == VK_FORMAT_D32_SFLOAT)
		{
			aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
		}

		VkImageViewCreateInfo view_info = imageviewCreateInfo(format, newImage.image, aspectFlag);
		VK_CHECK(vkCreateImageView(m_device, &view_info, nullptr, &newImage.imageView));
		return newImage;
	}

	AllocatedImage RenderDevice::createImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage)
	{
		size_t data_size = size.depth * size.width * size.height * 4;
		AllocatedBuffer uploadBuffer = createBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(uploadBuffer.info.pMappedData, data, data_size);

		AllocatedImage new_image = createImage(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		immediateSubmit([&](VkCommandBuffer cmd)
		{
			transitionImage(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkBufferImageCopy copyRegion = {};
			copyRegion.bufferOffset = 0;
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageExtent = size;

			vkCmdCopyBufferToImage(cmd, uploadBuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
				&copyRegion);

			transitionImage(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

		destroyBuffer(uploadBuffer);

		return new_image;
	}

	void RenderDevice::updateScene()
	{
		//start clock
		auto start = std::chrono::system_clock::now();
		
		m_mainCamera.update();

		m_mainDrawContext.OpaqueSurfaces.clear();
		m_mainDrawContext.TransparentSurfaces.clear();
		m_loadedScenes["structure"]->Draw(glm::mat4{ 1.f }, m_mainDrawContext);

		glm::mat4 view = m_mainCamera.getViewMatrix();
		glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)m_windowExtent.width / (float)m_windowExtent.height, 10000.f, 0.1f);
		projection[1][1] *= -1;

		m_sceneData.view = view;
		m_sceneData.proj = projection;
		m_sceneData.viewproj = projection * view;

		m_sceneData.ambientColor = glm::vec4(.1f);
		m_sceneData.sunlightColor = glm::vec4(1.f);
		m_sceneData.sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);

		//end clock
		auto end = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		m_stats.sceneUpdateTime = elapsed.count() / 1000.0f;
	}

	void RenderDevice::initVulkan()
	{
		VK_CHECK(volkInitialize());

		vkb::InstanceBuilder builder;
		auto inst_ret = builder.set_app_name("Moon Engine")
			.request_validation_layers(g_useValidationLayer)
			.require_api_version(1, 3, 0)
			.use_default_debug_messenger()
			.build();

		vkb::Instance vkb_inst = inst_ret.value();
		m_instance = vkb_inst.instance;
		m_debugMessenger = vkb_inst.debug_messenger;

		volkLoadInstance(m_instance);

		SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface);

		VkPhysicalDeviceVulkan13Features features13{};
		features13.dynamicRendering = true;
		features13.synchronization2 = true;
		
		VkPhysicalDeviceVulkan12Features features12{};
		features12.bufferDeviceAddress = true;
		features12.descriptorIndexing = true;

		vkb::PhysicalDeviceSelector selector{ vkb_inst };
		vkb::PhysicalDevice physicalDevice = selector
			//.add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
			//.add_required_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
			//.add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
			//.add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
			.set_minimum_version(1, 3)
			.set_required_features_13(features13)
			.set_required_features_12(features12)
			.set_surface(m_surface)
			.select()
			.value();

		VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES };
		shaderDrawParametersFeatures.shaderDrawParameters = VK_TRUE;

		vkb::DeviceBuilder deviceBuilder{ physicalDevice };
		vkb::Device vkbDevice = deviceBuilder
			.add_pNext(&shaderDrawParametersFeatures)
			.build().value();
		m_device = vkbDevice.device;
		m_physicalDevice = physicalDevice.physical_device;
		m_gpuProperties = vkbDevice.physical_device.properties;

		m_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
		m_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

		volkLoadDevice(m_device);

		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.physicalDevice = m_physicalDevice;
		allocatorInfo.device = m_device;
		allocatorInfo.instance = m_instance;
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		vmaCreateAllocator(&allocatorInfo, &m_allocator);
		m_mainDeletionQueue.pushFunction([&]() 
			{
				vmaDestroyAllocator(m_allocator);
			});
	}

	void RenderDevice::initSwapchain()
	{
		VkSurfaceFormatKHR desiredFormat{ VK_FORMAT_B8G8R8A8_UNORM , VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

		vkb::SwapchainBuilder swapchainBuilder{ m_physicalDevice, m_device, m_surface };
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
		//drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		VkImageCreateInfo rimg_info = imageCreateInfo(m_drawImage.imageFormat, drawImageUsages, drawImageExtent);

		VmaAllocationCreateInfo rimg_allocinfo = {};
		rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vmaCreateImage(m_allocator, &rimg_info, &rimg_allocinfo, &m_drawImage.image, &m_drawImage.allocation, nullptr);

		VkImageViewCreateInfo view_info = imageviewCreateInfo(m_drawImage.imageFormat, m_drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(m_device, &view_info, nullptr, &m_drawImage.imageView));

		// Depth 
		m_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
		VkImageCreateInfo dimg_info = imageCreateInfo(m_depthImage.imageFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, drawImageExtent);

		VmaAllocationCreateInfo dimg_allocinfo = {};
		dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vmaCreateImage(m_allocator, &dimg_info, &dimg_allocinfo, &m_depthImage.image, &m_depthImage.allocation, nullptr);

		VkImageViewCreateInfo dview_info = imageviewCreateInfo(m_depthImage.imageFormat, m_depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
		VK_CHECK(vkCreateImageView(m_device, &dview_info, nullptr, &m_depthImage.imageView));

		m_mainDeletionQueue.pushFunction([=]()
			{
				vkDestroyImageView(m_device, m_drawImage.imageView, nullptr);
				vmaDestroyImage(m_allocator, m_drawImage.image, m_drawImage.allocation);

				vkDestroyImageView(m_device, m_depthImage.imageView, nullptr);
				vmaDestroyImage(m_allocator, m_depthImage.image, m_depthImage.allocation);
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

			m_mainDeletionQueue.pushFunction([=]() 
				{
					vkDestroyCommandPool(m_device, m_frames[i].commandPool, nullptr);
				});
		}

		// Immediate submit related
		VK_CHECK(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_immCommandPool));
		VkCommandBufferAllocateInfo cmdAllocInfo = Moon::commandBufferAllocateInfo(m_immCommandPool, 1);
		VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &m_immCommandBuffer));
		m_mainDeletionQueue.pushFunction([=]() 
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

			m_mainDeletionQueue.pushFunction([=]()
				{
					vkDestroySemaphore(m_device, m_frames[i].renderSemaphore, nullptr);
					vkDestroySemaphore(m_device, m_frames[i].presentSemaphore, nullptr);
					vkDestroyFence(m_device, m_frames[i].renderFence, nullptr);
				});
		}

		VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_immFence));
		m_mainDeletionQueue.pushFunction([=]() { vkDestroyFence(m_device, m_immFence, nullptr); });
	}

	void RenderDevice::initDescriptors()
	{
		// Descriptor Pool
		std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 }
		};
		m_globalDescriptorAllocator.initPool(m_device, 10, sizes);
		m_mainDeletionQueue.pushFunction([=]()
			{
				m_globalDescriptorAllocator.clearDescriptors(m_device);
				m_globalDescriptorAllocator.destroyPool(m_device);
			});

		{
			DescriptorLayoutBuilder builder;
			builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
			m_gpuSceneDataDescriptorLayout = builder.build(m_device);
			m_mainDeletionQueue.pushFunction([=]()
				{
					vkDestroyDescriptorSetLayout(m_device, m_gpuSceneDataDescriptorLayout, nullptr);
				});
		}

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			std::vector<DescriptorAllocator::PoolSizeRatio> frame_sizes = {
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
			};

			m_frames[i].frameDescriptors = DescriptorAllocator{};
			m_frames[i].frameDescriptors.initPool(m_device, 1000, frame_sizes);

			m_mainDeletionQueue.pushFunction([&, i]()
				{
					m_frames[i].frameDescriptors.destroyPool(m_device);
				});
		}
	}

	void RenderDevice::initPipelines()
	{
		m_metalRoughMaterial.buildPipelines(this);
	}

	void RenderDevice::initRayTracing()
	{
		m_physicalDeviceProperties.pNext = &m_rtProperties;
		vkGetPhysicalDeviceProperties2(m_physicalDevice, &m_physicalDeviceProperties);
	}

	void RenderDevice::initImgui()
	{
		VkDescriptorPoolSize pool_sizes[] = { 
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
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

		ImGui::CreateContext();
		ImGui_ImplSDL2_InitForVulkan(m_window);

		ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void* vulkan_instance)
			{
				return vkGetInstanceProcAddr(*(reinterpret_cast<VkInstance*>(vulkan_instance)), function_name);
			}, &m_instance);

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

		immediateSubmit([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });

		ImGui_ImplVulkan_DestroyFontUploadObjects();
		m_mainDeletionQueue.pushFunction([=]()
			{
				vkDestroyDescriptorPool(m_device, imguiPool, nullptr);
				ImGui_ImplVulkan_Shutdown();
			});
	}

	void RenderDevice::initDefaultData()
	{
		// Default textures and samplers
		uint32_t white = 0xFFFFFFFF;
		m_whiteImage = createImage((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_SAMPLED_BIT);

		uint32_t grey = 0xAAAAAAFF;
		m_greyImage = createImage((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_SAMPLED_BIT);

		uint32_t black = 0x000000FF;
		m_blackImage = createImage((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_SAMPLED_BIT);

		//checkerboard image
		uint32_t magenta = 0xFF00FFFF;
		std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
		for (int x = 0; x < 16; x++)
		{
			for (int y = 0; y < 16; y++)
			{
				pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
			}
		}
		m_errorCheckerboardImage = createImage(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_SAMPLED_BIT);

		VkSamplerCreateInfo sampl = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		sampl.magFilter = VK_FILTER_NEAREST;
		sampl.minFilter = VK_FILTER_NEAREST;
		vkCreateSampler(m_device, &sampl, nullptr, &m_defaultSamplerNearest);

		sampl.magFilter = VK_FILTER_LINEAR;
		sampl.minFilter = VK_FILTER_LINEAR;
		vkCreateSampler(m_device, &sampl, nullptr, &m_defaultSamplerLinear);

		m_mainDeletionQueue.pushFunction([&]()
			{
				vkDestroySampler(m_device, m_defaultSamplerLinear, nullptr);
				vkDestroySampler(m_device, m_defaultSamplerNearest, nullptr);

				destroyImage(m_whiteImage);
				destroyImage(m_greyImage);
				destroyImage(m_blackImage);
				destroyImage(m_errorCheckerboardImage);
			});

		GLTFMetallic_Roughness::MaterialResources materialResources;
		materialResources.colorImage = m_whiteImage;
		materialResources.colorSampler = m_defaultSamplerLinear;
		materialResources.metalRoughImage = m_whiteImage;
		materialResources.metalRoughSampler = m_defaultSamplerLinear;

		AllocatedBuffer materialConstants = createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		GLTFMetallic_Roughness::MaterialConstants* sceneUniformData = (GLTFMetallic_Roughness::MaterialConstants*)materialConstants.allocation->GetMappedData();
		sceneUniformData->baseColorFactors = glm::vec4(1, 1, 1, 1);
		sceneUniformData->metalRoughFactors = glm::vec4(1, 0.5, 0, 0);

		m_mainDeletionQueue.pushFunction([=, this]()
			{
				destroyBuffer(materialConstants);
			});

		materialResources.dataBuffer = materialConstants.buffer;
		materialResources.dataBufferOffset = 0;

		m_defaultData = m_metalRoughMaterial.writeMaterial(m_device, MaterialPass::MainColor, materialResources, m_globalDescriptorAllocator);


		std::string structurePath = { "..\\..\\Assets\\structure.glb" };
		auto structureFile = loadGltf(this, structurePath);
		assert(structureFile.has_value());
		m_loadedScenes["structure"] = *structureFile;
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

	GPUMeshBuffers RenderDevice::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
	{
		const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
		const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

		GPUMeshBuffers newSurface;
		newSurface.vertexBuffer = createBuffer(vertexBufferSize, 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY);
		VkBufferDeviceAddressInfo deviceAdressInfo
		{ 
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, 
			.buffer = newSurface.vertexBuffer.buffer 
		};
		newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(m_device, &deviceAdressInfo);
		newSurface.indexBuffer = createBuffer(indexBufferSize, 
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY);

		AllocatedBuffer staging = createBuffer(vertexBufferSize + indexBufferSize, 
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
			VMA_MEMORY_USAGE_CPU_ONLY);

		void* data = staging.allocation->GetMappedData();
		memcpy(data, vertices.data(), vertexBufferSize);
		memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

		immediateSubmit([&](VkCommandBuffer cmd)
			{
				VkBufferCopy vertexCopy{ 0 };
				vertexCopy.dstOffset = 0;
				vertexCopy.srcOffset = 0;
				vertexCopy.size = vertexBufferSize;
				vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

				VkBufferCopy indexCopy{ 0 };
				indexCopy.dstOffset = 0;
				indexCopy.srcOffset = vertexBufferSize;
				indexCopy.size = indexBufferSize;
				vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
			});

		destroyBuffer(staging);

		return newSurface;
	}

	void RenderDevice::destroyBuffer(const AllocatedBuffer& buffer)
	{
		vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
	}

	void RenderDevice::destroyImage(const AllocatedImage& image)
	{
		vkDestroyImageView(m_device, image.imageView, nullptr);
		vmaDestroyImage(m_allocator, image.image, image.allocation);
	}

	size_t RenderDevice::padUniformBufferSize(size_t originalSize)
	{
		size_t minUboAlignment = m_gpuProperties.limits.minUniformBufferOffsetAlignment;
		size_t alignedSize = originalSize;
		if (minUboAlignment > 0)
		{
			alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
		}
		return alignedSize;
	}
	
	void GLTFMetallic_Roughness::buildPipelines(RenderDevice* engine)
	{
		VkShaderModule meshFragShader;
		VkShaderModule meshVertexShader;
		if (!engine->loadShaderModule("../../shaders/mesh.frag.spv", &meshFragShader))
			std::cout << "Error when building the triangle fragment shader module" << std::endl;
		if (!engine->loadShaderModule("../../shaders/mesh.vert.spv", &meshVertexShader))
			std::cout << "Error when building the triangle vertex shader module" << std::endl;

		VkPushConstantRange matrixRange{};
		matrixRange.offset = 0;
		matrixRange.size = sizeof(GPUDrawPushConstants);
		matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		DescriptorLayoutBuilder layoutBuilder;
		layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
		layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
		layoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
		materialLayout = layoutBuilder.build(engine->getDevice());

		VkPipelineLayout newLayout;
		VkDescriptorSetLayout layouts[] = { engine->getSceneDataDescriptorLayout(), materialLayout};
		VkPipelineLayoutCreateInfo mesh_layout_info = Moon::pipelineLayoutCreateInfo();
		mesh_layout_info.setLayoutCount = 2;
		mesh_layout_info.pSetLayouts = layouts;
		mesh_layout_info.pPushConstantRanges = &matrixRange;
		mesh_layout_info.pushConstantRangeCount = 1;
		VK_CHECK(vkCreatePipelineLayout(engine->getDevice(), &mesh_layout_info, nullptr, &newLayout));

		opaquePipeline.layout = newLayout;
		transparentPipeline.layout = newLayout;

		PipelineBuilder pipelineBuilder;
		pipelineBuilder.setShaders(meshVertexShader, meshFragShader);
		pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
		pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
		pipelineBuilder.setMultisamplingNone();
		pipelineBuilder.disableBlending();
		pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
		pipelineBuilder.setColorAttachmentFormat(engine->getDrawImage().imageFormat);
		pipelineBuilder.setDepthFormat(engine->getDepthImage().imageFormat);
		pipelineBuilder.setPipelineLayout(newLayout);
		opaquePipeline.pipeline = pipelineBuilder.buildPipeline(engine->getDevice());

		pipelineBuilder.enableBlendingAdditive();
		pipelineBuilder.enableDepthTest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
		transparentPipeline.pipeline = pipelineBuilder.buildPipeline(engine->getDevice());

		vkDestroyShaderModule(engine->getDevice(), meshFragShader, nullptr);
		vkDestroyShaderModule(engine->getDevice(), meshVertexShader, nullptr);

		engine->getDeletionQueue().pushFunction([=, this]()
			{
				vkDestroyPipeline(engine->getDevice(), opaquePipeline.pipeline, nullptr);
				vkDestroyPipeline(engine->getDevice(), transparentPipeline.pipeline, nullptr);
				vkDestroyPipelineLayout(engine->getDevice(), newLayout, nullptr);
				vkDestroyDescriptorSetLayout(engine->getDevice(), materialLayout, nullptr);
			});
	}

	void GLTFMetallic_Roughness::clearResources(VkDevice device)
	{
	}

	MaterialInstance GLTFMetallic_Roughness::writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocator& descriptorAllocator)
	{
		MaterialInstance matData;
		matData.passType = pass;
		if (pass == MaterialPass::Transparent)
		{
			matData.pipeline = &transparentPipeline;
		}
		else
		{
			matData.pipeline = &opaquePipeline;
		}
		matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

		writer.clear();
		writer.writeBuffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		writer.writeImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.writeImage(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.updateSet(device, matData.materialSet);

		return matData;
	}
}
