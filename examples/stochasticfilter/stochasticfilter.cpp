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
	vks::Texture2D colorInTex;
	vks::Texture2D randomInTex;
	uint32_t frameNumber=0;
	float time=0.f;

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


	struct UBOPostprocessParams {
		bool isFIS = true;
	}switchParam;

	struct {
		glm::vec4 randomParams;
	} ubos;

	struct {
		vks::Buffer paramsBuffer;
	}uniformBuffer;

	struct {
		VkPipeline fisPass;
		VkPipeline catRomPass;
	} pipelines;

	struct {
		VkPipelineLayout postprocess;
	} pipelineLayouts;

	struct {
		VkDescriptorSet stochasticFilter;
	} descriptorSets;

	struct {
		VkDescriptorSetLayout postprocess;
	} descriptorSetLayouts;

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
		vkDestroyPipeline(device, pipelines.fisPass, nullptr);
		vkDestroyPipeline(device, pipelines.catRomPass, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.postprocess, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.postprocess, nullptr);

		// Uniform buffers
		uniformBuffer.paramsBuffer.destroy();

		colorInTex.destroy();
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
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.framebuffer = frameBuffers[i];//FINAL PRESENT FRAMEBUFFER
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				cmdBeginLabel(drawCmdBuffers[i], "Postprocess rendering", { 1, 1,1, 1 });
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.postprocess, 0, 1, &descriptorSets.stochasticFilter, 0, NULL);
				if (switchParam.isFIS)
				{
					vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fisPass);
				}
				else
				{
					vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.catRomPass);
				}
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
		//colorInTex.loadFromFile(getAssetPath() + "textures/vulkan_cloth_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		//randomInTex.loadFromFile(getAssetPath() + "textures/vulkan_cloth_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);/*
		colorInTex.loadFromFile(getAssetPath() + "textures/rgba8noise.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		randomInTex.loadFromFile(getAssetPath() + "textures/rgba8noise.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;

		// rendering
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),			// Binding 0 : Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),	// Binding 1 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),	// Binding 2 : Fragment shader image sampler
		};

		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.postprocess));

		// Sets
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo;
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Skybox
		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.postprocess, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.stochasticFilter));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.stochasticFilter, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.paramsBuffer.descriptor),						// Binding 0: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.stochasticFilter, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	1, &randomInTex.descriptor),							// Binding 1: Fragment shader texture sampler
			vks::initializers::writeDescriptorSet(descriptorSets.stochasticFilter, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	2, &colorInTex.descriptor),							// Binding 1: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void preparePipelines()
	{
		// Layouts
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.postprocess, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.postprocess));

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

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.postprocess, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();


		// fis
		shaderStages[0] = loadShader(getShadersPath() + "stochasticfilter/stocasticcatfis.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "stochasticfilter/stocasticcatfis.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.layout = pipelineLayouts.postprocess;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.fisPass));

		// cat rom
		shaderStages[0] = loadShader(getShadersPath() + "stochasticfilter/stocasticcatrom.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "stochasticfilter/stocasticcatrom.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.catRomPass));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// randomParams
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffer.paramsBuffer,
			sizeof(ubos.randomParams)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffer.paramsBuffer.map());

		// Initialize uniform buffers
		updateUniformBuffersPostprocess();
	}

	// Update blur pass parameter uniform buffer
	void updateUniformBuffersPostprocess()
	{
		memcpy(uniformBuffer.paramsBuffer.mapped, &ubos.randomParams, sizeof(ubos.randomParams));
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
		//// Name some objects for debugging
		//setObjectName(device, VK_OBJECT_TYPE_IMAGE, (uint64_t)offscreenPass.framebuffer.color.image, "Off-screen color framebuffer");
		//setObjectName(device, VK_OBJECT_TYPE_IMAGE, (uint64_t)offscreenPass.framebuffer.depth.image, "Off-screen depth framebuffer");
		//setObjectName(device, VK_OBJECT_TYPE_SAMPLER, (uint64_t)offscreenPass.sampler, "Off-screen framebuffer default sampler");

		//setObjectName(device, VK_OBJECT_TYPE_BUFFER, (uint64_t)models.plants.vertices.buffer, "Scene vertex buffer");
		//setObjectName(device, VK_OBJECT_TYPE_BUFFER, (uint64_t)models.plants.indices.buffer, "Scene index buffer");

		////// Shader module count starts at 2 when UI overlay in base class is enabled
		////uint32_t moduleIndex = settings.overlay ? 2 : 0;
		////setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 0], "Toon shading vertex shader");
		////setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 1], "Toon shading fragment shader");
		////setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 2], "Color-only vertex shader");
		////setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 3], "Color-only fragment shader");
		////setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 4], "Postprocess vertex shader");
		////setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shaderModules[moduleIndex + 5], "Postprocess fragment shader");

		//setObjectName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipelineLayouts.scene, "Scene pipeline layout");
		//setObjectName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipelineLayouts.postprocess, "Postprocess pipeline layout");
		//setObjectName(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelines.skyBox, "Skybox pipeline");
		//setObjectName(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelines.phongPass, "SceneColor pipeline");
		//setObjectName(device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelines.postprocess, "Postprocess pipeline");

		//setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)descriptorSetLayouts.scene, "Scene descriptor set layout");
		//setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)descriptorSetLayouts.postprocess, "Postprocess descriptor set layout");
		//setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSets.skyBox, "Skybox descriptor set");
		//setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSets.scene, "Scene descriptor set");
		//setObjectName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSets.postprocess, "Postprocess descriptor set");
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		setupDebugUtils();
		loadAssets();
		prepareUniformBuffers();
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
		frameNumber++;
		time += 0.016f;
		draw();
		ubos.randomParams = glm::vec4(frameNumber, time, 0.0f, 0.0f);
		updateUniformBuffersPostprocess();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			if (overlay->checkBox("YES-FIS-FALSE-CATROM", &switchParam.isFIS)) {
				buildCommandBuffers();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
