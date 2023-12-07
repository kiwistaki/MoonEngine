#include "Pipeline.h"
#include "RenderUtilities.h"

namespace Moon
{
	PipelineBuilder::PipelineBuilder()
	{
		clear();
	}

	void PipelineBuilder::clear()
	{
		//m_vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		m_inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		m_rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		m_colorBlendAttachment = {};
		m_multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		m_pipelineLayout = {};
		m_depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		m_renderInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		m_shaderStages.clear();
		//m_colorAttachmentformats.clear();
	}

	VkPipeline PipelineBuilder::buildPipeline(VkDevice device)
	{
		VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &m_colorBlendAttachment;

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &m_renderInfo;
		pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStages.size());
		pipelineInfo.pStages = m_shaderStages.data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &m_inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &m_rasterizer;
		pipelineInfo.pMultisampleState = &m_multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDepthStencilState = &m_depthStencil;
		pipelineInfo.layout = m_pipelineLayout;

		VkDynamicState state[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicInfo.dynamicStateCount = 2;
		dynamicInfo.pDynamicStates = &state[0];
		pipelineInfo.pDynamicState = &dynamicInfo;


		VkPipeline newPipeline;
		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
		{
			std::cout << "failed to create pipeline\n";
			return VK_NULL_HANDLE; // failed to create graphics pipeline
		}
		else
		{
			return newPipeline;
		}
	}

	void PipelineBuilder::setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader)
	{
		m_shaderStages.clear();
		m_shaderStages.push_back(pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
		m_shaderStages.push_back(pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
	}

	void PipelineBuilder::setPipelineLayout(VkPipelineLayout pipelineLayout)
	{
		m_pipelineLayout = pipelineLayout;
	}

	void PipelineBuilder::setInputTopology(VkPrimitiveTopology topology)
	{
		m_inputAssembly.topology = topology;
		m_inputAssembly.primitiveRestartEnable = VK_FALSE;
	}

	void PipelineBuilder::setVertexInputInfo(VertexInputDescription& vertexInput)
	{
		m_vertexInputInfo.pVertexAttributeDescriptions = vertexInput.attributes.data();
		m_vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInput.attributes.size());
		m_vertexInputInfo.pVertexBindingDescriptions = vertexInput.bindings.data();
		m_vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInput.bindings.size());
	}

	void PipelineBuilder::setPolygonMode(VkPolygonMode mode)
	{
		m_rasterizer.polygonMode = mode;
		m_rasterizer.lineWidth = 1.f;
	}

	void PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace)
	{
		m_rasterizer.cullMode = cullMode;
		m_rasterizer.frontFace = frontFace;
	}

	void PipelineBuilder::setMultisamplingNone()
	{
		m_multisampling.sampleShadingEnable = VK_FALSE;
		m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		m_multisampling.minSampleShading = 1.0f;
		m_multisampling.pSampleMask = nullptr;
		m_multisampling.alphaToCoverageEnable = VK_FALSE;
		m_multisampling.alphaToOneEnable = VK_FALSE;
	}

	void PipelineBuilder::disableBlending()
	{
		m_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		m_colorBlendAttachment.blendEnable = VK_FALSE;
	}

	void PipelineBuilder::setColorAttachmentFormat(VkFormat format)
	{
		m_colorAttachmentformat = format;
		m_renderInfo.colorAttachmentCount = 1;
		m_renderInfo.pColorAttachmentFormats = &m_colorAttachmentformat;
	}

	void PipelineBuilder::setDepthFormat(VkFormat format)
	{
		m_renderInfo.depthAttachmentFormat = format;
	}

	void PipelineBuilder::disableDepthTest()
	{
		m_depthStencil.depthTestEnable = VK_FALSE;
		m_depthStencil.depthWriteEnable = VK_FALSE;
		m_depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
		m_depthStencil.depthBoundsTestEnable = VK_FALSE;
		m_depthStencil.stencilTestEnable = VK_FALSE;
		m_depthStencil.front = {};
		m_depthStencil.back = {};
		m_depthStencil.minDepthBounds = 0.f;
		m_depthStencil.maxDepthBounds = 1.f;
	}

	void PipelineBuilder::enableDepthTest(bool bDepthWrite, VkCompareOp compareOp)
	{
		m_depthStencil.depthTestEnable = VK_TRUE;
		m_depthStencil.depthWriteEnable = bDepthWrite;
		m_depthStencil.depthCompareOp = compareOp;
		m_depthStencil.depthBoundsTestEnable = VK_FALSE;
		m_depthStencil.stencilTestEnable = VK_FALSE;
		m_depthStencil.front = {};
		m_depthStencil.back = {};
		m_depthStencil.minDepthBounds = 0.f;
		m_depthStencil.maxDepthBounds = 1.f;
	}
}