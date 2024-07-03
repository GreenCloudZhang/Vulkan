/*
* Vulkan Example - Cube map texture loading and displaying
*
* This sample shows how to load and render a cubemap. A cubemap is a textures that contains 6 images, one per cube face.
* The sample displays the cubemap as a skybox (background) and as a reflection on a selectable object
* 
* Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include <ktx.h>
#include <ktxvulkan.h>
#include <cstdlib>
#include <ctime>

class VulkanExample : public VulkanExampleBase
{
public:
	bool displaySkybox = true;

	int shBand = 4;
	int shSamples = 8096;

	bool dedicatedComputeQueue = false;
	bool firstDraw = true;

	vks::Texture cubeMap;

	struct Models {
		vkglTF::Model skybox;
		// The sample lets you select different models to apply the cubemap to
		std::vector<vkglTF::Model> objects;
		vkglTF::Model sphere;
		int32_t objectIndex = 0;
	} models;

	struct UBOVS {
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::mat4 inverseModelview;
		float lodBias = 0.0f;
	} uboVS;
	vks::Buffer uniformBuffer;
	struct UBOVSPS {
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::mat4 inverseModelview;
		glm::vec4 controlP;//x:band y:sampleNum
	} uboVSPSRebuild;
	vks::Buffer uniformBufferRebuild;

	struct {
		VkPipeline skybox{ VK_NULL_HANDLE };
		VkPipeline reflect{ VK_NULL_HANDLE };
		VkPipeline shRebuild{ VK_NULL_HANDLE };
	} pipelines;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayoutRebuild{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSetRebuild{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayoutRebuild{ VK_NULL_HANDLE };

	std::vector<std::string> objectNames;

	vks::Buffer coeffStorageBuffer;
	//SH project
	struct Compute {
	    VkSemaphore semaphore{ VK_NULL_HANDLE };
		VkQueue queue{ VK_NULL_HANDLE };
		VkCommandPool commandPool{ VK_NULL_HANDLE };
		VkCommandBuffer commandBuffer;
		VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
		VkDescriptorSet descriptorSets{ VK_NULL_HANDLE };
		VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
		VkPipeline pipeline{ VK_NULL_HANDLE };
		struct UniformData {
			glm::vec4 random[4048];//max 40000 sample;
			glm::vec4 comtrolP;//x:band y:sampleNum
		} uniformData;
		vks::Buffer uniformBuffer;
	} compute;

	VulkanExample() : VulkanExampleBase()
	{
		title = "Cube map textures";
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -4.0f));
		camera.setRotation(glm::vec3(0.0f));
		camera.setRotationSpeed(0.25f);
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyImageView(device, cubeMap.view, nullptr);
			vkDestroyImage(device, cubeMap.image, nullptr);
			vkDestroySampler(device, cubeMap.sampler, nullptr);
			vkFreeMemory(device, cubeMap.deviceMemory, nullptr);

			vkDestroyPipeline(device, pipelines.skybox, nullptr);
			vkDestroyPipeline(device, pipelines.reflect, nullptr);
			vkDestroyPipeline(device, pipelines.shRebuild, nullptr);
			vkDestroyPipeline(device, compute.pipeline, nullptr);

			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayoutRebuild, nullptr);
			vkDestroyPipelineLayout(device, compute.pipelineLayout, nullptr);

			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayoutRebuild, nullptr);
			vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);

			vkDestroySemaphore(device, compute.semaphore, nullptr);
			vkDestroyCommandPool(device, compute.commandPool, nullptr);

			uniformBuffer.destroy();
			uniformBufferRebuild.destroy();
			coeffStorageBuffer.destroy();
			compute.uniformBuffer.destroy();
		}
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	}

	// Loads a cubemap from a file, uploads it to the device and create all Vulkan resources required to display it
	void loadCubemap(std::string filename, VkFormat format)
	{
		ktxResult result;
		ktxTexture* ktxTexture;

#if defined(__ANDROID__)
		// Textures are stored inside the apk on Android (compressed)
		// So they need to be loaded via the asset manager
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		if (!asset) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
		}
		size_t size = AAsset_getLength(asset);
		assert(size > 0);

		ktx_uint8_t *textureData = new ktx_uint8_t[size];
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);
		result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		delete[] textureData;
#else
		if (!vks::tools::fileExists(filename)) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
		}
		result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif
		assert(result == KTX_SUCCESS);

		// Get properties required for using and upload texture data from the ktx texture object
		cubeMap.width = ktxTexture->baseWidth;
		cubeMap.height = ktxTexture->baseHeight;
		cubeMap.mipLevels = ktxTexture->numLevels;
		ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		// Create a host-visible staging buffer that contains the raw image data
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
		bufferCreateInfo.size = ktxTextureSize;
		// This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

		// Get memory requirements for the staging buffer (alignment, memory type bits)
		vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		// Get memory type index for a host visible buffer
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

		// Copy texture data into staging buffer
		uint8_t *data;
		VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, memReqs.size, 0, (void **)&data));
		memcpy(data, ktxTextureData, ktxTextureSize);
		vkUnmapMemory(device, stagingMemory);

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = cubeMap.mipLevels;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { cubeMap.width, cubeMap.height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		// Cube faces count as array layers in Vulkan
		imageCreateInfo.arrayLayers = 6;
		// This flag is required for cube map images
		imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &cubeMap.image));

		vkGetImageMemoryRequirements(device, cubeMap.image, &memReqs);

		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubeMap.deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, cubeMap.image, cubeMap.deviceMemory, 0));

		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Setup buffer copy regions for each face including all of its miplevels
		std::vector<VkBufferImageCopy> bufferCopyRegions;
		uint32_t offset = 0;

		for (uint32_t face = 0; face < 6; face++)
		{
			for (uint32_t level = 0; level < cubeMap.mipLevels; level++)
			{
				// Calculate offset into staging buffer for the current mip level and face
				ktx_size_t offset;
				KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, level, 0, face, &offset);
				assert(ret == KTX_SUCCESS);
				VkBufferImageCopy bufferCopyRegion = {};
				bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bufferCopyRegion.imageSubresource.mipLevel = level;
				bufferCopyRegion.imageSubresource.baseArrayLayer = face;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> level;
				bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> level;
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;
				bufferCopyRegions.push_back(bufferCopyRegion);
			}
		}

		// Image barrier for optimal image (target)
		// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = cubeMap.mipLevels;
		subresourceRange.layerCount = 6;

		vks::tools::setImageLayout(
			copyCmd,
			cubeMap.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			subresourceRange);

		// Copy the cube map faces from the staging buffer to the optimal tiled image
		vkCmdCopyBufferToImage(
			copyCmd,
			stagingBuffer,
			cubeMap.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(bufferCopyRegions.size()),
			bufferCopyRegions.data()
			);

		// Change texture image layout to shader read after all faces have been copied
		cubeMap.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vks::tools::setImageLayout(
			copyCmd,
			cubeMap.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			cubeMap.imageLayout,
			subresourceRange);

		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

		// Create sampler
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = static_cast<float>(cubeMap.mipLevels);
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		sampler.maxAnisotropy = 1.0f;
		if (vulkanDevice->features.samplerAnisotropy)
		{
			sampler.maxAnisotropy = vulkanDevice->properties.limits.maxSamplerAnisotropy;
			sampler.anisotropyEnable = VK_TRUE;
		}
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &cubeMap.sampler));

		// Create image view
		VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
		// Cube map view type
		view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		view.format = format;
		view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		// 6 array layers (faces)
		view.subresourceRange.layerCount = 6;
		// Set number of mip levels
		view.subresourceRange.levelCount = cubeMap.mipLevels;
		view.image = cubeMap.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &cubeMap.view));

		// Clean up staging resources
		vkFreeMemory(device, stagingMemory, nullptr);
		vkDestroyBuffer(device, stagingBuffer, nullptr);
		ktxTexture_Destroy(ktxTexture);
	}

	void addGraphicsToComputeBarriers(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
	{
		if (dedicatedComputeQueue) {
			VkBufferMemoryBarrier bufferBarrier = vks::initializers::bufferMemoryBarrier();
			bufferBarrier.srcAccessMask = srcAccessMask;
			bufferBarrier.dstAccessMask = dstAccessMask;
			bufferBarrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
			bufferBarrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
			bufferBarrier.size = VK_WHOLE_SIZE;

			std::vector<VkBufferMemoryBarrier> bufferBarriers;
			bufferBarrier.buffer = coeffStorageBuffer.buffer;
			bufferBarriers.push_back(bufferBarrier);
			vkCmdPipelineBarrier(commandBuffer,
				srcStageMask,
				dstStageMask,
				VK_FLAGS_NONE,
				0, nullptr,
				static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
				0, nullptr);
		}
	}

	void addComputeToGraphicsBarriers(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask)
	{
		if (dedicatedComputeQueue) {
			VkBufferMemoryBarrier bufferBarrier = vks::initializers::bufferMemoryBarrier();
			bufferBarrier.srcAccessMask = srcAccessMask;
			bufferBarrier.dstAccessMask = dstAccessMask;
			bufferBarrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
			bufferBarrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
			bufferBarrier.size = VK_WHOLE_SIZE;
			std::vector<VkBufferMemoryBarrier> bufferBarriers;
			bufferBarrier.buffer = coeffStorageBuffer.buffer;
			bufferBarriers.push_back(bufferBarrier);
			vkCmdPipelineBarrier(
				commandBuffer,
				srcStageMask,
				dstStageMask,
				VK_FLAGS_NONE,
				0, nullptr,
				static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
				0, nullptr);
		}
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			//TODO
			//add barrier

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width,	(float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width,	height,	0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

			// Skybox
			if (displaySkybox)
			{
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
				models.skybox.draw(drawCmdBuffers[i]);
			}

			// 3D object
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.reflect);
			models.objects[models.objectIndex].draw(drawCmdBuffers[i]);

			// SH rebuild
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayoutRebuild, 0, 1, &descriptorSetRebuild, 0, nullptr);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.shRebuild);
			models.sphere.draw(drawCmdBuffers[i]);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			//add barrier

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
		uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY;
		// Skybox
		models.skybox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		// Objects
		std::vector<std::string> filenames = { "sphere.gltf", "teapot.gltf", "torusknot.gltf", "venus.gltf" };
		objectNames = { "Sphere", "Teapot", "Torusknot", "Venus" };
		models.objects.resize(filenames.size());
		for (size_t i = 0; i < filenames.size(); i++) {
			models.objects[i].loadFromFile(getAssetPath() + "models/" + filenames[i], vulkanDevice, queue, glTFLoadingFlags);
		}
		models.sphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
		// Cubemap texture
		loadCubemap(getAssetPath() + "textures/cubemap_6colorfaces.ktx", VK_FORMAT_R8G8B8A8_UNORM);
		//loadCubemap(getAssetPath() + "textures/cubemap_yokohama_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM);
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 4);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Graphic Layout1
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			// Binding 1 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Set1
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		// Image descriptor for the cube map texture
		VkDescriptorImageInfo textureDescriptor = vks::initializers::descriptorImageInfo(cubeMap.sampler, cubeMap.view, cubeMap.imageLayout);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor),
			// Binding 1 : Fragment shader cubemap sampler
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textureDescriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		//Graphic Layout2
		setLayoutBindings = {
			// Binding 0 : Vertex/fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			// Binding 1 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2 : Fragment shader storage buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
		};
        descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayoutRebuild));

		// Set2
		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayoutRebuild, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSetRebuild));

		writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSetRebuild, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBufferRebuild.descriptor),
			// Binding 1 : Fragment shader cubemap sampler
			vks::initializers::writeDescriptorSet(descriptorSetRebuild, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textureDescriptor),
			// Binding 2 : Fragment shader STORAGE buffer
			vks::initializers::writeDescriptorSet(descriptorSetRebuild, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &coeffStorageBuffer.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);


		//Compute Layout
		setLayoutBindings = {
			// Binding 0 : compute shader storage buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			// Binding 1 : compute shader cubemap
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
			// Binding 2 : compute shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2)
		};
		descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &compute.descriptorSetLayout));

		// Set3
		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &compute.descriptorSets));

		writeDescriptorSets =
		{
			// Binding 0 : storage buffer
			vks::initializers::writeDescriptorSet(compute.descriptorSets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &coeffStorageBuffer.descriptor),
			// Binding 1 : cubemap sampler
			vks::initializers::writeDescriptorSet(compute.descriptorSets, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textureDescriptor),
			// Binding 2 : UNIFORM buffer
			vks::initializers::writeDescriptorSet(compute.descriptorSets, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &compute.uniformBuffer.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	//void preparePipelines()
	//{
	//	// Layout
	//	const VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
	//	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

	//	// Pipeline
	//	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
	//	VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
	//	VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
	//	VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
	//	VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
	//	VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
	//	VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
	//	std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	//	VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
	//	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	//	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
	//	pipelineCI.pInputAssemblyState = &inputAssemblyState;
	//	pipelineCI.pRasterizationState = &rasterizationState;
	//	pipelineCI.pColorBlendState = &colorBlendState;
	//	pipelineCI.pMultisampleState = &multisampleState;
	//	pipelineCI.pViewportState = &viewportState;
	//	pipelineCI.pDepthStencilState = &depthStencilState;
	//	pipelineCI.pDynamicState = &dynamicState;
	//	pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	//	pipelineCI.pStages = shaderStages.data();
	//	pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal });

	//	// Skybox pipeline (background cube)
	//	shaderStages[0] = loadShader(getShadersPath() + "sphericalHarmonic/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	//	shaderStages[1] = loadShader(getShadersPath() + "sphericalHarmonic/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	//	rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
	//	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));

	//	// Cube map reflect pipeline
	//	shaderStages[0] = loadShader(getShadersPath() + "sphericalHarmonic/reflect.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	//	shaderStages[1] = loadShader(getShadersPath() + "sphericalHarmonic/reflect.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	//	// Enable depth test and write
	//	depthStencilState.depthWriteEnable = VK_TRUE;
	//	depthStencilState.depthTestEnable = VK_TRUE;
	//	rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	//	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.reflect));
	//}

	void prepareGraphics()
	{
		// Layout
		const VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal });

		// Skybox pipeline (background cube)
		shaderStages[0] = loadShader(getShadersPath() + "sphericalHarmonic/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "sphericalHarmonic/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));

		// Cube map reflect pipeline
		shaderStages[0] = loadShader(getShadersPath() + "sphericalHarmonic/reflect.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "sphericalHarmonic/reflect.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Enable depth test and write
		depthStencilState.depthWriteEnable = VK_TRUE;
		depthStencilState.depthTestEnable = VK_TRUE;
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.reflect));

		const VkPipelineLayoutCreateInfo pipelineLayoutRebuildCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayoutRebuild, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutRebuildCI, nullptr, &pipelineLayoutRebuild));
		pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayoutRebuild, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal });
		shaderStages[0] = loadShader(getShadersPath() + "sphericalHarmonic/reflectSHbuild.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "sphericalHarmonic/reflectSHbuild.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.shRebuild));

		buildCommandBuffers();
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(uboVS)));
		VK_CHECK_RESULT(uniformBuffer.map());
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBufferRebuild, sizeof(uboVSPSRebuild)));
		VK_CHECK_RESULT(uniformBufferRebuild.map());
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &compute.uniformBuffer, sizeof(compute.uniformData)));
		VK_CHECK_RESULT(compute.uniformBuffer.map());
		updateUniformBuffers();
	}

	void prepareStorageBuffers()
	{
		VkDeviceSize storageBufferSize = shBand * shBand * sizeof(glm::vec4);
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&coeffStorageBuffer,
			storageBufferSize);
	}

	void updateUniformBuffers()
	{
		uboVS.projection = camera.matrices.perspective;
		// Note: Both the object and skybox use the same uniform data, the translation part of the skybox is removed in the shader (see skybox.vert)
		uboVS.modelView = camera.matrices.view;
		uboVS.inverseModelview = glm::inverse(camera.matrices.view);
		memcpy(uniformBuffer.mapped, &uboVS, sizeof(uboVS));

		glm::mat4 m = glm::mat4(1);
		m = glm::translate(m, glm::vec3(2,0,0));
		uboVSPSRebuild.projection = uboVS.projection;
		uboVSPSRebuild.modelView = uboVS.modelView * m;
		uboVSPSRebuild.inverseModelview = glm::inverse(uboVSPSRebuild.modelView);
		uboVSPSRebuild.controlP = glm::vec4(shBand, shSamples, 0.f, 0.f);
		memcpy(uniformBufferRebuild.mapped, &uboVSPSRebuild, sizeof(uboVSPSRebuild));

		//compute pass uniform buffer
		if (firstDraw)
		{
			compute.uniformData.comtrolP = glm::vec4(shBand, shSamples, 0, 0);
			srand((unsigned)time(0));
			int sqrtSHSamples = std::sqrt(shSamples);
			float oneovern = 1.f / sqrtSHSamples;
			for (int a = 0; a < sqrtSHSamples; a++)
			{
				for (int b = 0; b < sqrtSHSamples; b++)
				{
					int index = a * sqrtSHSamples + b;
					int bufferId = index / 2;
					float r1 = (a + (rand() / float(RAND_MAX))) * oneovern;
					float r2 = (b + (rand() / float(RAND_MAX))) * oneovern;
					if (index % 2 == 0)//xy
					{
						compute.uniformData.random[bufferId].x = r1;
						compute.uniformData.random[bufferId].y = r2;
					}
					else//zw
					{
						compute.uniformData.random[bufferId].z = r1;
						compute.uniformData.random[bufferId].w = r2;
					}
				}
			}
			memcpy(compute.uniformBuffer.mapped, &compute.uniformData, sizeof(compute.uniformData));
			firstDraw = false;
		}
	}

	void prepareCompute()
	{
		// Layout
		const VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &compute.pipelineLayout));

		vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.compute, 0, &compute.queue);
		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(compute.pipelineLayout, 0);
		computePipelineCreateInfo.stage = loadShader(getShadersPath() + "sphericalHarmonic/shProject.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &compute.pipeline));

		VkCommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &compute.commandPool));

		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(compute.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &compute.commandBuffer));

		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &compute.semaphore));

		//#####build compute command buffer#####
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		VK_CHECK_RESULT(vkBeginCommandBuffer(compute.commandBuffer, &cmdBufInfo));

		//add barrier

		vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
		vkCmdBindDescriptorSets(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSets, 0, 0);
		int coeffCount = shBand * shBand;
		int threadx = 16;
		vkCmdDispatch(compute.commandBuffer, (coeffCount - 1 + threadx) / threadx, 1, 1);
		//add barrier
		vkEndCommandBuffer(compute.commandBuffer);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		dedicatedComputeQueue = vulkanDevice->queueFamilyIndices.graphics != vulkanDevice->queueFamilyIndices.compute;
		loadAssets();
		prepareUniformBuffers();
		prepareStorageBuffers();
		setupDescriptors();
		prepareGraphics();
		prepareCompute();
		//preparePipelines();
		//buildCommandBuffers();
		prepared = true;
	}

	void draw()
	{
		//VulkanExampleBase::prepareFrame();
		//submitInfo.commandBufferCount = 1;
		//submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		//VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		//VulkanExampleBase::submitFrame();

		//##############
		VkSubmitInfo computeSubmitInfo = vks::initializers::submitInfo();
		VkPipelineStageFlags computeWaitDstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		computeSubmitInfo.signalSemaphoreCount = 1;
		computeSubmitInfo.pSignalSemaphores = &compute.semaphore;
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;

		VK_CHECK_RESULT(vkQueueSubmit(compute.queue, 1, &computeSubmitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::prepareFrame();
		VkPipelineStageFlags waitDstStageMask[2] = { submitPipelineStages, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT };
		VkSemaphore waitSemaphore[2] = { semaphores.presentComplete, compute.semaphore };
		VkSemaphore signalSemaphore[1] = { semaphores.renderComplete };
		submitInfo.waitSemaphoreCount = 2;
		submitInfo.pWaitDstStageMask = waitDstStageMask;
		submitInfo.pWaitSemaphores = waitSemaphore;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}
	virtual void render()
	{
		if (!prepared)
			return;
		updateUniformBuffers();
		draw();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			if (overlay->sliderFloat("LOD bias", &uboVS.lodBias, 0.0f, (float)cubeMap.mipLevels)) {
				updateUniformBuffers();
			}
			if (overlay->comboBox("Object type", &models.objectIndex, objectNames)) {
				buildCommandBuffers();
			}
			if (overlay->checkBox("Skybox", &displaySkybox)) {
				buildCommandBuffers();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()