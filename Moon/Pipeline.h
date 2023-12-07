#pragma once
#include "RenderTypes.h"
#include "Mesh.h"

namespace Moon
{
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
		void disableDepthTest();
		void enableDepthTest(bool bDepthWrite, VkCompareOp compareOp);

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