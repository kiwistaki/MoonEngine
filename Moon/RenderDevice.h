#pragma once
#include "RenderTypes.h"
#include "Image.h"
#include "Mesh.h"
#include "Descriptor.h"
#include "Pipeline.h"
#include "Camera.h"

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
	//Forward declaration
	class RenderDevice;

	struct MeshPushConstants
	{
		glm::vec4 data;
		glm::mat4 renderMatrix;
	};

	struct DeletionQueue
	{
		std::deque<std::function<void()>> deletors;

		void pushFunction(std::function<void()>&& function)
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

	struct GLTFMetallic_Roughness
	{
		MaterialPipeline opaquePipeline;
		MaterialPipeline transparentPipeline;
		VkDescriptorSetLayout materialLayout;

		struct MaterialConstants
		{
			glm::vec4 baseColorFactors;
			glm::vec4 metalRoughFactors;
			glm::vec4 extra[14];
		};

		struct MaterialResources
		{
			AllocatedImage colorImage;
			VkSampler colorSampler;
			AllocatedImage metalRoughImage;
			VkSampler metalRoughSampler;
			VkBuffer dataBuffer;
			uint32_t dataBufferOffset;
		};

		DescriptorWriter writer;

		void buildPipelines(RenderDevice* engine);
		void clearResources(VkDevice device);

		MaterialInstance writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocator& descriptorAllocator);
	};

	struct EngineStats
	{
		float frametime;
		int triangleCount;
		int drawcallCount;
		float sceneUpdateTime;
		float meshDrawTime;
		float assetLoadTime;
	};

	class RenderDevice
	{
	public:
		void init();
		void cleanup();
		void draw();
		void drawImpl(VkCommandBuffer cmd);
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

		bool loadShaderModule(const char* filePath, VkShaderModule* shaderModule);

		VkDevice getDevice() { return m_device; }
		VkDescriptorSetLayout getSceneDataDescriptorLayout() { return m_gpuSceneDataDescriptorLayout; }
		AllocatedImage getDrawImage() { return m_drawImage; }
		AllocatedImage getDepthImage() { return m_depthImage; }

		DeletionQueue& getDeletionQueue() { return m_mainDeletionQueue; }

		void updateScene();

	public:
		// Default Image
		AllocatedImage m_whiteImage;
		AllocatedImage m_blackImage;
		AllocatedImage m_greyImage;
		AllocatedImage m_errorCheckerboardImage;
		VkSampler m_defaultSamplerLinear;
		VkSampler m_defaultSamplerNearest;

		GLTFMetallic_Roughness m_metalRoughMaterial;

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

		FrameData& getCurrentFrame();
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
		VkDescriptorSetLayout m_globalSetLayout;
		VkDescriptorSetLayout m_objectSetLayout;

		// Immediate Submit structures
		VkFence m_immFence;
		VkCommandBuffer m_immCommandBuffer;
		VkCommandPool m_immCommandPool;

		// Internal Render Image
		AllocatedImage m_drawImage;
		AllocatedImage m_depthImage;

		// RayTracing
		VkPhysicalDeviceProperties2 m_physicalDeviceProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };

		// For GLTF mesh rendering
		VkDescriptorSetLayout m_gpuSceneDataDescriptorLayout;
		MaterialInstance m_defaultData;
		DrawContext m_mainDrawContext;
		GPUSceneData m_sceneData;
		std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> m_loadedScenes;

		Camera m_mainCamera;

		EngineStats m_stats;
	};
}
