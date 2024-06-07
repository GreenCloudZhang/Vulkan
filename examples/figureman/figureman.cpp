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

class VulkanExample;
class EyeRender
{
public:
	VkDevice device{ VK_NULL_HANDLE };

	struct {
		vkglTF::Model eye;
	} models;

	//global
	vks::Texture2D midPlaneDisplacementMap;
	vks::Texture2D normalMap;

	//iris
	vks::Texture2D irisBaseMap;
	vks::Texture2D irisOcculusionMap;

	//sclera
	vks::Texture2D scleraBaseMap;

	//Veins
	vks::Texture2D veinsBaseMap;

	//Highlight
	vks::Texture2D highlightMap;

	struct ENVUBOParams {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 cameraPosWS;
		glm::vec4 lightDir;
		glm::vec4 lightColor;
	}envParams;

	struct EyeUBOParams{
		//global
		glm::vec4 irisBleedTint;
		glm::vec4  pupilShift;
		glm::vec4 scleraTint;
		float refractionDepthScale;

		//iris
		float irisUVRadius;
		//glm::vec4 irisBleedTint;
		float irisSaturation;
		float aoInBaseColorPower;
		float irisBorderWidth;
		float limbusDarkScale;
		float limbusDarkPower;
		float pupilScale;
		//glm::vec2  pupilShift;

		//sclera
		//glm::vec4 scleraTint;
		float scleraBrightness;
		float scleraPower;

		//Veins
		float veinsPower;
		float veinsOpacity;

		//highlight override
		float highlightIntensity;
	}eyeParams;

	struct {
		vks::Buffer envParamsBuffer;
		vks::Buffer eyeParamsBuffer;
	}uniformBuffers;

	struct {
		VkPipeline eyePass;
	} pipelines;

	struct {
		VkPipelineLayout eye;
	} pipelineLayouts;

	struct {
		VkDescriptorSet eye;
	} descriptorSets;

	struct {
		VkDescriptorSetLayout eye;
	} descriptorSetLayouts;

	EyeRender() {

	};
	~EyeRender() {
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.eyePass, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.eye, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.eye, nullptr);

		// Uniform buffers
		uniformBuffers.envParamsBuffer.destroy();
		uniformBuffers.eyeParamsBuffer.destroy();

