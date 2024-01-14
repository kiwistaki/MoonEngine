#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <Volk/volk.h>

#include <vk_mem_alloc.h>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <iostream>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

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
    struct AllocatedBuffer
    {
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo info;
    };

    struct AllocatedImage
    {
        VkImage image;
        VkImageView imageView;
        VkExtent3D imageExtent;
        VkFormat imageFormat;
        VmaAllocation allocation;
    };

	struct GPUSceneData
	{
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 viewproj;
		glm::vec4 ambientColor;
		glm::vec4 sunlightDirection; // w for sun power
		glm::vec4 sunlightColor;
	};

	enum class MaterialPass : uint8_t
	{
		MainColor,
		Transparent,
		Other
	};
	struct MaterialPipeline
	{
		VkPipeline pipeline;
		VkPipelineLayout layout;
	};

	struct MaterialInstance
	{
		MaterialPipeline* pipeline;
		VkDescriptorSet materialSet;
		MaterialPass passType;
	};

	struct DrawContext;

	class IRenderable
	{
		virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
	};

	struct Node : public IRenderable
	{
		std::weak_ptr<Node> parent;
		std::vector<std::shared_ptr<Node>> children;

		glm::mat4 localTransform;
		glm::mat4 worldTransform;

		void refreshTransform(const glm::mat4& parentMatrix)
		{
			worldTransform = parentMatrix * localTransform;
			for (auto c : children)
			{
				c->refreshTransform(worldTransform);
			}
		}

		virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
		{
			for (auto& c : children)
			{
				c->Draw(topMatrix, ctx);
			}
		}
	};
}