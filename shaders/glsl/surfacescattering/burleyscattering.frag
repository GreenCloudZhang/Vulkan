#version 450

layout (binding = 1) uniform sampler2D samplerDepth;
layout (binding = 2) uniform sampler2D samplerDiffuse;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

#define SamplerCount 32   //less: quality lower
#define ArrayCount 8
#define SSS_PIXELS_PER_SAMPLE 4

layout (binding = 4) uniform UBO 
{
	mat4 invProjMat;
	vec4 _DepthTexelSize;//1/width,1/height,width,height
	vec4 _BurleySubsurfaceParams;
	vec4 _ShapeParamsAndMaxScatterDists;//s d
	vec4 _ZNear_Far_zBufferParams;//near, far-near, zbuffer.x, zbuffer.y
	vec4 _Sample_r[ArrayCount];
	vec4 _Sample_rcpPdf[ArrayCount];
	vec4 _Sample_sinPhi[ArrayCount];
	vec4 _Sample_cosPhi[ArrayCount];
} ubo;

//sample r unit mm
#define _FilterRadius ubo._BurleySubsurfaceParams.x //cdf^-1(r) MAX_CDF //unit mm
#define _WorldScale ubo._BurleySubsurfaceParams.y

#define PI 3.1415926
#define TWO_PI 6.2831852
#define LOG2_E 1.44269504088896340736

//////RANDOM
uint JenkinsHash(uint x)
{
	x+=(x<<10);
	x^=(x>>6);
	x+=(x<<3);
	x^=(x>>11);
	x+=(x<<15);
	return x;
}
uint JenkinsHash(ivec2 v){
	return JenkinsHash(v.x^JenkinsHash(v.y));
}

float ConstructFloat(uint m) {
    const int ieeeMantissa = 0x007FFFFF; // Binary FP32 mantissa bitmask
    const int ieeeOne      = 0x3F800000; // 1.0 in FP32 IEEE

    m &= ieeeMantissa;                   // Keep only mantissa bits (fractional part)
    m |= ieeeOne;                        // Add fractional part to 1.0

    float  f = float(m);               //// Range [1, 2)
    return f - 1;                      //// Range [0, 1)
}

float GenerateHashedRandomFloat(ivec2 v)
{
    return ConstructFloat(JenkinsHash(v));
}
/////RANDOM

float DeviceDepth2Linear01(float d)
{
    return 1.0/(d * ubo._ZNear_Far_zBufferParams.z + ubo._ZNear_Far_zBufferParams.w + 0.00001f);
}

float SubsurfaceLinearEyeDepth(float z)//z 0-1
{
	return ubo._ZNear_Far_zBufferParams.x + z * ubo._ZNear_Far_zBufferParams.y;
}

vec4 ComputeClipSpacePosition(vec2 positionNDC, float deviceDepth)
{
	return vec4(positionNDC * 2.0 - 1.0, deviceDepth, 1.0);//attention flip y
}

//NDC->ViewSpace
vec3 SurfaceComputeViewSpacePosition(vec2 positionNDC, float deviceDepth, mat4 invProjMat)//deviceDepth -1,1
{
	vec4 positionCS = ComputeClipSpacePosition(positionNDC, deviceDepth);
	vec4 positionVS = invProjMat * positionCS;
	return positionVS.xyz/positionVS.w;
}

/////Burley sample
// Importance sample the normalized diffuse reflectance profile for the computed value of 's'.
// ------------------------------------------------------------------------------------
// R[r, phi, s]   = s * (Exp[-r * s] + Exp[-r * s / 3]) / (8 * Pi * r)
// PDF[r, phi, s] = r * R[r, phi, s]
// CDF[r, s]      = 1 - 1/4 * Exp[-r * s] - 3/4 * Exp[-r * s / 3]
// ------------------------------------------------------------------------------------
// We importance sample the color channel with the widest scattering distance.
//Calc F(r)
vec3 EvaluateBurleyDiffusionProfile(float r, vec3 S)
{
    vec3 exp_13 = exp2((LOG2_E*(-1.0/3.0)*r)*S);//exp[-S*r/3.0]
	vec3 expSum = exp_13*(1+exp_13*exp_13);//exp[-S*r/3.0]+exp[-S*r]
	return (S/(8*PI))*expSum;//S.(8 * PI)*(exp[-S*r/3.0]+exp[-S*r]);
}

