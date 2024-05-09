#version 450

layout (binding = 1) uniform sampler2D samplerColor;

layout (binding = 0) uniform UBO 
{
	float tintColorR;
	float tintColorG;
	float tintColorB;
	float tintColorA;
} ubo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec4 result = texture(samplerColor, inUV).rgba; 
    float gray = result.r*0.299 + result.g*0.587 + result.b*0.114;
	outFragColor = vec4(ubo.tintColorR * gray, ubo.tintColorG * gray, ubo.tintColorB * gray, ubo.tintColorA * gray);
	//#outFragColor = vec4(1,0,0,1);
}