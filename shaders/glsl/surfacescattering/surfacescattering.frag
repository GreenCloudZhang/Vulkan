#version 450

layout (binding = 1) uniform sampler2D samplerDepth;
layout (binding = 2) uniform sampler2D samplerDiffuse;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

#define SamplerSteps 25   //less: quality lower

layout (binding = 4) uniform UBO 
{
	vec4 _ScreenSubsurfaceProps;
	vec4 _DepthTexelSize;//1/width,1/height,width,height
	vec4 _ZNear_Far;//near, far - near, 0, 0
    vec4 _ScreenSubsurfaceKernel[SamplerSteps];//xyz：rgb_pdf   w：define samplerPos _ScreenSubsurfaceKernel[0] is center
} ubo;

//scale
#define _SSSScale ubo._ScreenSubsurfaceProps.x
//1.0/tan(0.5(radians(fov)))
#define DistanceToProjectionWindow ubo._ScreenSubsurfaceProps.y
//DistanceToProjectionWindow*300
#define DPTimes300 ubo._ScreenSubsurfaceProps.z
#define CameraAspect ubo._ScreenSubsurfaceProps.w

layout(constant_id=0)const int blurdirection=0;//1:x, else:y

float SubsurfaceLinearEyeDepth(float z)//z 0-1
{
	return ubo._ZNear_Far.x + z * ubo._ZNear_Far.y;
}

vec3 SSS(vec3 sceneColor, float sceneDepth, vec2 uv, vec2 sssDirIntensity)
{
    float dp = DistanceToProjectionWindow;
	float dp300 = DPTimes300;
	if(blurdirection==1)
	{
	    dp = dp/CameraAspect;
	    dp300 = dp300/CameraAspect;
	}
	float blurLength = dp / sceneDepth;//far is short
	vec2 uvOffset = sssDirIntensity * blurLength;
	vec3 blurSceneColor = sceneColor;
	blurSceneColor.rgb *= ubo._ScreenSubsurfaceKernel[0].xyz;

	for(int i=0;i<SamplerSteps;i++)
	{
		vec2 sssUV = uv + ubo._ScreenSubsurfaceKernel[i].a * uvOffset;
		vec4 sssSceneColor = texture(samplerDiffuse, sssUV);
		float sssDepth = SubsurfaceLinearEyeDepth(texture(samplerDepth, sssUV).r);
		float dir = length(sssDirIntensity);

		float sssScale = clamp(dp300*dir*abs(sceneDepth-sssDepth),0.f, 1.f);
		sssSceneColor.rgb = mix(sssSceneColor.rgb, sceneColor.rgb, vec3(sssScale));//depth diff larger：more like sceneColor

		blurSceneColor += sssSceneColor.rgb * ubo._ScreenSubsurfaceKernel[i].xyz;
	}
	return blurSceneColor;
}

void main() 
{
    vec4 sceneColor = texture(samplerDiffuse, inUV);
	float sssIntensity;
	vec2 offsetDir;
	if(blurdirection == 1)//x direction
	{
		sssIntensity = _SSSScale*ubo._DepthTexelSize.x;
		offsetDir = vec2(sssIntensity,0);
	}
	else{
		sssIntensity = _SSSScale*ubo._DepthTexelSize.y;
		offsetDir = vec2(0,sssIntensity);
	}

	float sceneDepth = texture(samplerDepth, inUV).r;
	sceneDepth = SubsurfaceLinearEyeDepth(sceneDepth);
    
	vec3 blurRes = SSS(sceneColor.xyz, sceneDepth, inUV, offsetDir);
    outFragcolor = vec4(blurRes, 1.0);	
}