		midPlaneDisplacementMap.destroy();
		normalMap.destroy();
		//iris
		irisBaseMap.destroy();
		irisOcculusionMap.destroy();
		//sclera
		scleraBaseMap.destroy();
		//Veins
		veinsBaseMap.destroy();
		//Highlight
		highlightMap.destroy();
	}


	void loadAssets(vks::VulkanDevice* vulkanDevice, VkQueue queue)
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.eye.loadFromFile(getAssetPath() + "models/SphereWithTangent.gltf", vulkanDevice, queue, glTFLoadingFlags);
		midPlaneDisplacementMap.loadFromFile(getAssetPath() + "textures/figureman/eye/T_EyeMidPlaneDisplacement_dLDR.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		normalMap.loadFromFile(getAssetPath() + "textures/figureman/eye/T_EYE_NORMALS.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		//iris
		irisBaseMap.loadFromFile(getAssetPath() + "textures/figureman/eye/T_EyeIris_Base_F.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		irisOcculusionMap.loadFromFile(getAssetPath() + "textures/figureman/eye/T_Iris001_01_AO.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		//sclera
		scleraBaseMap.loadFromFile(getAssetPath() + "textures/figureman/eye/T_EyeSclera_Base.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		//Veins
		veinsBaseMap.loadFromFile(getAssetPath() + "textures/figureman/eye/T_EyeVeins_Base.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		//Highlight
		highlightMap.loadFromFile(getAssetPath() + "textures/figureman/eye/HIGHLIGHT_RGBM.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void updateUniformEnvBuffers()
	{
		memcpy(uniformBuffers.envParamsBuffer.mapped, &envParams, sizeof(envParams));
	}

	void updateUniformEyeBuffers()
	{
		memcpy(uniformBuffers.eyeParamsBuffer.mapped, &eyeParams, sizeof(eyeParams));
	}

	void initParams(Camera* camera)
	{
		envParams.projection = camera->matrices.perspective;
		envParams.view = camera->matrices.view;
		envParams.model = glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.0f, 0));
		envParams.cameraPosWS = glm::vec4(camera->position.x, camera->position.y, camera->position.z, 1.0f);
		envParams.lightDir = glm::vec4(0.8f, 0.2f, 0.1f, 1.f);
		envParams.lightColor = glm::vec4(1.f, 1.f, 1.f, 1.f);

		eyeParams.refractionDepthScale=1.3f;
		//iris
		eyeParams.irisUVRadius=0.15f;
		eyeParams.irisBleedTint=glm::vec4(1.f,1.f,1.f,1.f);
		eyeParams.irisSaturation=1.f;
		eyeParams.aoInBaseColorPower=0.1f;
		eyeParams.irisBorderWidth=0.04f;
		eyeParams.limbusDarkScale=2.f;
		eyeParams.limbusDarkPower=4.f;
		eyeParams.pupilScale=0.84f;
		eyeParams.pupilShift=glm::vec4(0.f,0.f,0.f,0.f);
		//sclera
		eyeParams.scleraTint=glm::vec4(1.0f,1.f,1.f,1.f);
		eyeParams.scleraBrightness=1.25f;
		eyeParams.scleraPower=1.f;
		//Veins
		eyeParams.veinsPower=0.3f;
		eyeParams.veinsOpacity=1.f;
		//highlight override
		eyeParams.highlightIntensity=4.f;

		//void main()
		//{
		//	//NORMAL TBN
		//	vec3 normalTS = normalize(texture(normalMap, inUV).xyz * 2.0 - 1.0);
		//	vec3 binormalWS = normalize(cross(inNormalWS, inTangentWS.xyz) * inTangentWS.w);
		//	mat3 tangent2normal = mat3(normalize(inTangentWS.xyz), binormalWS, normalize(inNormalWS));
		//	vec3 normalWS = tangent2normal * normalTS;

		//	//VIEW
		//	vec3 viewWS = normalize(env_ubo.cameraPosWS.xyz - inPosWS);

		//	float internalIor = 1.33;//index of refraction
		//	vec2 uv = inUV;
		//	float limbusUVWidth = eye_params.irisBorderWidth;
		//	float depthScale = eye_params.refractionDepthScale;
		//	float midPlaneDisplacement = texture(midPlaneDisplacementMap, inUV).r * 2.0f;
		//	vec3 eyeDirectionWS = normalWS;
		//	float irisUVRadius = eye_params.irisUVRadius;

		//	float depthPlaneOffset = texture(midPlaneDisplacementMap, vec2(irisUVRadius + 0.5, 0.5)).r * 2.0f;

		//	float irisMask;
		//	vec2 refractedUV;
		//	EyeRefraction(internalIor, uv, limbusUVWidth, depthScale, depthPlaneOffset, midPlaneDisplacement, eyeDirectionWS, irisUVRadius, normalWS, viewWS, inTangentWS.xyz, irisMask, refractedUV);

		//	float pupilScale = eye_params.pupilScale;
		//	float pupilShiftX = eye_params.pupilShift.x;
		//	float pupilShiftY = eye_params.pupilShift.y;

		//	vec2 scaledUV;
		//	GetScaledUVByCenter(irisUVRadius, refractedUV, pupilScale, pupilShiftX, pupilShiftY, scaledUV);

		//	vec3 lerpSclera1A = pow(texture(scleraBaseMap, inUV).rgb * vec3(eye_params.scleraBrightness) * eye_params.scleraTint.rgb, vec3(eye_params.scleraPower));
		//	vec3 lerpSclera1B = lerpSclera1A * eye_params.irisBleedTint.rgb;
		//	vec3 lerpSclera2A = mix(lerpSclera1A, lerpSclera1B, irisMask);//irisBleed
		//	vec3 lerpSclera2B = pow(texture(veinsBaseMap, inUV).rgb, vec3(eye_params.veinsPower));

		//	vec3 lerpAlbedoA = mix(lerpSclera2A, lerpSclera2B, eye_params.veinsOpacity);//add veinTex

		//	vec3 irisDetail = texture(irisBaseMap, scaledUV).rgb * clamp(1.0 - pow(length((scaledUV - vec2(0.5)) * eye_params.limbusDarkScale), eye_params.limbusDarkPower), 0.0f, 1.0f);
		//	vec3 irisAO = pow(texture(irisOcculusionMap, scaledUV).rgb, vec3(eye_params.aoInBaseColorPower));

		//	vec3 lerpAlbedoB;
		//	Desaturation(irisDetail * irisAO, 1 - eye_params.irisSaturation, vec3(0.3, 0.59, 0.11), lerpAlbedoB);

		//	vec3 albedo = mix(lerpAlbedoA, lerpAlbedoB, irisMask);
		//	float smoothness = 0.92f;
		//	float metallic = 0.0f;
		//	float occlusion = 1.0f;

		//	vec4 highlightSampler = texture(highlightMap, CartesianToPolar(reflect(viewWS, normalWS)));
		//	vec3 decodeHighlightHDR = highlightSampler.xyz * highlightSampler.w * 6.0f;
		//	vec4 highlight = vec4(decodeHighlightHDR * eye_params.highlightIntensity, 1.0);

		//	//shading
		//	vec3 shadingRes;
		//	EyeShading(inPosWS, normalWS, viewWS, vec4(albedo, 1.0), smoothness, metallic, occlusion, highlight, shadingRes);

		//	outFragColor = vec4(albedo, 1.0);
		//}
	}

	void prepareUniformBuffers(vks::VulkanDevice* vulkanDevice, Camera* camera)
	{
		initParams(camera);
		//Env Parms
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.envParamsBuffer,
			sizeof(envParams)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.envParamsBuffer.map());

		// Initialize uniform buffers
		updateUniformEnvBuffers();


		//Eye Parms
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.eyeParamsBuffer,
			sizeof(eyeParams)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.eyeParamsBuffer.map());

		// Initialize uniform buffers
		updateUniformEyeBuffers();
	}

	void setupDescriptors(VkDevice device, VkDescriptorPool descriptorPool)
	{
		// Layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;

		// rendering
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),			// Binding 0 : Vertex|Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),			// Binding 1 : Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),	// Binding 2 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),	// Binding 3 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),	// Binding 4 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5),	// Binding 5 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 6),	// Binding 6 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 7),	// Binding 7 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 8),	// Binding 8 : Fragment shader image sampler
		};

		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.eye));

		// Sets
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo;
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// eyeSet
		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.eye, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.eye));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.envParamsBuffer.descriptor),						// Binding 0: Vertex|Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniformBuffers.eyeParamsBuffer.descriptor),						// Binding 1: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	2, &midPlaneDisplacementMap.descriptor),							// Binding 2: Fragment shader texture sampler
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	3, &normalMap.descriptor),							// Binding 3: Fragment shader texture sampler
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	4, &irisBaseMap.descriptor),							// Binding 4: Fragment shader texture sampler
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	5, &irisOcculusionMap.descriptor),							// Binding 5: Fragment shader texture sampler
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	6, &scleraBaseMap.descriptor),							// Binding 6: Fragment shader texture sampler
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	7, &veinsBaseMap.descriptor),							// Binding 7: Fragment shader texture sampler
			vks::initializers::writeDescriptorSet(descriptorSets.eye, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	8, &highlightMap.descriptor),							// Binding 8: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
};

