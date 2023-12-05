#include "RenderMesh.h"
#include "RenderDevice.h"
#include <tiny_obj_loader.h>

#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <iostream>

namespace Moon
{
	VertexInputDescription Vertex::getVertexDescription()
	{
		VkVertexInputBindingDescription mainBinding = {};
		mainBinding.binding = 0;
		mainBinding.stride = sizeof(Vertex);
		mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription positionAttribute = {};
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = offsetof(Vertex, position);

		VkVertexInputAttributeDescription normalAttribute = {};
		normalAttribute.binding = 0;
		normalAttribute.location = 1;
		normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		normalAttribute.offset = offsetof(Vertex, normal);

		VkVertexInputAttributeDescription colorAttribute = {};
		colorAttribute.binding = 0;
		colorAttribute.location = 2;
		colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		colorAttribute.offset = offsetof(Vertex, color);

		VertexInputDescription description;
		description.bindings.push_back(mainBinding);
		description.attributes.push_back(positionAttribute);
		description.attributes.push_back(normalAttribute);
		description.attributes.push_back(colorAttribute);
		return description;
	}

	bool Mesh::loadFromObj(const char* filename)
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;

		std::string warn;
		std::string err;
		tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename, nullptr);
		if (!warn.empty())
		{
			std::cout << "WARN: " << warn << std::endl;
		}
		if (!err.empty())
		{
			std::cerr << err << std::endl;
			return false;
		}

		for (size_t s = 0; s < shapes.size(); s++)
		{
			size_t index_offset = 0;
			for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
			{
				int fv = 3;

				for (size_t v = 0; v < fv; v++)
				{
					// access to vertex
					tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

					//vertex position
					tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
					tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
					tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
					//vertex normal
					tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
					tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
					tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];

					Vertex new_vert;
					new_vert.position.x = vx;
					new_vert.position.y = vy;
					new_vert.position.z = vz;
					new_vert.normal.x = nx;
					new_vert.normal.y = ny;
					new_vert.normal.z = nz;
					// TEMP
					new_vert.color = glm::vec4(new_vert.normal, 1.0f);
					m_vertices.push_back(new_vert);
				}
				index_offset += fv;
			}
		}

		return true;
	}

	std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(RenderDevice* device, std::filesystem::path filePath)
	{
		std::cout << "Loading GLTF: " << filePath << std::endl;

		constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

		fastgltf::GltfDataBuffer data;
		data.loadFromFile(filePath);

		fastgltf::Asset gltf;
		fastgltf::Parser parser;
		auto load = parser.loadBinaryGLTF(&data, filePath.parent_path(), gltfOptions);
		if (load)
		{
			gltf = std::move(load.get());
		}
		else
		{
			std::cout << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
			return {};
		}

		std::vector<std::shared_ptr<MeshAsset>> meshes;
		std::vector<uint32_t> indices;
		std::vector<Vertex> vertices;
		for (fastgltf::Mesh& mesh : gltf.meshes)
		{
			MeshAsset newmesh;
			newmesh.name = mesh.name;

			indices.clear();
			vertices.clear();

			for (auto&& p : mesh.primitives)
			{
				SubMesh newSubmesh;
				newSubmesh.startIndex = (uint32_t)indices.size();
				newSubmesh.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

				size_t initial_vtx = vertices.size();
				{
					fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];

					fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor, [&](std::uint32_t idx)
						{
						indices.push_back(idx + initial_vtx);
						});
				}

				fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];

				size_t vidx = initial_vtx;
				fastgltf::iterateAccessor<glm::vec3>(gltf, posAccessor, [&](glm::vec3 v) 
					{ 
						vertices[vidx++].position = v;
						Vertex newvtx;
						newvtx.position = v;
						newvtx.normal = { 1,0,0 };
						newvtx.color = glm::vec4{ 1.f };
						newvtx.uv_x = 0;
						newvtx.uv_y = 0;
						vertices.push_back(newvtx);
					});

				auto normals = p.findAttribute("NORMAL");
				if (normals != p.attributes.end())
				{
					vidx = initial_vtx;
					fastgltf::iterateAccessor<glm::vec3>(gltf, gltf.accessors[(*normals).second], [&](glm::vec3 v) 
						{ 
							vertices[vidx++].normal = v; 
						});
				}

				auto uv = p.findAttribute("TEXCOORD_0");
				if (uv != p.attributes.end())
				{
					vidx = initial_vtx;
					fastgltf::iterateAccessor<glm::vec2>(gltf, gltf.accessors[(*uv).second], [&](glm::vec2 v)
						{
							vertices[vidx].uv_x = v.x;
							vertices[vidx].uv_y = v.y;
							vidx++;
						});
				}

				auto colors = p.findAttribute("COLOR_0");
				if (colors != p.attributes.end())
				{
					vidx = initial_vtx;
					fastgltf::iterateAccessor<glm::vec4>(gltf, gltf.accessors[(*colors).second], [&](glm::vec4 v)
						{ 
							vertices[vidx++].color = v; 
						});
				}

				newmesh.surfaces.push_back(newSubmesh);
			}

			newmesh.meshBuffers = device->uploadMesh(indices, vertices);

			meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newmesh)));
		}

		return meshes;
	}
}
