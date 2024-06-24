#version 450

layout (binding = 1) uniform sampler2D samplerColor1;
layout (binding = 2) uniform sampler2D samplerColor2;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

void main() 
{
  vec2 uv = vec2(inUV.x, 1-inUV.y);
  if(uv.y > 0.5)//upper
  {
	outFragColor = texture(samplerColor1, vec2(uv.x, (uv.y-0.5) * 2));
  }
  else//bottom
  {
	outFragColor = texture(samplerColor2, vec2(uv.x, uv.y * 2));
  }
}