class VulkanExample : public VulkanExampleBase
{
public:
	EyeRender eyeRender;
	float pickIrisbleedColors[4] = {1,1,1,1};
	float pickScleraColors[4] = { 1,1,1,1 };

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


	struct UBO {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
	}globalUBO;

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

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)//RECORD TO DRAWBUFFERS
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
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

				cmdBeginLabel(drawCmdBuffers[i], "Eye Rendering", { 1, 1, 1, 1 });
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, eyeRender.pipelineLayouts.eye, 0, 1, &eyeRender.descriptorSets.eye, 0, NULL);
			    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, eyeRender.pipelines.eyePass);
				eyeRender.models.eye.draw(drawCmdBuffers[i]);
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
		eyeRender.device = device;
		eyeRender.loadAssets(vulkanDevice, queue);
	}

	void setupDescriptors()
	{
		// Pool:  all pass
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		eyeRender.setupDescriptors(device, descriptorPool);
	}

	void preparePipelines()
	{
		// Layouts
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&eyeRender.descriptorSetLayouts.eye, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &eyeRender.pipelineLayouts.eye));

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

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(eyeRender.pipelineLayouts.eye, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// eye
		shaderStages[0] = loadShader(getShaderBasePath() + "glsl" + "/" + "figureman/eye.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderBasePath() + "glsl" + "/" + "figureman/eye.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Tangent });
		pipelineCI.layout = eyeRender.pipelineLayouts.eye;
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &eyeRender.pipelines.eyePass));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		eyeRender.prepareUniformBuffers(vulkanDevice, &camera);
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

	void updateUniformBuffersScene()
	{
		// UBO
		globalUBO.projection = camera.matrices.perspective;
		globalUBO.view = camera.matrices.view;
		globalUBO.model = glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.0f, 0));

		eyeRender.envParams.projection = globalUBO.projection;
		eyeRender.envParams.view = globalUBO.view;
		eyeRender.envParams.model = globalUBO.model;
		eyeRender.envParams.cameraPosWS = glm::vec4(camera.position.x, camera.position.y, camera.position.z, 1.0f);
		eyeRender.updateUniformEnvBuffers();
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
			if (overlay->inputFloat("refractionDepthScale", &eyeRender.eyeParams.refractionDepthScale, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("irisUVRadius", &eyeRender.eyeParams.irisUVRadius, 0.0001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}

			if (overlay->colorPicker("irisBleedTint", &pickIrisbleedColors[0])) {
				eyeRender.eyeParams.irisBleedTint = glm::vec4(pickIrisbleedColors[0], pickIrisbleedColors[1], pickIrisbleedColors[2], pickIrisbleedColors[3]);
				eyeRender.updateUniformEyeBuffers();
			}

			if (overlay->inputFloat("irisSaturation", &eyeRender.eyeParams.irisSaturation, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("aoInBaseColorPower", &eyeRender.eyeParams.aoInBaseColorPower, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("irisBorderWidth", &eyeRender.eyeParams.irisBorderWidth, 0.0001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("limbusDarkScale", &eyeRender.eyeParams.limbusDarkScale, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("limbusDarkPower", &eyeRender.eyeParams.limbusDarkPower, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("pupilScale", &eyeRender.eyeParams.pupilScale, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}

			if (overlay->inputFloat("pupilShiftX", &eyeRender.eyeParams.pupilShift.x, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("pupilShiftY", &eyeRender.eyeParams.pupilShift.y, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->colorPicker("scleraTint", &pickScleraColors[0])) {
				eyeRender.eyeParams.scleraTint = glm::vec4(pickScleraColors[0], pickScleraColors[1], pickScleraColors[2], pickScleraColors[3]);
				eyeRender.updateUniformEyeBuffers();
			}

			if (overlay->inputFloat("scleraBrightness", &eyeRender.eyeParams.scleraBrightness, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("scleraPower", &eyeRender.eyeParams.scleraPower, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("veinsPower", &eyeRender.eyeParams.veinsPower, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("veinsOpacity", &eyeRender.eyeParams.veinsOpacity, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
			if (overlay->inputFloat("highlightIntensity", &eyeRender.eyeParams.highlightIntensity, 0.001f, 5)) {
				eyeRender.updateUniformEyeBuffers();
			}
		}

		//global
		float refractionDepthScale;

		//iris
		float irisUVRadius;
		glm::vec4 irisBleedTint;
		float irisSaturation;
		float aoInBaseColorPower;
		float irisBorderWidth;
		float limbusDarkScale;
		float limbusDarkPower;
		float pupilScale;
		glm::vec2  pupilShift;

		//sclera
		glm::vec4 scleraTint;
		float scleraBrightness;
		float scleraPower;

		//Veins
		float veinsPower;
		float veinsOpacity;

		//highlight override
		float highlightIntensity;
	}
};

VULKAN_EXAMPLE_MAIN()
