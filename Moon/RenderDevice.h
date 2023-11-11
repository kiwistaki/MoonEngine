#pragma once
#include "RenderTypes.h"
#include "RenderImage.h"

#include <vector>
#include <deque>
#include <functional>
#include <span>

constexpr unsigned int FRAME_OVERLAP = 2;
constexpr unsigned int SCREEN_WIDTH = 1920;
constexpr unsigned int SCREEN_HEIGHT = 1080;

//Forward declaration
struct SDL_Window;

namespace Moon
{
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
		void run();

	private:
		void initVulkan();
		void initSwapchain();
		void initCommands();
		void initSyncStructures();
		void initDescriptors();
		void initPipelines();

		FrameData& getCurrentFrame();

		bool loadShaderModule(const char* filePath, VkShaderModule* shaderModule);

	private:
		bool _isInitialized{ false };
		int _frameNumber{ 0 };

		VkExtent2D _windowExtent{ SCREEN_WIDTH , SCREEN_HEIGHT };
		SDL_Window* _window{ nullptr };

		// Basic Vulkan
		VkInstance _instance;
		VkDebugUtilsMessengerEXT _debug_messenger;
		VkPhysicalDevice _chosenGPU;
		VkDevice _device;
		VkSurfaceKHR _surface;
		VkQueue _graphicsQueue;
		uint32_t _graphicsQueueFamily;
		VmaAllocator _allocator;
		FrameData _frames[FRAME_OVERLAP];
		DeletionQueue _mainDeletionQueue;

		// Swapchain
		VkSwapchainKHR _swapchain;
		VkFormat _swapchainImageFormat;
		std::vector<VkImage> _swapchainImages;
		std::vector<VkImageView> _swapchainImageViews;

		DescriptorAllocator globalDescriptorAllocator;
		VkDescriptorSetLayout _drawImageDescriptorLayout;
		VkDescriptorSet _drawImageDescriptorSet;

		//Internal Render Image
		AllocatedImage _drawImage;

		VkPipeline _gradientPipeline;
		VkPipelineLayout _gradientPipelineLayout;
	};
}
