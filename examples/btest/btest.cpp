/*
* Vulkan Example - Implements a separable two-pass fullscreen blur (also known as bloom)
*
* Copyright (C) 2016 - 2023 Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"


// Offscreen frame buffer properties
#define FB_DIM 256
#define FB_COLOR_FORMAT VK_FORMAT_R8G8B8A8_UNORM

class VulkanExample : public VulkanExampleBase
{
public:
	vks::TextureCubeMap cubemap;

	//For debug
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT{ nullptr };
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT{ nullptr };
	PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT{ nullptr };
	PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT{ nullptr };
	PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT{ nullptr };
	PFN_vkQueueBeginDebugUtilsLabelEXT vkQueueBeginDebugUtilsLabelEXT{ nullptr };
	PFN_vkQueueInsertDebugUtilsLabelEXT vkQueueInsertDebugUtilsLabelEXT{ nullptr };
	PFN_vkQueueEndDebugUtilsLabelEXT vkQueueEndDebugUtilsLabelEXT{ nullptr };
	PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT{ nullptr };
	bool debugUtilsSupported = false;

	struct {
		vkglTF::Model plants;
		vkglTF::Model skyBox;
	} models;

	struct {
		vks::Buffer scene;
		vks::Buffer skyBox;
		vks::Buffer postprocessParams;
	} uniformBuffers;

	struct UBO {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
	};

	struct UBOPostprocessParams {
		float tintColorR = 1.0f;
		float tintColorG = 1.0f;
		float tintColorB = 1.0f;
		float tintColorA = 1.0f;
	};

	struct {
		UBO scene, skyBox;
		UBOPostprocessParams postprocessParams;
	} ubos;

	struct {
		VkPipeline postprocess;
		VkPipeline phongPass;
		VkPipeline skyBox;
	} pipelines;

	struct {
		VkPipelineLayout postprocess;
		VkPipelineLayout scene;//same layout可以共用，如scene和skybox
	} pipelineLayouts;

	struct {
		VkDescriptorSet postprocess;//one vs/fs pass one ds set
		VkDescriptorSet scene;
		VkDescriptorSet skyBox;
	} descriptorSets;

	struct {
		VkDescriptorSetLayout postprocess;
		VkDescriptorSetLayout scene;
	} descriptorSetLayouts;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	};
	struct FrameBuffer {
		VkFramebuffer framebuffer;
		FrameBufferAttachment color, depth;
		VkDescriptorImageInfo descriptor;
	};
	struct OffscreenPass {
		int32_t width, height;
		VkRenderPass renderPass;
		VkSampler sampler;
		FrameBuffer framebuffer;
	} offscreenPass;

	VulkanExample() : VulkanExampleBase()
	{
		title = "TEST (offscreen rendering)";
		timerSpeed *= 0.5f;
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -10.25f));
		camera.setRotation(glm::vec3(7.5f, -343.0f, 0.0f));
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class

		vkDestroySampler(device, offscreenPass.sampler, nullptr);

		// Frame buffer
		auto& framebuffer = offscreenPass.framebuffer;
		{
			// Attachments
			vkDestroyImageView(device, framebuffer.color.view, nullptr);
			vkDestroyImage(device, framebuffer.color.image, nullptr);
			vkFreeMemory(device, framebuffer.color.mem, nullptr);
			vkDestroyImageView(device, framebuffer.depth.view, nullptr);
			vkDestroyImage(device, framebuffer.depth.image, nullptr);
			vkFreeMemory(device, framebuffer.depth.mem, nullptr);

			vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);
		}
		vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);

		vkDestroyPipeline(device, pipelines.postprocess, nullptr);
		vkDestroyPipeline(device, pipelines.phongPass, nullptr);
		vkDestroyPipeline(device, pipelines.skyBox, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.postprocess, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.scene, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.postprocess, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);

		// Uniform buffers
		uniformBuffers.scene.destroy();
		uniformBuffers.skyBox.destroy();
		uniformBuffers.postprocessParams.destroy();

		cubemap.destroy();
	}

	// Checks if debug utils are supported (usually only when a graphics debugger is active) and does the setup necessary to use this debug utils
	void setupDebugUtils()
	{
		bool extentionPresent = false;
		uint32_t extensionCount;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
		for (auto& extension : extensions)
		{
			if (strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
			{
				extentionPresent = true;
				break;
			}
		}
		if (extentionPresent)
		{
			vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
			vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
			vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
			vkCmdInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT"));
			vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
			vkQueueBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkQueueBeginDebugUtilsLabelEXT"));
			vkQueueInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueInsertDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkQueueInsertDebugUtilsLabelEXT"));
			vkQueueEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, "vkQueueEndDebugUtilsLabelEXT"));
			vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
			
			debugUtilsSupported = (vkCreateDebugUtilsMessengerEXT != VK_NULL_HANDLE);
		}
		else
		{
			std::cout << "Warning:" << VK_EXT_DEBUG_UTILS_EXTENSION_NAME << "not present， debug utils are disabled.";
		    std::cout << "Trying running the sample from inside a Vulkan graphics debugger(e.g.Renderdoc)" << std::endl;
		}
	}
	// The debug utils extensions allows us to put labels into command buffers and queues (to e.g. mark regions of interest) and to name Vulkan objects
	// We wrap these into functions for convenience

	// Functions for putting labels into a command buffer
	// Labels consist of a name and an optional color
	// How or if these are diplayed depends on the debugger used (RenderDoc e.g. displays both)

	void cmdBeginLabel(VkCommandBuffer command_buffer, const char* label_name, std::vector<float> color)
	{
		if (!debugUtilsSupported) {
			return;
		}
		VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		label.pLabelName = label_name;
		memcpy(label.color, color.data(), sizeof(float) * 4);
		vkCmdBeginDebugUtilsLabelEXT(command_buffer, &label);
	}

	void cmdInsertLabel(VkCommandBuffer command_buffer, const char* label_name, std::vector<float> color)
	{
		if (!debugUtilsSupported) {
			return;
		}
		VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		label.pLabelName = label_name;
		memcpy(label.color, color.data(), sizeof(float) * 4);
		vkCmdInsertDebugUtilsLabelEXT(command_buffer, &label);
	}

	void cmdEndLabel(VkCommandBuffer command_buffer)
	{
		if (!debugUtilsSupported) {
			return;
		}
		vkCmdEndDebugUtilsLabelEXT(command_buffer);
	}

	// Setup the offscreen framebuffer for rendering the mirrored scene
	// The color attachment of this framebuffer will then be sampled from
	void prepareOffscreenFramebuffer(FrameBuffer *frameBuf, VkFormat colorFormat, VkFormat depthFormat)
	{
		// Color attachment
		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = colorFormat;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		// We will sample directly from the color attachment
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = colorFormat;
		colorImageView.flags = 0;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &frameBuf->color.image));
		vkGetImageMemoryRequirements(device, frameBuf->color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->color.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->color.image, frameBuf->color.mem, 0));

		colorImageView.image = frameBuf->color.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &frameBuf->color.view));

		// Depth stencil attachment
		image.format = depthFormat;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VkImageViewCreateInfo depthStencilView = vks::initializers::imageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = depthFormat;
		depthStencilView.flags = 0;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (vks::tools::formatHasStencil(depthFormat)) {
			depthStencilView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &frameBuf->depth.image));
		vkGetImageMemoryRequirements(device, frameBuf->depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->depth.image, frameBuf->depth.mem, 0));

		depthStencilView.image = frameBuf->depth.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &frameBuf->depth.view));

		VkImageView attachments[2];
		attachments[0] = frameBuf->color.view;
		attachments[1] = frameBuf->depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
		fbufCreateInfo.renderPass = offscreenPass.renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = width;
		fbufCreateInfo.height = height;
		fbufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &frameBuf->framebuffer));

		// Fill a descriptor for later use in a descriptor set
		frameBuf->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		frameBuf->descriptor.imageView = frameBuf->color.view;
		frameBuf->descriptor.sampler = offscreenPass.sampler;
	}

	// Prepare the offscreen framebuffers used for the vertical- and horizontal blur
	void prepareOffscreen()
	{
		offscreenPass.width = width;
		offscreenPass.height = height;

		// Find a suitable depth format
		VkFormat fbDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
		assert(validDepthFormat);

		// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering

		std::array<VkAttachmentDescription, 2> attchmentDescriptions = {};
		// Color attachment
		attchmentDescriptions[0].format = FB_COLOR_FORMAT;
		attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// Depth attachment
		attchmentDescriptions[1].format = fbDepthFormat;
		attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &offscreenPass.renderPass));

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &offscreenPass.sampler));

		// Create two frame buffers
		prepareOffscreenFramebuffer(&offscreenPass.framebuffer, FB_COLOR_FORMAT, fbDepthFormat);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		VkViewport viewport;
		VkRect2D scissor;

		/*
			The blur method used in this example is multi pass and renders the vertical blur first and then the horizontal one
			While it's possible to blur in one pass, this method is widely used as it requires far less samples to generate the blur
		*/

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)//RECORD TO DRAWBUFFERS
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
			/*
				Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
			*/

			/*
				Third render pass: Scene rendering with applied vertical blur

				Renders the scene and the (vertically blurred) contents of the second framebuffer and apply a horizontal blur

			*/
			//render
			{
				clearValues[0].color = defaultClearColor;
				clearValues[1].depthStencil = { 1.0f, 0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
				renderPassBeginInfo.renderPass = offscreenPass.renderPass;
				renderPassBeginInfo.framebuffer = offscreenPass.framebuffer.framebuffer;//FINAL PRESENT FRAMEBUFFER
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues;

				cmdBeginLabel(drawCmdBuffers[i], "Offscreen scene rendering", { 1,1,1,1 });
				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				// Skybox
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.skyBox, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skyBox);
				models.skyBox.draw(drawCmdBuffers[i]);

				// 3D scene
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.scene, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phongPass);
				models.plants.draw(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
				cmdEndLabel(drawCmdBuffers[i]);

				//postprocess
				clearValues[1].depthStencil = { 0.0f, 0 };
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.framebuffer = frameBuffers[i];
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues;
				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				cmdBeginLabel(drawCmdBuffers[i], "Postprocess rendering", { 1, 1,1, 1 });
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.postprocess, 0, 1, &descriptorSets.postprocess, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.postprocess);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
				cmdEndLabel(drawCmdBuffers[i]);

				cmdBeginLabel(drawCmdBuffers[i], "UI Overlay rendering", { 1, 1, 1, 1 });
				drawUI(drawCmdBuffers[i]);
				cmdEndLabel(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

			}

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.plants.loadFromFile(getAssetPath() + "models/plants.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.skyBox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		cubemap.loadFromFile(getAssetPath() + "textures/cubemap_yokohama_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 5);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;

		// Scene rendering
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),	// Binding 1 : Fragment shader image sampler
		};

		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.scene));

		//postprocess
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),			// Binding 0: Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)	// Binding 1: Fragment shader image sampler
		};
		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.postprocess));

		// Sets
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo;
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Skybox
		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.scene, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.skyBox));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.skyBox, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.skyBox.descriptor),						// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.skyBox, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	1, &cubemap.descriptor),							// Binding 1: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Scene rendering
		//descriptorSetAllocInfo=SKYBOX
	    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.scene));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.scene.descriptor)							// Binding 0: Vertex shader uniform buffer
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Postprocess
		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.postprocess, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.postprocess));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.postprocess, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.postprocessParams.descriptor),				// Binding 0: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.postprocess, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &offscreenPass.framebuffer.descriptor),	            // Binding 1: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void preparePipelines()
	{
		// Layouts
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.postprocess, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.postprocess));

		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.scene, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.scene));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.scene, offscreenPass.renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// Skybox (cubemap)
		shaderStages[0] = loadShader(getShadersPath() + "btest/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "btest/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal });
		pipelineCI.layout = pipelineLayouts.scene;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skyBox));

		// Phong pass (3D model)
		shaderStages[0] = loadShader(getShadersPath() + "btest/phongpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "btest/phongpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phongPass));

		// post pipelines
		shaderStages[0] = loadShader(getShadersPath() + "btest/graying.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "btest/graying.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.layout = pipelineLayouts.postprocess;
		pipelineCI.renderPass = renderPass;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.postprocess));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Phong and color pass vertex shader uniform buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.scene,
			sizeof(ubos.scene)));

		// Blur parameters uniform buffers
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.postprocessParams,
			sizeof(ubos.postprocessParams)));

		// Skybox
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.skyBox,
			sizeof(ubos.skyBox)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.scene.map());
		VK_CHECK_RESULT(uniformBuffers.postprocessParams.map());
		VK_CHECK_RESULT(uniformBuffers.skyBox.map());

		// Initialize uniform buffers
		updateUniformBuffersScene();
		updateUniformBuffersPostprocess();
	}

	// Update uniform buffers for rendering the 3D scene
	void updateUniformBuffersScene()
	{
		// Plants
		ubos.scene.projection = camera.matrices.perspective;
		ubos.scene.view = camera.matrices.view;

		ubos.scene.model = glm::translate(glm::mat4(1.0f), glm::vec3(sin(glm::radians(timer * 360.0f)) * 0.25f, -1.0f, cos(glm::radians(timer * 360.0f)) * 0.25f));
		ubos.scene.model = glm::rotate(ubos.scene.model, -sinf(glm::radians(timer * 360.0f)) * 0.15f, glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.scene.model = glm::rotate(ubos.scene.model, glm::radians(timer * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		memcpy(uniformBuffers.scene.mapped, &ubos.scene, sizeof(ubos.scene));

		// Skybox
		ubos.skyBox.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
		ubos.skyBox.view = glm::mat4(glm::mat3(camera.matrices.view));
		ubos.skyBox.model = glm::mat4(1.0f);

		memcpy(uniformBuffers.skyBox.mapped, &ubos.skyBox, sizeof(ubos.skyBox));
	}

	// Update blur pass parameter uniform buffer
	void updateUniformBuffersPostprocess()
	{
		memcpy(uniformBuffers.postprocessParams.mapped, &ubos.postprocessParams, sizeof(ubos.postprocessParams));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}

	// Function for naming Vulkan objects
	// In Vulkan, all objects (that can be named) are opaque unsigned 64 bit handles, and can be cased to uint64_t

	void setObjectName(VkDevice device, VkObjectType object_type, uint64_t object_handle, const char* object_name)
	{
		if (!debugUtilsSupported) {
			return;
		}
		VkDebugUtilsObjectNameInfoEXT name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		name_info.objectType = object_type;
		name_info.objectHandle = object_handle;
		name_info.pObjectName = object_name;
		vkSetDebugUtilsObjectNameEXT(device, &name_info);
	}

	void nameDebugObjects()
	{
		// Name some objects for debugging
		setObjectName(device, VK_OBJECT_TYPE_IMAGE, (uint64_t)offscreenPass.framebuffer.color.image, "Off-screen color framebuffer");
		setObjectName(device, VK_OBJECT_TYPE_IMAGE, (uint64_t)offscreenPass.framebuffer.depth.image, "Off-screen depth framebuffer");
		setObjectName(device, VK_OBJECT_TYPE_SAMPLER, (uint64_t)offscreenPass.sampler, "Off-screen framebuffer default sampler");

		setObjectName(device, VK_OBJECT_TYPE_BUFFER, (uint64_t)models.plants.vertices.buffer, "Scene vertex buffer");
		setObjectName(device, VK_OBJECT_TYPE_BUFFER, (uint64_t)models.plants.indices.buffer, "Scene index buffer");

		//// Shader module count starts at 2 when UI overlay in base class is enabled
		//uint32_t moduleIndex = settings.overlay ? 2 : 0;
		//setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 0], "Toon shading vertex shader");
		//setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 1], "Toon shading fragment shader");
		//setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 2], "Color-only vertex shader");
		//setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 3], "Color-only fragment shader");
		//setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 4], "Postprocess vertex shader");
		//setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 5], "Postprocess fragment shader");

		setObjectName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipelineLayouts.scene, "Scene pipeline layout");
		setObjectName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipelineLayouts.postprocess, "Postprocess pipeline layout");
		setObjectName(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelines.skyBox, "Skybox pipeline");
		setObjectName(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelines.phongPass, "SceneColor pipeline");
		setObjectName(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelines.postprocess, "Postprocess pipeline");

		setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)descriptorSetLayouts.scene, "Scene descriptor set layout");
		setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)descriptorSetLayouts.postprocess, "Postprocess descriptor set layout");
		setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSets.skyBox, "Skybox descriptor set");
		setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSets.scene, "Scene descriptor set");
		setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSets.postprocess, "Postprocess descriptor set");
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		setupDebugUtils();
		loadAssets();
		prepareUniformBuffers();
		prepareOffscreen();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffers();
		nameDebugObjects();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		if (!paused || camera.updated)
		{
			updateUniformBuffersScene();
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			if (overlay->inputFloat("TintColorR", &ubos.postprocessParams.tintColorR, 0.1f, 2)) {
				updateUniformBuffersPostprocess();
			}
			if (overlay->inputFloat("TintColorG", &ubos.postprocessParams.tintColorG, 0.1f, 2)) {
				updateUniformBuffersPostprocess();
			}
			if (overlay->inputFloat("TintColorB", &ubos.postprocessParams.tintColorB, 0.1f, 2)) {
				updateUniformBuffersPostprocess();
			}
			if (overlay->inputFloat("TintColorA", &ubos.postprocessParams.tintColorA, 0.1f, 2)) {
				updateUniformBuffersPostprocess();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
