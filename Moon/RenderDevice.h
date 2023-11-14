#pragma once
#include "RenderTypes.h"
#include "RenderImage.h"

#include <vector>
#include <deque>
#include <functional>
#include <span>

#include <glm/glm.hpp>

constexpr unsigned int FRAME_OVERLAP = 2;
constexpr unsigned int SCREEN_WIDTH = 1920;
constexpr unsigned int SCREEN_HEIGHT = 1080;

//Forward declaration
struct SDL_Window;

namespace Moon
{
	struct ComputePushConstants
	{
		glm::vec4 data1;
		glm::vec4 data2;
		glm::vec4 data3;
		glm::vec4 data4;
	};

	struct DeletionQueue
	{
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function)
		{
			deletors.push_back(function);
		}

		void flush()
		{
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
			{
				(*it)();
			}
			deletors.clear();
		}
	};

	struct FrameData
	{
		VkSemaphore presentSemaphore, renderSemaphore;
		VkFence renderFence;

		VkCommandPool commandPool;
		VkCommandBuffer mainCommandBuffer;

		DeletionQueue deletionQueue;
	};

	struct DescriptorLayoutBuilder
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings;

		void addBinding(uint32_t binding, VkDescriptorType type);
		void clear();
		VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages);
	};

	struct DescriptorAllocator
	{
		struct PoolSizeRatio
		{
			VkDescriptorType type;
			float ratio;
		};

		VkDescriptorPool pool;

		void initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
		void clearDescriptors(VkDevice device);
		void destroyPool(VkDevice device);

		VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
	};

	class RenderDevice
	{
	public:
		PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
		PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR;

		void init();
		void cleanup();
		void draw();
		void drawMain(VkCommandBuffer cmd);
		void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView);
		void run();

		void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	private:
		void initVulkan();
		void initSwapchain();
		void initCommands();
		void initSyncStructures();
		void initDescriptors();
		void initPipelines();
		void initRayTracing();
		void initImgui();

		FrameData& getCurrentFrame();

		bool loadShaderModule(const char* filePath, VkShaderModule* shaderModule);

	private:
		bool m_isInitialized{ false };
		int m_frameNumber{ 0 };

		VkExtent2D m_windowExtent{ SCREEN_WIDTH , SCREEN_HEIGHT };
		SDL_Window* m_window{ nullptr };

		// Basic Vulkan
		VkInstance m_instance;
		VkDebugUtilsMessengerEXT m_debugMessenger;
		VkPhysicalDevice m_physicalDevice;
		VkDevice m_device;
		VkSurfaceKHR m_surface;
		VkQueue m_graphicsQueue;
		uint32_t m_graphicsQueueFamily;
		VmaAllocator m_allocator;
		FrameData m_frames[FRAME_OVERLAP];
		DeletionQueue m_mainDeletionQueue;

		// Swapchain
		VkSwapchainKHR m_swapchain;
		VkFormat m_swapchainImageFormat;
		std::vector<VkImage> m_swapchainImages;
		std::vector<VkImageView> m_swapchainImageViews;

		DescriptorAllocator m_globalDescriptorAllocator;
		VkDescriptorSetLayout m_drawImageDescriptorLayout;
		VkDescriptorSet m_drawImageDescriptorSet;

		// Immediate Submit structures
		VkFence m_immFence;
		VkCommandBuffer m_immCommandBuffer;
		VkCommandPool m_immCommandPool;

		//Internal Render Image
		AllocatedImage m_drawImage;

		VkPipeline m_gradientPipeline;
		VkPipelineLayout m_gradientPipelineLayout;
		ComputePushConstants m_gradientPipelinePushConstant;

		VkPhysicalDeviceProperties2 m_physicalDeviceProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
	};
}
