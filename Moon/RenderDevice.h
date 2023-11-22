﻿#pragma once
#include "RenderTypes.h"
#include "RenderImage.h"
#include "RenderMesh.h"

#include <vector>
#include <deque>
#include <functional>
#include <span>
#include <unordered_map>
#include <string>

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

	struct Material
	{
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;
	};

	struct RenderObject
	{
		Mesh* mesh;
		Material* material;
		glm::mat4 transformMatrix;
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
		void init();
		void cleanup();
		void draw();
		void drawMain(VkCommandBuffer cmd);
		void drawRenderObjects(VkCommandBuffer cmd, RenderObject* first, int count);
		void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView);
		void run();

		void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

		Material* createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
		Material* getMaterial(const std::string& name);
		Mesh* getMesh(const std::string& name);

	private:
		void initVulkan();
		void initSwapchain();
		void initCommands();
		void initSyncStructures();
		void initDescriptors();
		void initPipelines();
		void initRayTracing();
		void initImgui();
		void loadMeshes();
		void initScene();


		FrameData& getCurrentFrame();

		bool loadShaderModule(const char* filePath, VkShaderModule* shaderModule);
		void uploadMesh(Mesh& mesh);

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
		AllocatedImage m_depthImage;

		//RayTracing
		VkPhysicalDeviceProperties2 m_physicalDeviceProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
		
		//Scene Management
		std::vector<RenderObject> m_renderables;
		std::unordered_map<std::string, Material> m_materials;
		std::unordered_map<std::string, Mesh> m_meshes;

		// TEMP: For Compute Gradient
		VkPipeline m_gradientPipeline;
		VkPipelineLayout m_gradientPipelineLayout;
		ComputePushConstants m_gradientPipelinePushConstant;
	};

	class PipelineBuilder 
	{
	public:
		PipelineBuilder();

		void clear();
		VkPipeline buildPipeline(VkDevice device);

		void setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
		void setPipelineLayout(VkPipelineLayout pipelineLayout);
		void setInputTopology(VkPrimitiveTopology topology);
		void setVertexInputInfo(VertexInputDescription& vertexInput);
		void setPolygonMode(VkPolygonMode mode);
		void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
		void setMultisamplingNone();
		void disableBlending();
		void setColorAttachmentFormat(VkFormat format);
		void setDepthFormat(VkFormat format);
		void enableDepthTest(bool bDepthTest, bool bDepthWrite, VkCompareOp compareOp);

	public:
		std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
		VkFormat m_colorAttachmentformat;
		VkPipelineVertexInputStateCreateInfo m_vertexInputInfo;
		VkPipelineInputAssemblyStateCreateInfo m_inputAssembly;
		VkPipelineRasterizationStateCreateInfo m_rasterizer;
		VkPipelineColorBlendAttachmentState m_colorBlendAttachment;
		VkPipelineMultisampleStateCreateInfo m_multisampling;
		VkPipelineLayout m_pipelineLayout;
		VkPipelineDepthStencilStateCreateInfo m_depthStencil;
		VkPipelineRenderingCreateInfo m_renderInfo;
	};
}
