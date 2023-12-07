#pragma once
#include "RenderTypes.h"

#include <filesystem>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

namespace Moon
{
	struct Vertex
	{
		glm::vec3 position;
		float uv_x;
		glm::vec3 normal;
		float uv_y;
		glm::vec4 color;
	};

	struct GPUMeshBuffers
	{
		AllocatedBuffer indexBuffer;
		AllocatedBuffer vertexBuffer;
		VkDeviceAddress vertexBufferAddress;
	};

	struct GPUDrawPushConstants
	{
		glm::mat4 worldMatrix;
		VkDeviceAddress vertexBuffer;
	};

	struct SubMesh
	{
		uint32_t startIndex;
		uint32_t count;
	};

	struct MeshAsset
	{
		std::string name;
		std::vector<SubMesh> surfaces;
		GPUMeshBuffers meshBuffers;
	};

	//Forward declaration
	class RenderDevice;
	std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(RenderDevice* device, std::filesystem::path filePath, bool useNormalAsColor = false);

}