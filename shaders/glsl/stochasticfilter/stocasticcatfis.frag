#version 450

layout(binding=1)uniform sampler2D randomMap;
layout(binding=2)uniform sampler2D inColorMap;

layout(binding=0)uniform Params
{
	vec4 randomParams;
}params;
layout(location=0)in vec2 inUV;
layout(location=0)out vec4 outColor;


/////////
//      stochastic (bi)quadratic
//stochastic gaussian    stochastic bilinear
//      normal bilinear


float rand(float co) { return fract(sin(co*(91.3458)) * 47453.5453); }
float rand(vec2 co){ return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453); }

const float PI = 3.1415926;

vec2 boxMullerTransform(vec2 u)
{
    vec2 r;
    float mag = sqrt(-2.0 * log(u.x));
    return mag * vec2(cos(2.0 * PI * u.y), sin(2.0 * PI * u.y));
}

vec3 stochastic_gauss(in vec2 uv, in vec2 rand) {
    ivec2 texSize = textureSize(inColorMap, 0);
    vec2 orig_tex_coord = uv * texSize.xy - 0.5;
    vec2 uv_full = (round(orig_tex_coord + boxMullerTransform(rand)*0.5)+0.5) / texSize.xy;

    return texture(inColorMap, uv_full).xyz;  
}


vec3 stochastic_bilin(in vec2 uv, in vec2 rand) {
    ivec2 texSize = textureSize(inColorMap, 0);
    vec2 orig_tex_coord = uv * texSize.xy - 0.5;
    vec2 uv_full = (round(orig_tex_coord + rand - 0.5)+0.5) / texSize.xy;

    return texture(inColorMap, uv_full).xyz;  
}

// Inverse CDF sampling for a tent / bilinear kernel. Followed by rounding - nearest-neighbor box kernel,
// UV jittering this way produces a biquadratic B-Spline kernel.
// See the paper for the explanation: https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/
vec2 bilin_inverse_cdf_sample(vec2 x) {
    return mix(1.0 - sqrt(2.0 - 2.0 * x), -1.0 + sqrt(2.0 * x), step(x, vec2(0.5)));
}

vec3 stochastic_quadratic(in vec2 uv, in vec2 rand) {
    ivec2 texSize = textureSize(inColorMap, 0);
    vec2 orig_tex_coord = uv * texSize.xy - 0.5;
    vec2 uv_full = (round(orig_tex_coord + bilin_inverse_cdf_sample(rand))+0.5) / texSize.xy;

    return texture(inColorMap, uv_full).xyz;  
}


void main()
{
	vec4 random = texture(randomMap,inUV+vec2(rand(params.randomParams.x),rand(params.randomParams.y)));
    vec3 col = inUV.x > inUV.y ? stochastic_quadratic(inUV, random.xy) : stochastic_gauss(inUV, random.xy);

	if(1.0-inUV.x < inUV.y)
	{
		col = texture(inColorMap, inUV).xyz;
		if(inUV.x > inUV.y)
		{
			col = stochastic_bilin(inUV, random.xy);
		}
	}

	//split
	if(abs(1.0-inUV.x-inUV.y)<0.005)
	    col=vec3(1,1,1);
	if(abs(inUV.x-inUV.y)<0.005)
	    col=vec3(1,1,1);
	
	outColor=vec4(col,1.0);
}
