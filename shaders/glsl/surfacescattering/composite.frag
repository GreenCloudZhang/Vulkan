#version 450

layout (binding = 1) uniform sampler2D samplerDiffuse;
layout (binding = 2) uniform sampler2D samplerSpecular;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

void main() 
{
	vec3 diffuse = texture(samplerDiffuse, inUV).rgb;
	vec3 specular = texture(samplerSpecular, inUV).rgb;
	
  outFragcolor = vec4(diffuse+specular, 1.0);	
}