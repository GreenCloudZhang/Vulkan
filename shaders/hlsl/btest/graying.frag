// Copyright 2020 Google LLC

Texture2D textureColor : register(t1);
SamplerState samplerColor : register(s1);

cbuffer UBO : register(b0)
{
	float tintColorR;
	float tintColorG;
	float tintColorB;
	float tintColorA;
};

float4 main([[vk::location(0)]] float2 inUV : TEXCOORD0) : SV_TARGET
{
	float4 result = textureColor.Sample(samplerColor, inUV).rgba;
	float gray = result.r*0.299 + result.g*0.587 + result.b*0.114;
	return float4(tintColorR * gray, tintColorG * gray, tintColorB * gray, tintColorA * gray);
	//return float4(1,0,0,1);
}