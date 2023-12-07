#include "Mesh.h"
#include "RenderDevice.h"

#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <iostream>

namespace Moon
{
	std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(RenderDevice* device, std::filesystem::path filePath, bool useNormalAsColor)
	{
		std::cout << "Loading GLTF: " << filePath << std::endl;

		fastgltf::GltfDataBuffer data;
		data.loadFromFile(filePath);

		fastgltf::Asset gltf;
		fastgltf::Parser parser{};
		constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

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
							indices.push_back(idx + (uint32_t)initial_vtx);
						});
				}

				fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];

				size_t vidx = initial_vtx;
				fastgltf::iterateAccessor<glm::vec3>(gltf, posAccessor, [&](glm::vec3 v)
					{
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

			// Display normals as vertex colors for debugging
			if (useNormalAsColor)
			{
				for (Vertex& vtx : vertices)
				{
					vtx.color = glm::vec4(vtx.normal, 1.f);
				}
			}

			newmesh.meshBuffers = device->uploadMesh(indices, vertices);
			meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newmesh)));
		}

		return meshes;
	}
}
