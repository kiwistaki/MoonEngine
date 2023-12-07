#pragma once
#include "RenderTypes.h"
#include "Image.h"
#include "Mesh.h"
#include "Descriptor.h"
#include "Pipeline.h"

#include <functional>
#include <unordered_map>

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

	struct MeshPushConstants
	{
		glm::vec4 data;
		glm::mat4 renderMatrix;
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
		DescriptorAllocator frameDescriptors;
	};

	class RenderDevice
	{
	public:
		void init();
		void cleanup();
		void draw();
		void drawImpl(VkCommandBuffer cmd);
		void drawBackground(VkCommandBuffer cmd);
		void drawMeshes(VkCommandBuffer cmd);
		void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView);
		void run();

		void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

		AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
		AllocatedImage createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage);
		AllocatedImage createImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage);

		GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
		void destroyBuffer(const AllocatedBuffer& buffer);
		void destroyImage(const AllocatedImage& image);

	private:
		void initVulkan();
		void initSwapchain();
		void initCommands();
		void initSyncStructures();
		void initDescriptors();
		void initPipelines();
		void initRayTracing();
		void initImgui();
		void initDefaultData();

		//TEMP:
		void initGradientPipeline();
		void initMeshPipeline();

		FrameData& getCurrentFrame();

		bool loadShaderModule(const char* filePath, VkShaderModule* shaderModule);
		size_t padUniformBufferSize(size_t originalSize);

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
		VkPhysicalDeviceProperties m_gpuProperties;

		// Swapchain
		VkSwapchainKHR m_swapchain;
		VkFormat m_swapchainImageFormat;
		std::vector<VkImage> m_swapchainImages;
		std::vector<VkImageView> m_swapchainImageViews;

		DescriptorAllocator m_globalDescriptorAllocator;
		VkDescriptorSetLayout m_drawImageDescriptorLayout;
		VkDescriptorSet m_drawImageDescriptorSet;

		VkDescriptorSetLayout m_globalSetLayout;
		VkDescriptorSetLayout m_objectSetLayout;

		// Immediate Submit structures
		VkFence m_immFence;
		VkCommandBuffer m_immCommandBuffer;
		VkCommandPool m_immCommandPool;

		//Internal Render Image
		AllocatedImage m_drawImage;
		AllocatedImage m_depthImage;

		//RayTracing
		VkPhysicalDeviceProperties2 m_physicalDeviceProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };

		//TEMP: Mesh pipeline
		VkPipelineLayout m_meshPipelineLayout;
		VkPipeline m_meshPipeline;
		std::vector<std::shared_ptr<MeshAsset>> m_testMeshes;
		int m_meshIndex;

		//TEMP: Default Image
		AllocatedImage m_whiteImage;
		AllocatedImage m_blackImage;
		AllocatedImage m_greyImage;
		AllocatedImage m_errorCheckerboardImage;
		VkSampler m_defaultSamplerLinear;
		VkSampler m_defaultSamplerNearest;

		// TEMP: For Compute Gradient
		VkPipeline m_gradientPipeline;
		VkPipelineLayout m_gradientPipelineLayout;
		ComputePushConstants m_gradientPipelinePushConstant;
	};
}
