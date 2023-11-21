#pragma once
#include "RenderTypes.h"
#include <vector>
#include <glm/vec3.hpp>

namespace Moon
{
	struct VertexInputDescription
	{
		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;

		VkPipelineVertexInputStateCreateFlags flags = 0;
	};

	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;

		static VertexInputDescription getVertexDescription();
	};

	struct Mesh
	{
		std::vector<Vertex> m_vertices;
		AllocatedBuffer m_vertexBuffer;

		bool loadFromObj(const char* filename);
	};
}