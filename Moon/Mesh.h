#pragma once
#include "RenderTypes.h"
#include "Descriptor.h"

#include <filesystem>
#include <unordered_map>

#include <glm/vec3.hpp>

namespace Moon
{
	//Forward declaration
	class RenderDevice;

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

	struct GLTFMaterial
	{
		MaterialInstance data;
	};

	struct Bounds
	{
		glm::vec3 origin;
		float sphereRadius;
		glm::vec3 extents;
	};

	struct SubMesh
	{
		uint32_t startIndex;
		uint32_t count;
		Bounds bounds;
		std::shared_ptr<GLTFMaterial> material;
	};

	struct MeshAsset
	{
		std::string name;
		std::vector<SubMesh> surfaces;
		GPUMeshBuffers meshBuffers;
	};

	struct RenderObject
	{
		uint32_t indexCount;
		uint32_t firstIndex;
		VkBuffer indexBuffer;

		MaterialInstance* material;
		Bounds bounds;
		glm::mat4 transform;
		VkDeviceAddress vertexBufferAddress;
	};

	struct DrawContext
	{
		std::vector<RenderObject> OpaqueSurfaces;
		std::vector<RenderObject> TransparentSurfaces;
	};

	struct MeshNode : public Node
	{
		std::shared_ptr<MeshAsset> mesh;

		virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);
	};

	struct LoadedGLTF : public IRenderable
	{
		std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
		std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
		std::unordered_map<std::string, AllocatedImage> images;
		std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

		std::vector<std::shared_ptr<Node>> topNodes;

		std::vector<VkSampler> samplers;
		DescriptorAllocator descriptorPool;
		AllocatedBuffer materialDataBuffer;

		RenderDevice* creator;

		~LoadedGLTF() { clearAll(); };
		virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

	private:
		void clearAll();
	};

	std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(RenderDevice* engine, std::string_view filePath);
}