/*
* Vulkan Example - Deferred shading with multiple render targets (aka G-Buffer) example
*
* This samples shows how to do deferred rendering. Unlike forward rendering, different components like
* albedo, normals, world positions are rendered to offscreen images which are then put together and lit
* in a composition pass
* Use the dropdown in the ui to switch between the final composition pass or the separate components
* 
* 
* Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include <vector>
#include <algorithm>
#include <iterator>
#define samplerSteps 25
//8*4=32 samples
#define ArrayCount 8

//model
//composite1
//scattering x
//scattering y
//composite2

class VulkanExample : public VulkanExampleBase
{
public:
	int32_t debugDisplayTarget = 0;

	struct {
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} model;
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} floor;
	} textures;

	struct {
		vkglTF::Model model;
		vkglTF::Model floor;
	} models;

	struct UniformDataOffscreen  {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec4 instancePos[3];
	} uniformDataOffscreen;

	struct Light {
		glm::vec4 position;
		glm::vec3 color;
		float radius;
	};

	struct UniformDataComposition {
		Light lights[6];
		glm::vec4 viewPos;
		int debugDisplayTarget = 0;
	} uniformDataComposition;

	struct SeparableSSSParams
	{
		glm::vec3 subsurfaceColor =glm::vec3(0.8396226, 0.1069331, 0.1069331);
		glm::vec3 subsurfaceFalloff = glm::vec3(0.754717, 0.3402277, 0.05);
		float strength = 1.f;
	}separableSSSParams;

	bool burleySSS = false;//false is ssss

	struct UniformDataScatteringSSSS {
		glm::vec4 _ScreenSubsurfaceProps;
		glm::vec4 _DepthTexelSize = glm::vec4{ 1.f / 2048.f, 1.f / 2048.f, 2048.f, 2048.f};
		glm::vec4 _ZNear_Far;
		glm::vec4 _ScreenSubsurfaceKernel[samplerSteps];
	} uniformDataScatteringSSSS;


	struct SeparableBurleySSSParams
	{
		glm::vec3 shapeParams = glm::vec3(0.026, 0.011, 0.006);
		float worldScale = 1.f;
	}separableBurleySSSParams;
	struct UniformDataScatteringBurleySSS
	{
		glm::mat4 invProjMat;
		glm::vec4 _DepthTexelSize = glm::vec4{ 1.f / 2048.f, 1.f / 2048.f, 2048.f, 2048.f };//1/width,1/height,width,height
		glm::vec4 _BurleySubsurfaceParams;
		glm::vec4 _ShapeParamsAndMaxScatterDists;//s d
		glm::vec4 _ZNear_Far_zBufferParams;//near, far-near, zbuffer.x, zbuffer.y
		glm::vec4 _Sample_r[ArrayCount];
		glm::vec4 _Sample_rcpPdf[ArrayCount];
		glm::vec4 _Sample_sinPhi[ArrayCount];
		glm::vec4 _Sample_cosPhi[ArrayCount];
	}uniformDataScatteringBurleySSS;


	struct {
		vks::Buffer offscreen{ VK_NULL_HANDLE };
		vks::Buffer scattering{ VK_NULL_HANDLE };
		vks::Buffer composition{ VK_NULL_HANDLE };
	} uniformBuffers;

	struct {
		VkPipeline offscreen{ VK_NULL_HANDLE };
		VkPipeline composition1{ VK_NULL_HANDLE };
		VkPipeline scatteringX{ VK_NULL_HANDLE };
		VkPipeline scatteringY{ VK_NULL_HANDLE };
		VkPipeline scatteringBurley{ VK_NULL_HANDLE };
		VkPipeline composition2{ VK_NULL_HANDLE };
	} pipelines;
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };

	struct {
		VkDescriptorSet model{ VK_NULL_HANDLE };
		VkDescriptorSet floor{ VK_NULL_HANDLE };
		VkDescriptorSet composition1{ VK_NULL_HANDLE };
		VkDescriptorSet scatteringX{ VK_NULL_HANDLE };// == burleySSS
		VkDescriptorSet scatteringY{ VK_NULL_HANDLE };
		//VkDescriptorSet scatteringBurley{ VK_NULL_HANDLE };
		VkDescriptorSet composition2{ VK_NULL_HANDLE };
	} descriptorSets;

	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

	// Framebuffers holding the deferred attachments
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkFormat format;
	};
	VkImageView depthReadview;
	struct FrameBuffer {
		int32_t width, height;
		VkFramebuffer frameBuffer1;//DEFER
		VkFramebuffer frameBuffer2;//DIFFUSE+SPECULAR
		VkFramebuffer frameBuffer3;//blurX
		VkFramebuffer frameBuffer4;//blurY
		// One attachment for every component required for a deferred rendering setup
		FrameBufferAttachment position, normal, albedo;
		FrameBufferAttachment depth;
		FrameBufferAttachment depth2;
		FrameBufferAttachment depth3;
		FrameBufferAttachment diffuse, specular;
		FrameBufferAttachment blurTempX;
		FrameBufferAttachment blurTempY;
		VkRenderPass renderPass1;//POS NORMAL 
		VkRenderPass renderPass2;//DEFER RENDER
		VkRenderPass renderPass3;//BLUR
	} offScreenFrameBuf{};


	// One sampler for the frame buffer color attachments
	VkSampler colorSampler{ VK_NULL_HANDLE };

	VkCommandBuffer offScreenCmdBuffer{ VK_NULL_HANDLE };
	VkCommandBuffer shadingCmdBuffer{ VK_NULL_HANDLE };

	// Semaphore used to synchronize between offscreen and final scene rendering
	VkSemaphore offscreenSemaphore{ VK_NULL_HANDLE };
	VkSemaphore shadingSemaphore{ VK_NULL_HANDLE };

	VulkanExample() : VulkanExampleBase()
	{
		title = "Deferred shading";
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 2.15f, 0.3f, -8.75f };
		camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroySampler(device, colorSampler, nullptr);

			// Frame buffer

			// Color attachments
			vkDestroyImageView(device, offScreenFrameBuf.position.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.position.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.position.mem, nullptr);

			vkDestroyImageView(device, offScreenFrameBuf.normal.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.normal.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.normal.mem, nullptr);

			vkDestroyImageView(device, offScreenFrameBuf.albedo.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.albedo.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.albedo.mem, nullptr);

			vkDestroyImageView(device, offScreenFrameBuf.diffuse.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.diffuse.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.diffuse.mem, nullptr);

			vkDestroyImageView(device, offScreenFrameBuf.specular.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.specular.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.specular.mem, nullptr);

			vkDestroyImageView(device, offScreenFrameBuf.blurTempX.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.blurTempX.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.blurTempX.mem, nullptr);

			vkDestroyImageView(device, offScreenFrameBuf.blurTempY.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.blurTempY.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.blurTempY.mem, nullptr);

			// Depth attachment
			vkDestroyImageView(device, offScreenFrameBuf.depth.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.depth.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.depth.mem, nullptr);
			vkDestroyImageView(device, offScreenFrameBuf.depth2.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.depth2.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.depth2.mem, nullptr);
			vkDestroyImageView(device, offScreenFrameBuf.depth3.view, nullptr);
			vkDestroyImage(device, offScreenFrameBuf.depth3.image, nullptr);
			vkFreeMemory(device, offScreenFrameBuf.depth3.mem, nullptr);

			vkDestroyFramebuffer(device, offScreenFrameBuf.frameBuffer1, nullptr);
			vkDestroyFramebuffer(device, offScreenFrameBuf.frameBuffer2, nullptr);
			vkDestroyFramebuffer(device, offScreenFrameBuf.frameBuffer3, nullptr);
			vkDestroyFramebuffer(device, offScreenFrameBuf.frameBuffer4, nullptr);

			vkDestroyPipeline(device, pipelines.composition1, nullptr);
			vkDestroyPipeline(device, pipelines.offscreen, nullptr);
			vkDestroyPipeline(device, pipelines.scatteringX, nullptr);
			vkDestroyPipeline(device, pipelines.scatteringY, nullptr);
			vkDestroyPipeline(device, pipelines.scatteringBurley, nullptr);
			vkDestroyPipeline(device, pipelines.composition2, nullptr);

			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

			// Uniform buffers
			uniformBuffers.offscreen.destroy();
			uniformBuffers.composition.destroy();
			uniformBuffers.scattering.destroy();

			vkDestroyRenderPass(device, offScreenFrameBuf.renderPass1, nullptr);
			vkDestroyRenderPass(device, offScreenFrameBuf.renderPass2, nullptr);
			vkDestroyRenderPass(device, offScreenFrameBuf.renderPass3, nullptr);

			textures.model.colorMap.destroy();
			textures.model.normalMap.destroy();
			textures.floor.colorMap.destroy();
			textures.floor.normalMap.destroy();

			vkDestroySemaphore(device, offscreenSemaphore, nullptr);
		}
	}

	bool hasDepth(FrameBufferAttachment* attachment)
	{
		std::vector<VkFormat> formats = {
			VK_FORMAT_D16_UNORM,
			VK_FORMAT_X8_D24_UNORM_PACK32,
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D16_UNORM_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
		};
		return std::find(formats.begin(), formats.end(), attachment->format) != formats.end();
	}

	bool hasStencil(FrameBufferAttachment* attachment)
	{
		std::vector<VkFormat> formats = {
			VK_FORMAT_S8_UINT,
			VK_FORMAT_D16_UNORM_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
		};
		return std::find(formats.begin(), formats.end(), attachment->format) != formats.end();
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	};

	// Create a frame buffer attachment
	void createAttachment(
		VkFormat format,
		VkImageUsageFlagBits usage,
		FrameBufferAttachment *attachment)
	{
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment->format = format;

		VkImageUsageFlags imageUsages;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imageUsages = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (format >= VK_FORMAT_D16_UNORM_S8_UINT)
				aspectMask |=VK_IMAGE_ASPECT_STENCIL_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			imageUsages = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}
		assert(aspectMask > 0);


		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = offScreenFrameBuf.width;
		image.extent.height = offScreenFrameBuf.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = imageUsages | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->mem, 0));


		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = hasDepth(attachment) ? VK_IMAGE_ASPECT_DEPTH_BIT : aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	// Prepare a new framebuffer and attachments for offscreen rendering (G-Buffer)
	void prepareOffscreenFramebuffer()
	{
		// Note: Instead of using fixed sizes, one could also match the window size and recreate the attachments on resize
		offScreenFrameBuf.width = 2048;
		offScreenFrameBuf.height = 2048;

		// Color attachments

		// (World space) Positions
		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&offScreenFrameBuf.position);

		// (World space) Normals
		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&offScreenFrameBuf.normal);

		// Albedo (color)
		createAttachment(
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&offScreenFrameBuf.albedo);

		// Depth attachment

		// Find a suitable depth format
		VkFormat attDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &attDepthFormat);
		assert(validDepthFormat);

		createAttachment(
			attDepthFormat,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			&offScreenFrameBuf.depth);

		// Set up separate renderpass with references to the color and depth attachments
		std::array<VkAttachmentDescription, 4> attachmentDescs = {};

		// Init attachment properties
		for (uint32_t i = 0; i < 4; ++i)
		{
			attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (i == 3)
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			else
			{
				attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}

		// Formats
		attachmentDescs[0].format = offScreenFrameBuf.position.format;
		attachmentDescs[1].format = offScreenFrameBuf.normal.format;
		attachmentDescs[2].format = offScreenFrameBuf.albedo.format;
		attachmentDescs[3].format = offScreenFrameBuf.depth.format;

		std::vector<VkAttachmentReference> colorReferences;
		colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 3;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.pColorAttachments = colorReferences.data();
		subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpass.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for attachment layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.pAttachments = attachmentDescs.data();
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &offScreenFrameBuf.renderPass1));

		std::array<VkImageView,4> attachments;
		attachments[0] = offScreenFrameBuf.position.view;
		attachments[1] = offScreenFrameBuf.normal.view;
		attachments[2] = offScreenFrameBuf.albedo.view;
		attachments[3] = offScreenFrameBuf.depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = {};
		fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo.pNext = NULL;
		fbufCreateInfo.renderPass = offScreenFrameBuf.renderPass1;
		fbufCreateInfo.pAttachments = attachments.data();
		fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbufCreateInfo.width = offScreenFrameBuf.width;
		fbufCreateInfo.height = offScreenFrameBuf.height;
		fbufCreateInfo.layers = 1;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &offScreenFrameBuf.frameBuffer1));

		//framebuffer2
		//DIFFUSE
		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&offScreenFrameBuf.diffuse);
		//SPECULAR
		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&offScreenFrameBuf.specular);
		createAttachment(
			attDepthFormat,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			&offScreenFrameBuf.depth2);

		std::array<VkAttachmentDescription, 3> attachmentDescs2 = {};
		for (uint32_t i = 0; i < 3; ++i)
		{
			attachmentDescs2[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs2[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs2[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs2[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs2[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (i == 2)
			{
				attachmentDescs2[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs2[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			else
			{
				attachmentDescs2[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs2[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}
		attachmentDescs2[0].format = offScreenFrameBuf.diffuse.format;
		attachmentDescs2[1].format = offScreenFrameBuf.specular.format;
		attachmentDescs2[2].format = offScreenFrameBuf.depth2.format;

		std::vector<VkAttachmentReference> colorReferences2;
		colorReferences2.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences2.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		depthReference.attachment = 2;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass2 = {};
		subpass2.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass2.pColorAttachments = colorReferences2.data();
		subpass2.colorAttachmentCount = static_cast<uint32_t>(colorReferences2.size());
		subpass2.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for attachment layout transitions
		std::array<VkSubpassDependency, 2> dependencies2;

		dependencies2[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies2[0].dstSubpass = 0;
		dependencies2[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies2[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies2[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies2[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies2[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies2[1].srcSubpass = 0;
		dependencies2[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies2[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies2[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies2[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies2[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies2[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo2 = {};
		renderPassInfo2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo2.pAttachments = attachmentDescs2.data();
		renderPassInfo2.attachmentCount = static_cast<uint32_t>(attachmentDescs2.size());
		renderPassInfo2.subpassCount = 1;
		renderPassInfo2.pSubpasses = &subpass2;
		renderPassInfo2.dependencyCount = 2;
		renderPassInfo2.pDependencies = dependencies2.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo2, nullptr, &offScreenFrameBuf.renderPass2));

		std::array<VkImageView, 3> attachments2;
		attachments2[0] = offScreenFrameBuf.diffuse.view;
		attachments2[1] = offScreenFrameBuf.specular.view;
		attachments2[2] = offScreenFrameBuf.depth2.view;

		VkFramebufferCreateInfo fbufCreateInfo2 = {};
		fbufCreateInfo2.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo2.pNext = NULL;
		fbufCreateInfo2.renderPass = offScreenFrameBuf.renderPass2;
		fbufCreateInfo2.pAttachments = attachments2.data();
		fbufCreateInfo2.attachmentCount = static_cast<uint32_t>(attachments2.size());
		fbufCreateInfo2.width = offScreenFrameBuf.width;
		fbufCreateInfo2.height = offScreenFrameBuf.height;
		fbufCreateInfo2.layers = 1;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo2, nullptr, &offScreenFrameBuf.frameBuffer2));


		//BLUR framebuffer
		//DIFFUSE
		createAttachment(
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&offScreenFrameBuf.blurTempX);
		//SPECULAR
		createAttachment(
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&offScreenFrameBuf.blurTempY);
		createAttachment(
			attDepthFormat,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			&offScreenFrameBuf.depth3);
		std::array<VkAttachmentDescription, 2> attachmentDescs3 = {};
		for (uint32_t i = 0; i < 2; ++i)
		{
			attachmentDescs3[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs3[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs3[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs3[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs3[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			if (i == 1)
			{
				attachmentDescs3[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs3[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			else
			{
				attachmentDescs3[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				attachmentDescs3[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}
		attachmentDescs3[0].format = offScreenFrameBuf.blurTempX.format;
		attachmentDescs3[1].format = offScreenFrameBuf.depth3.format;

		std::vector<VkAttachmentReference> colorReferences3;
		colorReferences3.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass3 = {};
		subpass3.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass3.pColorAttachments = colorReferences3.data();
		subpass3.colorAttachmentCount = static_cast<uint32_t>(colorReferences3.size());
		subpass3.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for attachment layout transitions
		std::array<VkSubpassDependency, 2> dependencies3;

		dependencies3[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies3[0].dstSubpass = 0;
		dependencies3[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies3[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies3[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies3[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies3[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies3[1].srcSubpass = 0;
		dependencies3[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies3[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies3[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies3[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies3[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies3[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo3 = {};
		renderPassInfo3.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo3.pAttachments = attachmentDescs3.data();
		renderPassInfo3.attachmentCount = static_cast<uint32_t>(attachmentDescs3.size());
		renderPassInfo3.subpassCount = 1;
		renderPassInfo3.pSubpasses = &subpass3;
		renderPassInfo3.dependencyCount = 2;
		renderPassInfo3.pDependencies = dependencies3.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo3, nullptr, &offScreenFrameBuf.renderPass3));

		//BLURx
		std::array<VkImageView, 2> attachments3;
		attachments3[0] = offScreenFrameBuf.blurTempX.view;
		attachments3[1] = offScreenFrameBuf.depth3.view;

		VkFramebufferCreateInfo fbufCreateInfo3 = {};
		fbufCreateInfo3.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo3.pNext = NULL;
		fbufCreateInfo3.renderPass = offScreenFrameBuf.renderPass3;
		fbufCreateInfo3.pAttachments = attachments3.data();
		fbufCreateInfo3.attachmentCount = static_cast<uint32_t>(attachments3.size());
		fbufCreateInfo3.width = offScreenFrameBuf.width;
		fbufCreateInfo3.height = offScreenFrameBuf.height;
		fbufCreateInfo3.layers = 1;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo3, nullptr, &offScreenFrameBuf.frameBuffer3));//burleySSS only frameBuffer3

		//BLURy
		std::array<VkImageView, 2> attachments4;
		attachments4[0] = offScreenFrameBuf.blurTempY.view;
		attachments4[1] = offScreenFrameBuf.depth3.view;

		VkFramebufferCreateInfo fbufCreateInfo4 = {};
		fbufCreateInfo4.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo4.pNext = NULL;
		fbufCreateInfo4.renderPass = offScreenFrameBuf.renderPass3;
		fbufCreateInfo4.pAttachments = attachments4.data();
		fbufCreateInfo4.attachmentCount = static_cast<uint32_t>(attachments4.size());
		fbufCreateInfo4.width = offScreenFrameBuf.width;
		fbufCreateInfo4.height = offScreenFrameBuf.height;
		fbufCreateInfo4.layers = 1;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo4, nullptr, &offScreenFrameBuf.frameBuffer4));

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_NEAREST;
		sampler.minFilter = VK_FILTER_NEAREST;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &colorSampler));
	}


	//The Same Commandbuffer attachment num and type is same
	// Build command buffer for rendering the scene to the offscreen frame buffer attachments
	void buildDeferredCommandBuffer()
	{
		if (offScreenCmdBuffer == VK_NULL_HANDLE) {
			offScreenCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize offscreen rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &offscreenSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		// Clear values for all attachments written in the fragment shader
		std::array<VkClearValue,4> clearValues;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass =  offScreenFrameBuf.renderPass1;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer1;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(offScreenCmdBuffer, &cmdBufInfo));

		vkCmdBeginRenderPass(offScreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		vkCmdSetViewport(offScreenCmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		vkCmdSetScissor(offScreenCmdBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);

		// Floor
		vkCmdBindDescriptorSets(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.floor, 0, nullptr);
		models.floor.draw(offScreenCmdBuffer);

		// We render multiple instances of a model
		vkCmdBindDescriptorSets(offScreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.model, 0, nullptr);
		models.model.bindBuffers(offScreenCmdBuffer);
		vkCmdDrawIndexed(offScreenCmdBuffer, models.model.indices.count, 3, 0, 0, 0);

		vkCmdEndRenderPass(offScreenCmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(offScreenCmdBuffer));
	}

	
	void buildShadingCommandBuffer()
	{
		if (shadingCmdBuffer == VK_NULL_HANDLE) {
			shadingCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize offscreen rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &shadingSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		// Clear values for all attachments written in the fragment shader
		std::array<VkClearValue, 3> clearValues;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = offScreenFrameBuf.renderPass2;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer2;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(shadingCmdBuffer, &cmdBufInfo));

		vkCmdBeginRenderPass(shadingCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		vkCmdSetViewport(shadingCmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		vkCmdSetScissor(shadingCmdBuffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(shadingCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.composition1, 0, nullptr);
		vkCmdBindPipeline(shadingCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition1);
		vkCmdDraw(shadingCmdBuffer, 3, 1, 0, 0);

		vkCmdEndRenderPass(shadingCmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(shadingCmdBuffer));
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.model.loadFromFile(getAssetPath() + "models/armor/armor.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.floor.loadFromFile(getAssetPath() + "models/deferred_floor.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.model.colorMap.loadFromFile(getAssetPath() + "models/armor/colormap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.model.normalMap.loadFromFile(getAssetPath() + "models/armor/normalmap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.floor.colorMap.loadFromFile(getAssetPath() + "textures/stonefloor01_color_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.floor.normalMap.loadFromFile(getAssetPath() + "textures/stonefloor01_normal_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		std::array<VkClearValue, 2> clearValues;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = offScreenFrameBuf.renderPass3;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer3;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = clearValues.size();
		renderPassBeginInfo.pClearValues = clearValues.data();

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			VkViewport viewport = vks::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			////DEFER offscreen
			//vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);	
			//vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);
			//// Floor
			//vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.floor, 0, nullptr);
			//models.floor.draw(drawCmdBuffers[i]);
			//// We render multiple instances of a model
			//vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.model, 0, nullptr);
			//models.model.bindBuffers(drawCmdBuffers[i]);
			//vkCmdDrawIndexed(drawCmdBuffers[i], models.model.indices.count, 3, 0, 0, 0);
			//vkCmdEndRenderPass(drawCmdBuffers[i]);

			////defer diffuse+specular
			//renderPassBeginInfo.renderPass = offScreenFrameBuf.renderPass2;
			//renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer2;
			//vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			//vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.composition1, 0, nullptr);
			//vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition1);
			//vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
			//vkCmdEndRenderPass(drawCmdBuffers[i]);

			if (burleySSS)
			{
				//blurBurley
				renderPassBeginInfo.renderPass = offScreenFrameBuf.renderPass3;
				renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer3;
				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.scatteringX, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scatteringBurley);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}
			else
			{
				//blurX
				renderPassBeginInfo.renderPass = offScreenFrameBuf.renderPass3;
				renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer3;
				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.scatteringX, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scatteringX);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
				vkCmdEndRenderPass(drawCmdBuffers[i]);

				//blurY
				renderPassBeginInfo.renderPass = offScreenFrameBuf.renderPass3;
				renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer4;
				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.scatteringY, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scatteringY);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			//Final Composite
			renderPassBeginInfo.renderArea.extent.width = width;
			renderPassBeginInfo.renderArea.extent.height = height;
			renderPassBeginInfo.renderPass = renderPass;
			renderPassBeginInfo.framebuffer = frameBuffers[i];
			viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.composition2, 0, nullptr);
   			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition2);			
			// Final composition
			// This is done by simply drawing a full screen quad
			// The fragment shader then combines the deferred attachments into the final image
			// Note: Also used for debug display if debugDisplayTarget > 0
			vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 20),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 6);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			// Binding 1 : Position texture target / Scene colormap
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2 : Normals texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			// Binding 3 : Albedo texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			// Binding 4 : Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));
		
		// Sets
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		// Image descriptors for the offscreen color attachments
		VkDescriptorImageInfo texDescriptorPosition =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.position.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorNormal =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.normal.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorAlbedo =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.albedo.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo texDescriptorDepth =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.depth.view,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo texDescriptorDiffuse =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.diffuse.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo texDescriptorSpecular =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.specular.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo texDescriptorBlurX =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.blurTempX.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo texDescriptorBlurY =
			vks::initializers::descriptorImageInfo(
				colorSampler,
				offScreenFrameBuf.blurTempY.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Deferred composition
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.composition1));
		writeDescriptorSets = {
			// Binding 1 : Position texture target
			vks::initializers::writeDescriptorSet(descriptorSets.composition1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorPosition),
			// Binding 2 : Normals texture target
			vks::initializers::writeDescriptorSet(descriptorSets.composition1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorNormal),
			// Binding 3 : Albedo texture target
			vks::initializers::writeDescriptorSet(descriptorSets.composition1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &texDescriptorAlbedo),
			// Binding 4 : Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.composition1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.composition.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Offscreen (scene)

		// Scatteringx texDescriptorDepth+texDescriptorDiffuse->albedo  ==   ScatteringBurley
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.scatteringX));
		writeDescriptorSets = {
			// Binding 1 : texture target
			vks::initializers::writeDescriptorSet(descriptorSets.scatteringX, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorDepth),
			// Binding 2 : texture target
			vks::initializers::writeDescriptorSet(descriptorSets.scatteringX, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorDiffuse),
			// Binding 4 : Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.scatteringX, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.scattering.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Scatteringy texDescriptorDepth+texDescriptorDiffuse->albedo
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.scatteringY));
		writeDescriptorSets = {
			// Binding 1 : texture target
			vks::initializers::writeDescriptorSet(descriptorSets.scatteringY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorDepth),
			// Binding 2 : texture target
			vks::initializers::writeDescriptorSet(descriptorSets.scatteringY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorBlurX),
			// Binding 4 : Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.scatteringY, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.scattering.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Final composite
		if (burleySSS)
		{
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.composition2));
			writeDescriptorSets = {
				// Binding 1 : Position texture target
				vks::initializers::writeDescriptorSet(descriptorSets.composition2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorBlurX),
				// Binding 2 : Normals texture target
				vks::initializers::writeDescriptorSet(descriptorSets.composition2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorSpecular),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
		else
		{
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.composition2));
			writeDescriptorSets = {
				// Binding 1 : Position texture target
				vks::initializers::writeDescriptorSet(descriptorSets.composition2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorBlurY),
				// Binding 2 : Normals texture target
				vks::initializers::writeDescriptorSet(descriptorSets.composition2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorSpecular),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}

		// Model
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.model));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.model, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.offscreen.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(descriptorSets.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.model.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(descriptorSets.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.model.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Background
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.floor));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.floor, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.offscreen.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(descriptorSets.floor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.floor.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(descriptorSets.floor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.floor.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void preparePipelines()
	{
		// Pipeline layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();


		// Vertex input state from glTF model for pipeline rendering models
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Tangent});
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;

		// Offscreen pipeline
		shaderStages[0] = loadShader(getShadersPath() + "surfacescattering/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "surfacescattering/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		// Separate render pass
		pipelineCI.renderPass = offScreenFrameBuf.renderPass1;

		// Blend attachment states required for all color attachments
		// This is important, as color write mask will otherwise be 0x0 and you
		// won't see anything rendered to the attachment
		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};

		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreen));


		// fullscreen pass  split diffuse and specular pipeline
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		shaderStages[0] = loadShader(getShadersPath() + "surfacescattering/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "surfacescattering/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state, vertices are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.renderPass = offScreenFrameBuf.renderPass2;
		std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachmentStates2 = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates2.size());
		colorBlendState.pAttachments = blendAttachmentStates2.data();
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition1));

		// fullscreen pass  scattering blur pipeline
		shaderStages[0] = loadShader(getShadersPath() + "surfacescattering/surfacescattering.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "surfacescattering/surfacescattering.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state, vertices are generated by the vertex shader
		pipelineCI.renderPass = offScreenFrameBuf.renderPass3;
		std::array<VkPipelineColorBlendAttachmentState, 1> blendAttachmentStates1 = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates1.size());
		colorBlendState.pAttachments = blendAttachmentStates1.data();
		uint32_t blurdirection = 1;//Y
		VkSpecializationMapEntry specializationMapEntry = vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t));
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(1, &specializationMapEntry, sizeof(uint32_t), &blurdirection);
		shaderStages[1].pSpecializationInfo = &specializationInfo;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.scatteringX));
		blurdirection = 0;//X
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.scatteringY));

		//BurleySSS
		shaderStages[0] = loadShader(getShadersPath() + "surfacescattering/burleyscattering.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "surfacescattering/burleyscattering.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.renderPass = offScreenFrameBuf.renderPass3;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.scatteringBurley));

		//Final composite
		shaderStages[0] = loadShader(getShadersPath() + "surfacescattering/composite.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "surfacescattering/composite.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state, vertices are generated by the vertex shader
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition2));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Offscreen vertex shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.offscreen, sizeof(UniformDataOffscreen)));

		// Scattering fragment shader
		if (burleySSS)
		{
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.scattering, sizeof(UniformDataScatteringBurleySSS)));
		}
		else
		{
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.scattering, sizeof(UniformDataScatteringSSSS)));
		}

		// Deferred fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.composition, sizeof(UniformDataComposition)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.offscreen.map());
		VK_CHECK_RESULT(uniformBuffers.scattering.map());
		VK_CHECK_RESULT(uniformBuffers.composition.map());

		// Setup instanced model positions
		uniformDataOffscreen.instancePos[0] = glm::vec4(0.0f);
		uniformDataOffscreen.instancePos[1] = glm::vec4(-4.0f, 0.0, -4.0f, 0.0f);
		uniformDataOffscreen.instancePos[2] = glm::vec4(4.0f, 0.0, -4.0f, 0.0f);

		// Update
		updateUniformBufferOffscreen();
		updateUniformBufferScattering();
		updateUniformBufferComposition();
	}

	// Update matrices used for the offscreen rendering of the scene
	void updateUniformBufferOffscreen()
	{
		uniformDataOffscreen.projection = camera.matrices.perspective;
		uniformDataOffscreen.view = camera.matrices.view;
		uniformDataOffscreen.model = glm::mat4(1.0f);
		memcpy(uniformBuffers.offscreen.mapped, &uniformDataOffscreen, sizeof(UniformDataOffscreen));
	}

	// Update lights and parameters passed to the composition shaders
	//composition1
	void updateUniformBufferComposition()
	{
		// White
		uniformDataComposition.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		uniformDataComposition.lights[0].color = glm::vec3(1.5f);
		uniformDataComposition.lights[0].radius = 15.0f * 0.25f;
		// Red
		uniformDataComposition.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
		uniformDataComposition.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
		uniformDataComposition.lights[1].radius = 15.0f;
		// Blue
		uniformDataComposition.lights[2].position = glm::vec4(2.0f, -1.0f, 0.0f, 0.0f);
		uniformDataComposition.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
		uniformDataComposition.lights[2].radius = 5.0f;
		// Yellow
		uniformDataComposition.lights[3].position = glm::vec4(0.0f, -0.9f, 0.5f, 0.0f);
		uniformDataComposition.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
		uniformDataComposition.lights[3].radius = 2.0f;
		// Green
		uniformDataComposition.lights[4].position = glm::vec4(0.0f, -0.5f, 0.0f, 0.0f);
		uniformDataComposition.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
		uniformDataComposition.lights[4].radius = 5.0f;
		// Yellow
		uniformDataComposition.lights[5].position = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
		uniformDataComposition.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
		uniformDataComposition.lights[5].radius = 25.0f;

		// Animate the lights
		if (!paused) {
			uniformDataComposition.lights[0].position.x = sin(glm::radians(360.0f * timer)) * 5.0f;
			uniformDataComposition.lights[0].position.z = cos(glm::radians(360.0f * timer)) * 5.0f;

			uniformDataComposition.lights[1].position.x = -4.0f + sin(glm::radians(360.0f * timer) + 45.0f) * 2.0f;
			uniformDataComposition.lights[1].position.z = 0.0f + cos(glm::radians(360.0f * timer) + 45.0f) * 2.0f;

			uniformDataComposition.lights[2].position.x = 4.0f + sin(glm::radians(360.0f * timer)) * 2.0f;
			uniformDataComposition.lights[2].position.z = 0.0f + cos(glm::radians(360.0f * timer)) * 2.0f;

			uniformDataComposition.lights[4].position.x = 0.0f + sin(glm::radians(360.0f * timer + 90.0f)) * 5.0f;
			uniformDataComposition.lights[4].position.z = 0.0f - cos(glm::radians(360.0f * timer + 45.0f)) * 5.0f;

			uniformDataComposition.lights[5].position.x = 0.0f + sin(glm::radians(-360.0f * timer + 135.0f)) * 10.0f;
			uniformDataComposition.lights[5].position.z = 0.0f - cos(glm::radians(-360.0f * timer - 45.0f)) * 10.0f;
		}

		// Current view position
		uniformDataComposition.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

		uniformDataComposition.debugDisplayTarget = debugDisplayTarget;

		memcpy(uniformBuffers.composition.mapped, &uniformDataComposition, sizeof(UniformDataComposition));
	}

	glm::vec3 calculateGaussian(float variance, float r, glm::vec3 falloff)//r:sample range falloff:scale sample range
	{
		glm::vec3 res;
		float rr1 = r / (0.001f + falloff.x);
		res.x = std::exp((-rr1 * rr1) / (2.f * variance)) / (2.f * 3.14f * variance);
		float rr2 = r / (0.001f + falloff.y);
		res.y = std::exp((-rr2 * rr2) / (2.f * variance)) / (2.f * 3.14f * variance);
		float rr3 = r / (0.001f + falloff.z);
		res.z = std::exp((-rr3 * rr3) / (2.f * variance)) / (2.f * 3.14f * variance);
		return res;
	}

	glm::vec3 calcProfile(float r, glm::vec3 falloff)
	{
		//multi gaussian
		return 0.100f * calculateGaussian(0.0484f, r, falloff) +
			0.118f * calculateGaussian(0.187f, r, falloff) +
			0.113f * calculateGaussian(0.567f, r, falloff) +
			0.358f * calculateGaussian(1.99f, r, falloff) +
			0.078f * calculateGaussian(7.41f, r, falloff);
	}


	void calcScatteringData()//once
	{
		if (burleySSS)
		{
			//struct SeparableBurleySSSParams
			//{
			//	glm::vec3 shapeParams = glm::vec3(0.026, 0.011, 0.006);//S
			//	float worldScale = 1.f;
			//}separableBurleySSSParams;
			//struct UniformDataScatteringBurleySSS
			//{
			//	glm::mat4 invProjMat;
			//	glm::vec4 _DepthTexelSize;//1/width,1/height,width,height
			//	glm::vec4 _BurleySubsurfaceParams;
			//	glm::vec4 _ShapeParamsAndMaxScatterDists;//s d
			//	glm::vec4 _ZNear_Far_zBufferParams;//near, far-near, zbuffer.x, zbuffer.y
			//	glm::vec4 _Sample_r[ArrayCount];
			//	glm::vec4 _Sample_rcpPdf[ArrayCount];
			//	glm::vec4 _Sample_sinPhi[ArrayCount];
			//	glm::vec4 _Sample_cosPhi[ArrayCount];
			//}uniformDataScatteringBurleySSS;

			float nearPlane = camera.getNearClip();
			float farPlane = camera.getFarClip();
			uniformDataScatteringBurleySSS.invProjMat = glm::inverse(camera.matrices.perspective);
			uniformDataScatteringBurleySSS._ZNear_Far_zBufferParams = glm::vec4(nearPlane, farPlane - nearPlane, 1.0 - farPlane / nearPlane, farPlane / nearPlane);

			//1、calculateBurleyParams
			auto Golden2dSeq = [](int i, float n)
				{
					const float frac_golden_ratio = 1.f / 1.618033988749895f;
					// GoldenAngle = 2 * Pi * (1 - 1 / GoldenRatio).
	                // We can drop the "1 -" part since all it does is reverse the orientation.
					return glm::vec2(i/n + 0.5/n, i * frac_golden_ratio - std::floor(i * frac_golden_ratio));//frac(i * frac_golden_ratio)
				};
			auto SampleDiskGolden = [&](int i, int sampleCount)
				{
					glm::vec2 f = Golden2dSeq(i, sampleCount);
					return glm::vec2(std::sqrt(f.x), 2.f * 3.1415926f * f.y);
				};

			auto SampleBurleyDiffusionProfileForR = [](float u, float rcpS)//maxCdf, maxRCPS -> filterRadius
				{
					u = 1 - u; // Convert CDF to CCDF
					float g = 1 + (4 * u) * (2 * u + sqrtf(1 + (4 * u) * u));
					float n = powf(g, -1.0f / 3.0f);                      // g^(-1/3)
					float p = (g * n) * n;                                   // g^(+1/3)
					float c = 1 + p + n;                                     // 1 + g^(+1/3) + g^(-1/3)
					float x = 3 * logf(c / (4.0f * u));

					return x * rcpS;
				};

			auto SampleBurleyDiffusionProfile = [](float u, float rcpS, float& r, float& rcpPdf)
				{
					const float  LOG2_E = 1.44269504088896340736f;
					const float PI = 3.1415926f;
					u = 1 - u; // Convert CDF to CCDF

					float g = 1 + (4 * u) * (2 * u + std::sqrt(1 + (4 * u) * u));
					float n = exp2(std::log2(g) * (-1.0 / 3.0));                    // g^(-1/3)
					float p = (g * n) * n;                                   // g^(+1/3)
					float c = 1 + p + n;                                     // 1 + g^(+1/3) + g^(-1/3)
					float d = (3 / LOG2_E * 2) + (3 / LOG2_E) * std::log2(u);     // 3 * Log[4 * u]
					float x = (3 / LOG2_E) * std::log2(c) - d;                    // 3 * Log[c / (4 * u)]

					// x      = s * r
					// exp_13 = Exp[-x/3] = Exp[-1/3 * 3 * Log[c / (4 * u)]]
					// exp_13 = Exp[-Log[c / (4 * u)]] = (4 * u) / c
					// exp_1  = Exp[-x] = exp_13 * exp_13 * exp_13
					// expSum = exp_1 + exp_13 = exp_13 * (1 + exp_13 * exp_13)
					// rcpExp = rcp(expSum) = c^3 / ((4 * u) * (c^2 + 16 * u^2))
					float rcpExp = ((c * c) * c) / ((4 * u) * ((c * c) + (4 * u) * (4 * u)));

					r = x * rcpS;
					rcpPdf = (8 * PI * rcpS) * rcpExp; // (8 * Pi) / s / (Exp[-s * r / 3] + Exp[-s * r])
				};

			float maxCdf = 0.997f;
			//rcpS
			glm::vec3 shapeParams = glm::vec3(std::min(16777216.f, 1.f / separableBurleySSSParams.shapeParams.x), 
				std::min(16777216.f, 1.f / separableBurleySSSParams.shapeParams.y), 
				std::min(16777216.f, 1.f / separableBurleySSSParams.shapeParams.z));
			// Importance sample the normalized diffuse reflectance profile for the computed value of 's'.
			// ------------------------------------------------------------------------------------
			// R[r, phi, s]   = s * (Exp[-r * s] + Exp[-r * s / 3]) / (8 * Pi * r)
			// PDF[r, phi, s] = r * R[r, phi, s]
			// CDF[r, s]      = 1 - 1/4 * Exp[-r * s] - 3/4 * Exp[-r * s / 3]
			// ------------------------------------------------------------------------------------
			// We importance sample the color channel with the widest scattering distance.
			float maxScatteringDist = std::max(std::max(separableBurleySSSParams.shapeParams.x, separableBurleySSSParams.shapeParams.y), separableBurleySSSParams.shapeParams.z);
			float maxFilterRadius = SampleBurleyDiffusionProfileForR(maxCdf, maxScatteringDist);

			uniformDataScatteringBurleySSS._BurleySubsurfaceParams = glm::vec4(maxFilterRadius, separableBurleySSSParams.worldScale, 0.f, 0.f);
			uniformDataScatteringBurleySSS._ShapeParamsAndMaxScatterDists = glm::vec4(shapeParams.x, shapeParams.y, shapeParams.z, maxScatteringDist);

			//2、 calc sequence
			float sampleCount = ArrayCount * 4;
			float scale = 1.f / sampleCount;
			float offset = scale * 0.5f;
			for (int i = 0; i < sampleCount; i++)
			{
				float r, rcpPdf;
				float d = maxScatteringDist;
				SampleBurleyDiffusionProfile(i * scale + offset, d, r, rcpPdf);
				float phi = SampleDiskGolden(i, sampleCount).y;
				float sinPhi = std::sin(phi);
				float cosPhi = std::cos(phi);
				int arrayIndex = i / 4;
				int compIndex = i % 4;
				uniformDataScatteringBurleySSS._Sample_r[arrayIndex][compIndex] = r;
				uniformDataScatteringBurleySSS._Sample_rcpPdf[arrayIndex][compIndex] = rcpPdf;
				uniformDataScatteringBurleySSS._Sample_sinPhi[arrayIndex][compIndex] = sinPhi;
				uniformDataScatteringBurleySSS._Sample_cosPhi[arrayIndex][compIndex] = cosPhi;
			}
		}
		else//ssss
		{
			//struct SeparableSSSParams
			//{
			//	glm::vec3 subsurfaceColor = glm::vec3(0.8396226, 0.1069331, 0.1069331);
			//	glm::vec3 subsurfaceFalloff = glm::vec3(0.754717, 0.3402277, 0.05);
			//	float strength = 1.f;
			//}separableSSSParams;

			//struct UniformDataScatteringSSSS {
			//	glm::vec4 _ScreenSubsurfaceProps;
			//	glm::vec4 _DepthTexelSize;
			//	glm::vec4 _ZNear_Far;
			//	glm::vec4 _ScreenSubsurfaceKernel[samplerSteps];
			//} uniformDataScatteringSSSS;

			//_ScreenSubsurfaceProps&&_ZNear_Far
			float hFovTan = std::tan(0.5f * (glm::radians(camera.getFOV())));//X DIRECTION * ASPECT
			float distanceToProjectionWindow = 1.f / hFovTan;
			float DPTimes300 = 300.f * distanceToProjectionWindow;
			uniformDataScatteringSSSS._ScreenSubsurfaceProps = glm::vec4(separableSSSParams.strength, distanceToProjectionWindow, DPTimes300, camera.getAspect());

			float nearPlane = camera.getNearClip();
			float farPlane = camera.getFarClip();
			uniformDataScatteringSSSS._ZNear_Far = glm::vec4(nearPlane, farPlane - nearPlane, 0.f, 0.f);

			int nSamples = samplerSteps;
			float range = nSamples > 20 ? 3.f : 2.f;
			float exponent = 2.f;
			//2/3 pixels around center
			float step = 2.f * range / (nSamples - 1);
			for (int i = 0; i < nSamples; i++)
			{
				float o = -range + i * step;
				float sign = o < 0.f ? -1.f : 1.f;
				float w = range * sign * std::abs(std::pow(o, exponent)) / std::pow(range, exponent);
				uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i] = glm::vec4(0.f, 0.f, 0.f, w);
			}
			for (int i = 0; i < nSamples; i++)
			{
				float w0 = i > 0 ? std::abs(uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i].w - uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i - 1].w) : 0.0f;
				float w1 = i < nSamples - 1 ? std::abs(uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i].w - uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i + 1].w) : 0.0f;
				float area = (w0 + w1) * 0.5f;
				glm::vec3 temp = calcProfile(uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i].w, separableSSSParams.subsurfaceFalloff);
				glm::vec4 tt = glm::vec4(area * temp.x, area * temp.y, area * temp.z, uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i].w);
				uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i] = tt;
			}
			glm::vec4 t = uniformDataScatteringSSSS._ScreenSubsurfaceKernel[nSamples / 2];
			for (int i = nSamples / 2; i > 0; i--)
				uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i] = uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i - 1];
			uniformDataScatteringSSSS._ScreenSubsurfaceKernel[0] = t;
			glm::vec4 sum{ 0,0,0,0 };
			for (int i = 0; i < nSamples; i++)
			{
				sum.x += uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i].x;
				sum.y += uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i].y;
				sum.z += uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i].z;
			}
			for (int i = 0; i < nSamples; i++)
			{
				glm::vec4 vecx = uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i];
				vecx.x /= sum.x;
				vecx.y /= sum.y;
				vecx.z /= sum.z;
				uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i] = vecx;
			}
			glm::vec4 vec = uniformDataScatteringSSSS._ScreenSubsurfaceKernel[0];
			vec.x = (1.0f - separableSSSParams.subsurfaceColor.x) * 1.0f + separableSSSParams.subsurfaceColor.x * vec.x;
			vec.y = (1.0f - separableSSSParams.subsurfaceColor.y) * 1.0f + separableSSSParams.subsurfaceColor.y * vec.y;
			vec.z = (1.0f - separableSSSParams.subsurfaceColor.z) * 1.0f + separableSSSParams.subsurfaceColor.z * vec.z;
			uniformDataScatteringSSSS._ScreenSubsurfaceKernel[0] = vec;

			for (int i = 1; i < nSamples; i++)
			{
				auto vect = uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i];
				vect.x *= separableSSSParams.subsurfaceColor.x;
				vect.y *= separableSSSParams.subsurfaceColor.y;
				vect.z *= separableSSSParams.subsurfaceColor.z;
				uniformDataScatteringSSSS._ScreenSubsurfaceKernel[i] = vect;
			}
		}
	}

	void updateUniformBufferScattering()
	{
		calcScatteringData();
		if (burleySSS)
		{
			memcpy(uniformBuffers.scattering.mapped, &uniformDataScatteringBurleySSS, sizeof(UniformDataScatteringBurleySSS));
		}
		else
		{
			memcpy(uniformBuffers.scattering.mapped, &uniformDataScatteringSSSS, sizeof(UniformDataScatteringSSSS));
		}
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareOffscreenFramebuffer();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffers();
		buildDeferredCommandBuffer();
		buildShadingCommandBuffer();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// The scene render command buffer has to wait for the offscreen
		// rendering to be finished before we can use the framebuffer
		// color image for sampling during final rendering
		// To ensure this we use a dedicated offscreen synchronization
		// semaphore that will be signaled when offscreen rendering
		// has been finished
		// This is necessary as an implementation may start both
		// command buffers at the same time, there is no guarantee
		// that command buffers will be executed in the order they
		// have been submitted by the application

		// Offscreen rendering

		// Wait for swap chain presentation to finish
		submitInfo.pWaitSemaphores = &semaphores.presentComplete;
		// Signal ready with offscreen semaphore
		submitInfo.pSignalSemaphores = &offscreenSemaphore;

		// Submit work
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &offScreenCmdBuffer;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));


		// Scene rendering
		// Wait for offscreen semaphore
		submitInfo.pWaitSemaphores = &offscreenSemaphore;
		// Signal ready with render complete semaphore
		submitInfo.pSignalSemaphores = &shadingSemaphore;

		// Submit work
		submitInfo.pCommandBuffers = &shadingCmdBuffer;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));


		// final
		// Wait for shading Semaphore
		submitInfo.pWaitSemaphores = &shadingSemaphore;
		// Signal ready with render complete semaphore
		submitInfo.pSignalSemaphores = &semaphores.renderComplete;

		// Submit work
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	virtual void render()
	{
		if (!prepared)
			return;
		updateUniformBufferComposition();
		updateUniformBufferOffscreen();
		draw();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->comboBox("Display", &debugDisplayTarget, { "Final composition", "Position", "Normals", "Albedo", "Specular" });
			//ssss
			if (overlay->inputFloat("subsurfaceColor.r", &separableSSSParams.subsurfaceColor.r, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("subsurfaceColor.g", &separableSSSParams.subsurfaceColor.g, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("subsurfaceColor.b", &separableSSSParams.subsurfaceColor.b, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("subsurfaceFalloff.r", &separableSSSParams.subsurfaceFalloff.r, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("subsurfaceFalloff.g", &separableSSSParams.subsurfaceFalloff.g, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("subsurfaceFalloff.b", &separableSSSParams.subsurfaceFalloff.b, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("ssssStrength", &separableSSSParams.strength, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			//burleySSS
			if (overlay->inputFloat("shapeParams.r", &separableBurleySSSParams.shapeParams.r, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("shapeParams.g", &separableBurleySSSParams.shapeParams.g, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("shapeParams.b", &separableBurleySSSParams.shapeParams.b, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
			if (overlay->inputFloat("burleyWorldScale", &separableBurleySSSParams.worldScale, 0.1f, 2)) {
				updateUniformBufferScattering();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