void SampleBurleyDiffusionProfile(float u, float rcpS, out float r, out float rcpPdf)
{
    u = 1 - u; // Convert CDF to CCDF

    float g = 1 + (4 * u) * (2 * u + sqrt(1 + (4 * u) * u));
    float n = exp2(log2(g) * (-1.0 / 3.0));                    // g^(-1/3)
    float p = (g * n) * n;                                   // g^(+1/3)
    float c = 1 + p + n;                                     // 1 + g^(+1/3) + g^(-1/3)
    float d = (3 / LOG2_E * 2) + (3 / LOG2_E) * log2(u);     // 3 * Log[4 * u]
    float x = (3 / LOG2_E) * log2(c) - d;                    // 3 * Log[c / (4 * u)]

    // x      = s * r
    // exp_13 = Exp[-x/3] = Exp[-1/3 * 3 * Log[c / (4 * u)]]
    // exp_13 = Exp[-Log[c / (4 * u)]] = (4 * u) / c
    // exp_1  = Exp[-x] = exp_13 * exp_13 * exp_13
    // expSum = exp_1 + exp_13 = exp_13 * (1 + exp_13 * exp_13)
    // rcpExp = rcp(expSum) = c^3 / ((4 * u) * (c^2 + 16 * u^2))
    float rcpExp = ((c * c) * c) / ((4 * u) * ((c * c) + (4 * u) * (4 * u)));

    r = x * rcpS;
    rcpPdf = (8 * PI * rcpS) * rcpExp; // (8 * Pi) / s / (Exp[-s * r / 3] + Exp[-s * r])
}

vec3 ComputeBilateralWeight(float xy2, float z, float mmPerUnit, vec3 S, float rcpPdf)
{
	float r = sqrt(xy2 + (z * mmPerUnit)*(z * mmPerUnit));//translate to unit mm
//#if define SSS_USE_TANGENT_SPACE
//    float r = sqrt(xy2+z*z)*mmPerUnit;
//#endif
	return EvaluateBurleyDiffusionProfile(r,S)*rcpPdf;
}

void main() 
{
    vec2 uv = inUV;
	vec2 posSS = uv * ubo._DepthTexelSize.zw;//screen coord
	float depth = texture(samplerDepth, inUV).r;
	float linear01Depth = DeviceDepth2Linear01(depth);

	//pixel corner uv
	vec2 cornerPosNDC = uv + 0.5 * ubo._DepthTexelSize.xy;
	depth = depth * 2.0 - 1.0;//ndc -1,1
    
	vec3 centerPosVS = SurfaceComputeViewSpacePosition(uv, depth, ubo.invProjMat);
	vec3 cornerPosVS = SurfaceComputeViewSpacePosition(cornerPosNDC, depth, ubo.invProjMat);

	float mmPerUnit = 10.0f;//View space unit
	float unitsPerMM = 1.0f/10.f;
    float unitsPerPixel = max(0.0001f, 2.f* abs(cornerPosVS.x-centerPosVS.x)*_WorldScale);
	float pixelsPerMM = (1.f/unitsPerPixel)*unitsPerMM;

	//disk sampler2D
	//float filterArea = PI * pow(_FilterRadius*pixelsPerMM, 2.f);//unit pixel
	//uint sampleCount = uint(filterArea/SSS_PIXELS_PER_SAMPLE);

    vec3 S = ubo._ShapeParamsAndMaxScatterDists.xyz;
	float d = ubo._ShapeParamsAndMaxScatterDists.w;
    
	//another way: from random tex sampler
    float phase = TWO_PI * GenerateHashedRandomFloat(ivec2(posSS));
	float sinPha = sin(phase);
	float cosPha = cos(phase);
	vec3 totalIrradiance = vec3(0.f);
	vec3 totalWeight = vec3(0.f);
	float linearDepth = SubsurfaceLinearEyeDepth(linear01Depth);

	for(int i=0;i<SamplerCount;i++)
	{
		//this way: precompute in cpu, another way compute in shader
		float r, rcpPdf, sinPhi, cosPhi;
		int idx = i/4;
		int idy = i%4;
		r=ubo._Sample_r[idx][idy];
		rcpPdf = ubo._Sample_rcpPdf[idx][idy];
		sinPhi = ubo._Sample_sinPhi[idx][idy];
		cosPhi = ubo._Sample_cosPhi[idx][idy];
        
		//calc alpha : phase + phi
		float sinPsi = cosPha * sinPhi + sinPha * cosPhi;//sin(phase+phi)
		float cosPsi = cosPha * cosPhi - sinPha * sinPhi;//cos(phase+phi)
        
		vec2 positionSample = posSS + ivec2(round(pixelsPerMM * r))*vec2(cosPsi, sinPsi);//coord
		vec2 uvSample = positionSample * ubo._DepthTexelSize.xy;
		vec4 diffuseSample = texture(samplerDiffuse, uvSample);
		float depthSample = DeviceDepth2Linear01(texture(samplerDepth, uvSample).r);
		float linearZSample = SubsurfaceLinearEyeDepth(depthSample);

		vec3 weight = ComputeBilateralWeight(r*r, abs(linearZSample-linearDepth), mmPerUnit, S, rcpPdf);

		weight = depthSample == 1 ? vec3(0.f) : weight;// eliminate bacground pixel'
		totalIrradiance += weight * diffuseSample.rgb;
		totalWeight += weight;
	}
    totalWeight = max(totalWeight, 0.0000001f);	
	outFragcolor = vec4(totalIrradiance/totalWeight, linear01Depth);
}