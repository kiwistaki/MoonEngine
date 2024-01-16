#include "Mesh.h"
#include "RenderDevice.h"

#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Moon
{
	void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
	{
		glm::mat4 nodeMatrix = topMatrix * worldTransform;

		for (auto& s : mesh->surfaces)
		{
			RenderObject def;
			def.indexCount = s.count;
			def.firstIndex = s.startIndex;
			def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
			def.material = &s.material->data;
			def.bounds = s.bounds;
			def.transform = nodeMatrix;
			def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

			if (s.material->data.passType == MaterialPass::Transparent)
			{
				ctx.TransparentSurfaces.push_back(def);
			}
			else
			{
				ctx.OpaqueSurfaces.push_back(def);
			}
		}

		Node::Draw(topMatrix, ctx);
	}

	void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
	{
		for (auto& n : topNodes)
		{
			n->Draw(topMatrix, ctx);
		}
	}

	void LoadedGLTF::clearAll()
	{
		VkDevice dv = creator->getDevice();

		descriptorPool.destroyPool(dv);
		creator->destroyBuffer(materialDataBuffer);

		for (auto& [k, v] : meshes)
		{
			creator->destroyBuffer(v->meshBuffers.indexBuffer);
			creator->destroyBuffer(v->meshBuffers.vertexBuffer);
		}

		for (auto& [k, v] : images)
		{
			if (v.image == creator->m_errorCheckerboardImage.image)
			{
				continue;
			}
			creator->destroyImage(v);
		}

		for (auto& sampler : samplers)
		{
			vkDestroySampler(dv, sampler, nullptr);
		}
	}

	VkFilter extractFilter(fastgltf::Filter filter)
	{
		switch (filter)
		{
		case fastgltf::Filter::Nearest:
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::NearestMipMapLinear:
			return VK_FILTER_NEAREST;
		case fastgltf::Filter::Linear:
		case fastgltf::Filter::LinearMipMapNearest:
		case fastgltf::Filter::LinearMipMapLinear:
		default:
			return VK_FILTER_LINEAR;
		}
	}

	VkSamplerMipmapMode extractMipmapMode(fastgltf::Filter filter)
	{
		switch (filter)
		{
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::LinearMipMapNearest:
			return VK_SAMPLER_MIPMAP_MODE_NEAREST;
		case fastgltf::Filter::NearestMipMapLinear:
		case fastgltf::Filter::LinearMipMapLinear:
		default:
			return VK_SAMPLER_MIPMAP_MODE_LINEAR;
		}
	}

	std::optional<AllocatedImage> loadImage(RenderDevice* engine, fastgltf::Asset& asset, fastgltf::Image& image)
	{
		AllocatedImage newImage{};
		int width, height, nrChannels;

		std::visit(
			fastgltf::visitor
			{
				[](auto& arg) {},
				[&](fastgltf::sources::URI& filePath) //when textures are stored outside of the gltf/glb file
				{
					assert(filePath.fileByteOffset == 0);
					assert(filePath.uri.isLocalPath());

					const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());
					unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
					if (data)
					{
						VkExtent3D imagesize;
						imagesize.width = width;
						imagesize.height = height;
						imagesize.depth = 1;

						newImage = engine->createImage(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

						stbi_image_free(data);
					}
				},
				[&](fastgltf::sources::Vector& vector) //when fastgltf loads the texture into a std::vector type structure
				{
					unsigned char* data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()),
						&width, &height, &nrChannels, 4);
					if (data)
					{
						VkExtent3D imagesize;
						imagesize.width = width;
						imagesize.height = height;
						imagesize.depth = 1;

						newImage = engine->createImage(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

						stbi_image_free(data);
					}
				},
				[&](fastgltf::sources::BufferView& view) //when image file is embedded into the binary GLB file
				{
					auto& bufferView = asset.bufferViews[view.bufferViewIndex];
					auto& buffer = asset.buffers[bufferView.bufferIndex];

					std::visit(fastgltf::visitor 
						{
							[](auto& arg) {},
							[&](fastgltf::sources::Vector& vector)
							{
								unsigned char* data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset,
									static_cast<int>(bufferView.byteLength),
									&width, &height, &nrChannels, 4);
								if (data)
								{
									VkExtent3D imagesize;
									imagesize.width = width;
									imagesize.height = height;
									imagesize.depth = 1;

									newImage = engine->createImage(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

									stbi_image_free(data);
								}
							}
						},
					buffer.data);
				},
			},
		image.data);

		// if loading the data has failed
		if (newImage.image == VK_NULL_HANDLE)
		{
			return {};
		}
		else
		{
			return newImage;
		}
	}

	std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(RenderDevice* engine, std::string_view filePath)
	{
		std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
		scene->creator = engine;
		LoadedGLTF& file = *scene.get();

		fastgltf::Parser parser{};
		constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

		fastgltf::GltfDataBuffer data;
		data.loadFromFile(filePath);

		fastgltf::Asset gltf;
		std::filesystem::path path = filePath;

		// load gltf file
		auto type = fastgltf::determineGltfFileType(&data);
		if (type == fastgltf::GltfType::glTF)
		{
			auto load = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
			if (load)
			{
				gltf = std::move(load.get());
			}
			else
			{
				std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
				return {};
			}
		}
		else if (type == fastgltf::GltfType::GLB)
		{
			auto load = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
			if (load)
			{
				gltf = std::move(load.get());
			}
			else
			{
				std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
				return {};
			}
		}
		else
		{
			std::cerr << "Failed to determine glTF container" << std::endl;
			return {};
		}

		// init descriptor pool
		std::vector<DescriptorAllocator::PoolSizeRatio> sizes = { 
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
		};
		file.descriptorPool.initPool(engine->getDevice(), static_cast<uint32_t>(gltf.materials.size()), sizes);

		// load samplers
		for (fastgltf::Sampler& sampler : gltf.samplers)
		{
			VkSampler newSampler;
			VkSamplerCreateInfo samplerCI = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			vkCreateSampler(engine->getDevice(), &samplerCI, nullptr, &newSampler);

			file.samplers.push_back(newSampler);
		}

		// temporal arrays
		std::vector<std::shared_ptr<MeshAsset>> meshes;
		std::vector<std::shared_ptr<Node>> nodes;
		std::vector<AllocatedImage> images;
		std::vector<std::shared_ptr<GLTFMaterial>> materials;

		// load textures
		for (fastgltf::Image& image : gltf.images)
		{
			std::optional<AllocatedImage> img = loadImage(engine, gltf, image);

			if (img.has_value())
			{
				images.push_back(*img);
				file.images[image.name.c_str()] = *img;
			}
			else
			{
				images.push_back(engine->m_errorCheckerboardImage);
				std::cout << "gltf failed to load texture " << image.name << std::endl;
			}
		}

		// create buffer to hold the material data
		file.materialDataBuffer = engine->createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		int data_index = 0;
		GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants = (GLTFMetallic_Roughness::MaterialConstants*)file.materialDataBuffer.info.pMappedData;

		// load materials
		for (fastgltf::Material& mat : gltf.materials)
		{
			std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
			materials.push_back(newMat);
			file.materials[mat.name.c_str()] = newMat;

			GLTFMetallic_Roughness::MaterialConstants constants;
			constants.baseColorFactors.x = mat.pbrData.baseColorFactor[0];
			constants.baseColorFactors.y = mat.pbrData.baseColorFactor[1];
			constants.baseColorFactors.z = mat.pbrData.baseColorFactor[2];
			constants.baseColorFactors.w = mat.pbrData.baseColorFactor[3];
			constants.metalRoughFactors.x = mat.pbrData.metallicFactor;
			constants.metalRoughFactors.y = mat.pbrData.roughnessFactor;
			
			// write material parameters to buffer
			sceneMaterialConstants[data_index] = constants;

			MaterialPass passType = MaterialPass::MainColor;
			if (mat.alphaMode == fastgltf::AlphaMode::Blend)
			{
				passType = MaterialPass::Transparent;
			}

			// default the material textures
			GLTFMetallic_Roughness::MaterialResources materialResources;
			materialResources.colorImage = engine->m_whiteImage;
			materialResources.colorSampler = engine->m_defaultSamplerLinear;
			materialResources.metalRoughImage = engine->m_whiteImage;
			materialResources.metalRoughSampler = engine->m_defaultSamplerLinear;

			// set the uniform buffer for the material data
			materialResources.dataBuffer = file.materialDataBuffer.buffer;
			materialResources.dataBufferOffset = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);

			// grab textures from gltf file
			if (mat.pbrData.baseColorTexture.has_value())
			{
				size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
				size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

				materialResources.colorImage = images[img];
				materialResources.colorSampler = file.samplers[sampler];
			}

			// build material
			newMat->data = engine->m_metalRoughMaterial.writeMaterial(engine->getDevice(), passType, materialResources, file.descriptorPool);

			data_index++;
		}

		// use the same vectors for all meshes so that the memory doesnt reallocate as often
		std::vector<uint32_t> indices;
		std::vector<Vertex> vertices;

		for (fastgltf::Mesh& mesh : gltf.meshes)
		{
			std::shared_ptr<MeshAsset> newmesh = std::make_shared<MeshAsset>();
			meshes.push_back(newmesh);
			file.meshes[mesh.name.c_str()] = newmesh;
			newmesh->name = mesh.name;

			// clear the mesh arrays each mesh, we dont want to merge them by error
			indices.clear();
			vertices.clear();

			for (auto&& p : mesh.primitives)
			{
				SubMesh subMesh;
				subMesh.startIndex = (uint32_t)indices.size();
				subMesh.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

				size_t initial_vtx = vertices.size();

				// load indexes
				{
					fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
					indices.reserve(indices.size() + indexaccessor.count);

					fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor, [&](std::uint32_t idx)
						{
							indices.push_back(idx + (uint32_t)initial_vtx);
						});
				}

				// load vertex positions
				{
					fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
					vertices.resize(vertices.size() + posAccessor.count);

					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](glm::vec3 v, size_t index)
						{
							Vertex newvtx;
							newvtx.position = v;
							newvtx.normal = { 1, 0, 0 };
							newvtx.color = glm::vec4{ 1.f };
							newvtx.uv_x = 0;
							newvtx.uv_y = 0;
							vertices[initial_vtx + index] = newvtx;
						});
				}

				// load vertex normals
				auto normals = p.findAttribute("NORMAL");
				if (normals != p.attributes.end())
				{
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second], [&](glm::vec3 v, size_t index)
						{
							vertices[initial_vtx + index].normal = v;
						});
				}

				// load UVs
				auto uv = p.findAttribute("TEXCOORD_0");
				if (uv != p.attributes.end())
				{
					fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second], [&](glm::vec2 v, size_t index)
						{
							vertices[initial_vtx + index].uv_x = v.x;
							vertices[initial_vtx + index].uv_y = v.y;
						});
				}

				// load vertex colors
				auto colors = p.findAttribute("COLOR_0");
				if (colors != p.attributes.end())
				{
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second], [&](glm::vec4 v, size_t index)
						{
							vertices[initial_vtx + index].color = v;
						});
				}

				if (p.materialIndex.has_value())
				{
					subMesh.material = materials[p.materialIndex.value()];
				}
				else
				{
					subMesh.material = materials[0];
				}

				//calculate bounds
				glm::vec3 minpos = vertices[initial_vtx].position;
				glm::vec3 maxpos = vertices[initial_vtx].position;
				for (int i = initial_vtx; i < vertices.size(); i++)
				{
					minpos = glm::min(minpos, vertices[i].position);
					maxpos = glm::max(maxpos, vertices[i].position);
				}
				subMesh.bounds.origin = (maxpos + minpos) / 2.f;
				subMesh.bounds.extents = (maxpos - minpos) / 2.f;
				subMesh.bounds.sphereRadius = glm::length(subMesh.bounds.extents);

				newmesh->surfaces.push_back(subMesh);
			}

			newmesh->meshBuffers = engine->uploadMesh(indices, vertices);
		}

		// load all nodes and their meshes
		for (fastgltf::Node& node : gltf.nodes)
		{
			std::shared_ptr<Node> newNode;

			// find if the node has a mesh, and if it does hook it to the mesh pointer and allocate it with the meshnode class
			if (node.meshIndex.has_value())
			{
				newNode = std::make_shared<MeshNode>();
				static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
			}
			else
			{
				newNode = std::make_shared<Node>();
			}

			nodes.push_back(newNode);
			file.nodes[node.name.c_str()];

			std::visit(fastgltf::visitor
				{ 
					[&](fastgltf::Node::TransformMatrix matrix) 
					{
						memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));								  
					},
					[&](fastgltf::Node::TRS transform)
					{
						glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
						glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
						glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

						glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
						glm::mat4 rm = glm::toMat4(rot);
						glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

						newNode->localTransform = tm * rm * sm;
					} 
				},
				node.transform);
		}

		// run loop again to setup transform hierarchy
		for (int i = 0; i < gltf.nodes.size(); i++)
		{
			fastgltf::Node& node = gltf.nodes[i];
			std::shared_ptr<Node>& sceneNode = nodes[i];

			for (auto& c : node.children)
			{
				sceneNode->children.push_back(nodes[c]);
				nodes[c]->parent = sceneNode;
			}
		}

		// find the top nodes, with no parents
		for (auto& node : nodes)
		{
			if (node->parent.lock() == nullptr)
			{
				file.topNodes.push_back(node);
				node->refreshTransform(glm::mat4{ 1.f });
			}
		}

		return scene;
	}
}
