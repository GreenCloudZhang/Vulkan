#version 450

layout (location = 0) in vec4 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec3 inNormal;
layout (location = 4) in vec4 inTangent;

layout (binding = 0) uniform envUBO 
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec4 cameraPosWS;
	vec4 lightDir;
	vec4 lightColor;
} env_ubo;

layout (location = 0) out vec3 outNormalWS;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outColor;
layout (location = 3) out vec3 outPosWS;
layout (location = 4) out vec4 outTangentWS;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	outNormalWS = inNormal;
	outColor = inColor;
	outUV = inUV;
	gl_Position = env_ubo.projection * env_ubo.view * env_ubo.model * inPos;

	outPosWS = (env_ubo.model * inPos).xyz;
	outNormalWS = normalize(mat3(transpose(inverse(env_ubo.model))) * inNormal);
	outTangentWS = vec4(normalize(mat3(transpose(inverse(env_ubo.model))) * inTangent.xyz), inTangent.w);

	//half3 binormal = cross(v.normal, v.tangent.xyz) * v.tangent.w;
	//half3x3(tangent, binormal, normal)
}
