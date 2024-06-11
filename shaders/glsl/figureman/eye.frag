#version 450

layout (binding = 0) uniform envUBO 
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec4 cameraPosWS;
	vec4 lightDir;
	vec4 lightColor;
} env_ubo;

layout (binding = 1) uniform eyeParamUBO 
{
    vec4 irisBleedTint;
    vec4  pupilShift;
    vec4  scleraTint;
    //global
	float refractionDepthScale;

	//iris
	float irisUVRadius;
	//vec4 irisBleedTint;
	float irisSaturation;
	float aoInBaseColorPower;
	float irisBorderWidth;
	float limbusDarkScale;
	float limbusDarkPower;
	float pupilScale;
	//vec2  pupilShift;

	//sclera
	//vec4 scleraTint;
	float scleraBrightness;
	float scleraPower;

	//Veins
	float veinsPower;
	float veinsOpacity;

	//highlight override
	float highlightIntensity;
} eye_params;

//global
layout (binding = 2) uniform sampler2D midPlaneDisplacementMap;
layout (binding = 3) uniform sampler2D normalMap;

//iris
layout (binding = 4) uniform sampler2D irisBaseMap;
layout (binding = 5) uniform sampler2D irisOcculusionMap;

//sclera
layout (binding = 6) uniform sampler2D scleraBaseMap;

//Veins
layout (binding = 7) uniform sampler2D veinsBaseMap;

//Veins
layout (binding = 8) uniform sampler2D highlightMap;

layout (location = 0)in vec3 inNormalWS;
layout (location = 1)in vec2 inUV;
layout (location = 2)in vec3 inColor;
layout (location = 3)in vec3 inPosWS;
layout (location = 4)in vec4 inTangentWS;

layout (location = 0) out vec4 outFragColor;


#define EPSILON 0.000001f
#define HALF_MIN 6.103515625e-5  // 2^-14, the same value for 10, 11 and 16-bit: https://www.khronos.org/opengl/wiki/Small_Float_Formats
#define HALF_MIN_SQRT 0.0078125  // 2^-7 == sqrt(HALF_MIN), useful for ensuring HALF_MIN after x^2
#define FLT_EPS  5.960464478e-8  // 2^-24, machine epsilon: 1 + EPS = 1 (half of the ULP for 1.0f)

vec3 SafeNormalize(vec3 inVec)
{
    float dp3 = max(EPSILON, dot(inVec, inVec));
    return inVec / sqrt(dp3); // no rsqrt
}

vec2 SafeNormalizeVec2(vec2 inVec)
{
    float dp2 = max(EPSILON, dot(inVec, inVec));
    return inVec / sqrt(dp2); // no rsqrt
}

vec3 CalcRefractionDirection(float internalIoR, vec3 normalW, vec3 cameraW)
{
    float airIoR = 1.00029;
    float n = airIoR / (internalIoR + EPSILON);
    float facing = dot(normalW, cameraW);
    float w = n * facing;
    float k = sqrt(max(0.0f, 1 + (w - n) * (w + n)));
    vec3 t;
    t = (w - k) * normalW - n * cameraW;
    t = SafeNormalize(t);
    return -t;
}


float IrisMask(float IrisUVRadius, vec2 UV, float LimbusUVWidth)
{
    // Iris Mask with Limbus Ring falloff
    UV = UV - vec2(0.5f, 0.5f);

    float m;
    float r;
    float luv = length(UV);
    r = (luv - (IrisUVRadius - LimbusUVWidth)) / (LimbusUVWidth + EPSILON);
    //r = (length(UV) - (IrisUVRadius - LimbusUVWidth)) / LimbusUVWidth;
    m = clamp(1.0f - r, 0.0f, 1.0f);
    m = smoothstep(0.0f, 1.0f, m);
    return m;
}

void EyeRefraction(float internalIor, vec2 uv, float limbusUVWidth, float depthScale, 
    float depthPlaneOffset, float midPlaneDisplacement, vec3 eyeDirectionWS, float irisUVRadius,
    vec3 normalWS, vec3 viewWS, vec3 tangentWS,
    inout float irisMask, inout vec2 refractedUV)
{
    float irisDepth = max(midPlaneDisplacement - depthPlaneOffset, 0) * depthScale;
    float eyeDotView = dot(eyeDirectionWS, viewWS);
    float eyeDotViewFacing = (eyeDotView * eyeDotView);
    //mix(0.325, 1, x);
    eyeDotViewFacing = 0.675 * eyeDotViewFacing + 0.325;
    float irisDepthScaled = irisDepth / eyeDotViewFacing;

    vec3 refractedDir =  CalcRefractionDirection(internalIor, normalWS, viewWS);
    vec3 scaledRD = refractedDir * irisDepthScaled;
    //tangent space coord
    float tangentDotEye = dot(tangentWS, eyeDirectionWS);
    vec3 eyeTangent = SafeNormalize(tangentWS - tangentDotEye * eyeDirectionWS);
    vec3 bitangent = normalize(cross(eyeTangent, eyeDirectionWS));
    vec2 refractedUVOffset = vec2(dot(eyeTangent, scaledRD), dot(bitangent, scaledRD));

    irisMask = IrisMask(irisUVRadius, uv, limbusUVWidth);//ATTENTION
    vec2 offsetInIris = vec2(-1, 1) * irisUVRadius * refractedUVOffset;

    refractedUV = mix(uv, offsetInIris + uv, irisMask);
    //float transparency = length(offsetInIris) - irisUVRadius;
}

vec2 ScalePupils(vec2 UV, float PupilScale, float PupilShiftX, float PupilShiftY)
{
    // Scale UVs from from unit circle in or out from center
    // vec2 UV, float PupilScale

    float ShiftMask = pow(clamp(2 * ((length(vec2(0.5,0.5) - UV) - 0.45f) / -0.5f), 0.0f, 1.0f), 0.7);
    PupilShiftX *= ShiftMask * -0.1f;
    PupilShiftY *= ShiftMask * 0.1f;
    vec2 UVshifted = UV + vec2(PupilShiftX, PupilShiftY);
    vec2 UVcentered = UVshifted - vec2(0.5f, 0.5f);
    float UVlength = length(UVcentered);
    // UV on circle at distance 0.5 from the center, in direction of original UV
    vec2 UVmax = SafeNormalizeVec2(UVcentered) * 0.5f;

    vec2 UVscaled = mix(UVmax, vec2(0.0f, 0.0f), clamp((1.f - UVlength * 2.f) * PupilScale, 0.0f, 1.0f)) + vec2(0.5f, 0.5f);

    return UVscaled;
}

void GetScaledUVByCenter(float irisUVRadius, vec2 refractedUV, float pupilScale, float pupilShiftX, float pupilShiftY, inout vec2 outputUV)
{
    vec2 uv = (0.5f / (irisUVRadius + EPSILON)) * (refractedUV - vec2(0.5, 0.5)) + vec2(0.5, 0.5);
    outputUV = ScalePupils(uv, pupilScale, pupilShiftX, pupilShiftY);
}

void Desaturation(vec3 inColor, float fraction, vec3 luminanceFactors, inout vec3 outColor)
{
    outColor = mix(inColor, vec3(dot(inColor, luminanceFactors)), vec3(fraction));
}

vec2 CartesianToPolar(vec3 inDir)//panoramagram
{
    vec2 uv;
    uv.x = atan(inDir.z/inDir.x) / (2. * 3.1415926) + 0.5;
    uv.y = asin(inDir.y) / 3.1415926 + 0.5;
    return uv;
}


////***********************shading************************//

#define kDielectricSpec vec4(0.04, 0.04, 0.04, 1.0 - 0.04)

struct Light
{
    vec3 color;
    vec3 direction;
    float distanceAttenuation;
    float shadowAttenuation;
};

float PerceptualSmoothnessToPerceptualRoughness(float perceptualSmoothness)
{
    return (1.0 - perceptualSmoothness);
}

float PerceptualRoughnessToRoughness(float perceptualRoughness)
{
    return perceptualRoughness * perceptualRoughness;
}

vec3 GlobalIllumination(vec3 diffuse, vec3 specular, float roughness2, float grazingTerm, float perceptualRoughness,
    //float anisotropy, vec3 anisotropicT, vec3 anisotropicB,
    vec3 bakedGI, float occlusion,
    vec3 normalWS, vec3 viewDirectionWS, vec3 positionWS, vec3 highlight)
{
    vec3 reflectVector = reflect(-viewDirectionWS, normalWS);
    //if (anisotropy != 0)
    //{   
    //    vec3 anisotropyDirection = anisotropy >= 0.0 ? anisotropicB : anisotropicT;
    //    vec3 anisotropicTangent = cross(anisotropyDirection, viewDirectionWS);
    //    vec3 anisotropicNormal = cross(anisotropicTangent, anisotropyDirection);
    //    float bendFactor = abs(anisotropy) * saturate(5.0 * perceptualRoughness);
    //    vec3 bentNormal = normalize(mix(normalWS, anisotropicNormal, bendFactor));
    //
    //    reflectVector = reflect(-viewDirectionWS, bentNormal);
    //}
    float NoV = clamp(dot(normalWS, viewDirectionWS), 0.0f, 1.0f);
    float fresnelTerm = pow((1.0 - NoV), 4.0);
    vec3 indirectDiffuse = bakedGI * occlusion;
    vec3 indirectSpecular = highlight;//GlossyEnvironmentReflection(reflectVector, positionWS, perceptualRoughness, occlusion);

    vec3 color = indirectDiffuse * diffuse;
    float surfaceReduction = 1.0 / (roughness2 + 1.0);
    color += indirectSpecular * surfaceReduction * mix(specular, vec3(grazingTerm), vec3(fresnelTerm));
    return color;
}

void GetBSDFAngle(vec3 V, vec3 L, float NdotL, float NdotV,
                  out float LdotV, out float NdotH, out float LdotH, out float invLenLV)
{
    // Optimized math. Ref: PBR Diffuse Lighting for GGX + Smith Microsurfaces (slide 114), assuming |L|=1 and |V|=1
    LdotV = dot(L, V);
    invLenLV = 1.0 / sqrt(max(2.0 * LdotV + 2.0, FLT_EPS));    // invLenLV = rcp(length(L + V)), clamp to avoid rsqrt(0) = inf, inf * 0 = NaN
    NdotH = clamp((NdotL + NdotV) * invLenLV, 0.0f, 1.0f);
    LdotH = clamp(invLenLV * LdotV + invLenLV, 0.0f, 1.0f);
}

//FGD
float F_Schlick(float f0, float f90, float u)
{
    float x = 1.0 - u;
    float x2 = x * x;
    float x5 = x * x2 * x2;
    return (f90 - f0) * x5 + f0;                // sub mul mul mul sub mad
}

vec3 FresnelTerm(vec3 specularColor, float vdoth)
{
    vec3 fresnel = specularColor + (1. - specularColor) * pow((1. - vdoth), 5.);
    return fresnel;
}

float DisneyDiffuseNoPI(float NdotV, float NdotL, float LdotV, float perceptualRoughness)
{
    // (2 * LdotH * LdotH) = 1 + LdotV
    // float fd90 = 0.5 + (2 * LdotH * LdotH) * perceptualRoughness;
    float fd90 = 0.5 + (perceptualRoughness + perceptualRoughness * LdotV);
    // Two schlick fresnel term
    float lightScatter = F_Schlick(1.0, fd90, NdotL);
    float viewScatter = F_Schlick(1.0, fd90, NdotV);

    // Normalize the BRDF for polar view angles of up to (Pi/4).
    // We use the worst case of (roughness = albedo = 1), and, for each view angle,
    // integrate (brdf * cos(theta_light)) over all light directions.
    // The resulting value is for (theta_view = 0), which is actually a little bit larger
    // than the value of the integral for (theta_view = Pi/4).
    // Hopefully, the compiler folds the constant together with (1/Pi).
    return 1.0 / 1.03571 * (lightScatter * viewScatter);
}

//ANISOTROPIC
void ConvertValueAnisotropyToValueTB(float value, float anisotropy, out float valueT, out float valueB)
{
    // Use the parametrization of Sony Imageworks.
    // Ref: Revisiting Physically Based Shading at Imageworks, p. 15.
    valueT = value * (1 + anisotropy);
    valueB = value * (1 - anisotropy);
}

void ConvertAnisotropyToRoughness(float perceptualRoughness, float anisotropy, out float roughnessT, out float roughnessB)
{
    float roughness = PerceptualRoughnessToRoughness(perceptualRoughness);
    ConvertValueAnisotropyToValueTB(roughness, anisotropy, roughnessT, roughnessB);
}

float ClampRoughnessForAnalyticalLights(float roughness)
{
    return max(roughness, 1.0 / 1024.0);
}

void ConvertAnisotropyToClampRoughness(float perceptualRoughness, float anisotropy, out float roughnessT, out float roughnessB)
{
    ConvertAnisotropyToRoughness(perceptualRoughness, anisotropy, roughnessT, roughnessB);

    roughnessT = ClampRoughnessForAnalyticalLights(roughnessT);
    roughnessB = ClampRoughnessForAnalyticalLights(roughnessB);
}

float GetSmithJointGGXAnisoPartLambdaV(float TdotV, float BdotV, float NdotV, float roughnessT, float roughnessB)
{
    return length(vec3(roughnessT * TdotV, roughnessB * BdotV, NdotV));
}

float DV_SmithJointGGXAnisoNoPI(float TdotH, float BdotH, float NdotH, float NdotV,
                           float TdotL, float BdotL, float NdotL,
                           float roughnessT, float roughnessB, float partLambdaV)
{
    float a2 = roughnessT * roughnessB;
    vec3 v = vec3(roughnessB * TdotH, roughnessT * BdotH, a2 * NdotH);
    float s = dot(v, v);

    float lambdaV = NdotL * partLambdaV;
    float lambdaL = NdotV * length(vec3(roughnessT * TdotL, roughnessB * BdotL, NdotL));

    vec2 D = vec2(a2 * a2 * a2, s * s); // Fraction without the multiplier (1/Pi)
    vec2 G = vec2(1, lambdaV + lambdaL); // Fraction without the multiplier (1/2)

    // This function is only used for direct lighting.
    // If roughness is 0, the probability of hitting a punctual or directional light is also 0.
    // Therefore, we return 0. The most efficient way to do it is with a max().
    return (0.5) * (D.x * G.x) / max(D.y * G.y, HALF_MIN);
}

//


vec3 LWABRDF(Light light, vec3 normal, vec3 tangent, vec3 bitangent, vec3 view, vec3 baseColor, vec3 specularColor, float perceptualRoughness, float roughness2, float roughness2MinusOne, float normalizationTerm, float anisotropy)
{
    float roughness = PerceptualRoughnessToRoughness(perceptualRoughness);
    float NdotL = clamp(dot(normal, light.direction), 0.0f, 1.0f);
    float NdotV = max(dot(normal, view), 0.0001);
    
    float LdotV, NdotH, LdotH, invLenLV;
    GetBSDFAngle(view, light.direction, NdotL, NdotV, LdotV, NdotH, LdotH, invLenLV);
    
    vec3 H = SafeNormalize((light.direction + view));
  
    NdotH = clamp(dot(normal, H), 0.0f, 1.0f);
    LdotH = clamp(dot(light.direction, H), 0.0f, 1.0f);

    float diffTerm;
    vec3 specTerm;

    // Use abs NdotL to evaluate diffuse term also for transmission
    // TODO: See with Evgenii about the clampedNdotV here. This is what we use before the refactor
    // but now maybe we want to revisit it for transmission
    diffTerm = DisneyDiffuseNoPI(NdotV, abs(NdotL), LdotV, perceptualRoughness);

    // Fabric are dieletric but we simulate forward scattering effect with colored specular (fuzz tint term)
    vec3 F = FresnelTerm(specularColor, LdotH); //F_Schlick(specularColor, LdotH);
    
    if (anisotropy != 0 )//anisotropic material
    {   
    // For anisotropy we must not saturate these values
        float TdotH = dot(tangent, H);
        float TdotL = dot(tangent, light.direction);
        float BdotH = dot(bitangent, H);
        float BdotL = dot(bitangent, light.direction);

        float TdotV = dot(tangent, view);
        float BdotV = dot(bitangent, view);

        float roughnessB;
        float roughnessT;

    // TdotH = max(TdotH, 0.01);

        ConvertAnisotropyToClampRoughness(perceptualRoughness, anisotropy, roughnessT, roughnessB);

    // TODO: Do comparison between this correct version and the one from isotropic and see if there is any visual difference
    // We use abs(NdotL) to handle the none case of double sided
        float partLambdaV = GetSmithJointGGXAnisoPartLambdaV(TdotV, BdotV, NdotV, roughnessT, roughnessB);

    
        float DV = DV_SmithJointGGXAnisoNoPI(TdotH, BdotH, NdotH, NdotV, TdotL, BdotL, abs(NdotL),
                                    roughnessT, roughnessB, partLambdaV);

        specTerm = F * DV;

        return (diffTerm * baseColor + specTerm) * light.color * (NdotL * light.distanceAttenuation * light.shadowAttenuation);
    }
    else
    {
        float NoH = NdotH;
        float LoH = LdotH;

    // GGX Distribution multiplied by combined approximation of Visibility and Fresnel
    // BRDFspec = (D * V * F) / 4.0
    // D = roughness^2 / ( NoH^2 * (roughness^2 - 1) + 1 )^2
    // V * F = 1.0 / ( LoH^2 * (roughness + 0.5) )
    // See "Optimizing PBR for Mobile" from Siggraph 2015 moving mobile graphics course
    // https://community.arm.com/events/1155

    // Final BRDFspec = roughness^2 / ( NoH^2 * (roughness^2 - 1) + 1 )^2 * (LoH^2 * (roughness + 0.5) * 4.0)
    // We further optimize a few light invariant terms
    // brdfData.normalizationTerm = (roughness + 0.5) * 4.0 rewritten as roughness * 4.0 + 2.0 to a fit a MAD.
        float d = NoH * NoH * roughness2MinusOne + 1.00001f;

        float LoH2 = LoH * LoH;
        float sTerm = roughness2 / ((d * d) * max(0.1f, LoH2) * normalizationTerm);
        specTerm = vec3(sTerm, sTerm, sTerm);
        return (baseColor + specTerm * specularColor) * light.color * (NdotL * light.distanceAttenuation * light.shadowAttenuation);
    }
}

vec3 LWBRDF(Light light, vec3 normal, vec3 view, vec3 baseColor, vec3 specularColor, float perceptualRoughness, float roughness2, float roughness2MinusOne, float normalizationTerm)
{
    float roughness = PerceptualRoughnessToRoughness(perceptualRoughness);
    float NdotL = clamp(dot(normal, light.direction), 0.0f, 1.0f);
    float NdotV = max(dot(normal, view), 0.0001);
    
    float LdotV, NdotH, LdotH, invLenLV;
    GetBSDFAngle(view, light.direction, NdotL, NdotV, LdotV, NdotH, LdotH, invLenLV);
    
    vec3 H = SafeNormalize((light.direction + view));
  
    NdotH = clamp(dot(normal, H), 0.0f, 1.0f);
    LdotH = clamp(dot(light.direction, H), 0.0f, 1.0f);

    float diffTerm;
    vec3 specTerm;

    // Use abs NdotL to evaluate diffuse term also for transmission
    // TODO: See with Evgenii about the clampedNdotV here. This is what we use before the refactor
    // but now maybe we want to revisit it for transmission
    diffTerm = DisneyDiffuseNoPI(NdotV, abs(NdotL), LdotV, perceptualRoughness);

    // Fabric are dieletric but we simulate forward scattering effect with colored specular (fuzz tint term)
    vec3 F = FresnelTerm(specularColor, LdotH); //F_Schlick(specularColor, LdotH);

    float NoH = NdotH;
    float LoH = LdotH;

    // GGX Distribution multiplied by combined approximation of Visibility and Fresnel
    // BRDFspec = (D * V * F) / 4.0
    // D = roughness^2 / ( NoH^2 * (roughness^2 - 1) + 1 )^2
    // V * F = 1.0 / ( LoH^2 * (roughness + 0.5) )
    // See "Optimizing PBR for Mobile" from Siggraph 2015 moving mobile graphics course
    // https://community.arm.com/events/1155

    // Final BRDFspec = roughness^2 / ( NoH^2 * (roughness^2 - 1) + 1 )^2 * (LoH^2 * (roughness + 0.5) * 4.0)
    // We further optimize a few light invariant terms
    // brdfData.normalizationTerm = (roughness + 0.5) * 4.0 rewritten as roughness * 4.0 + 2.0 to a fit a MAD.
    float d = NoH * NoH * roughness2MinusOne + 1.00001f;

    float LoH2 = LoH * LoH;
    float sTerm = roughness2 / ((d * d) * max(0.1f, LoH2) * normalizationTerm);
    specTerm = vec3(sTerm, sTerm, sTerm);
    return (baseColor + specTerm * specularColor) * light.color * (NdotL * light.distanceAttenuation * light.shadowAttenuation);
}


void EyeShading(vec3 positionWS, vec3 normalWS, vec3 viewDirectionWS, vec4 albedo, float smoothness, float metallic, float occlusion, vec4 highlightOverride, inout vec3 color)
{
    color = vec3(0,0,0);
    float oneMinusReflectivity = kDielectricSpec.a - kDielectricSpec.a * metallic;
    float reflectivity = 1.0 - oneMinusReflectivity;
    vec3 diffuseColor = albedo.rgb * oneMinusReflectivity;
    vec3 specularColor = mix(kDielectricSpec.rgb, albedo.rgb, metallic);

    float perceptualRoughness = max(PerceptualSmoothnessToPerceptualRoughness(smoothness), HALF_MIN);
    float roughness = max(PerceptualRoughnessToRoughness(perceptualRoughness), HALF_MIN_SQRT);
    float roughness2 = max(roughness * roughness, HALF_MIN);
    float roughness2MinusOne = roughness2 - 1.0f;
    float normalizationTerm = roughness * 4.0f + 2.0f;
    float grazingTerm = clamp(smoothness + reflectivity, 0.0f, 1.0f);

    Light mainLight;
    mainLight.color = env_ubo.lightColor.rgb;
    mainLight.direction = env_ubo.lightDir.xyz;
    mainLight.distanceAttenuation = 1.0;
    mainLight.shadowAttenuation = 1.0;

    vec3 bakedGI = vec3(0.2,0.2,0.2);

    color = GlobalIllumination(diffuseColor, specularColor, roughness2, grazingTerm, perceptualRoughness,
    bakedGI, occlusion, normalWS, viewDirectionWS, positionWS, highlightOverride.rgb);
    color += LWBRDF(mainLight, normalWS, viewDirectionWS, diffuseColor, specularColor, perceptualRoughness, roughness2, roughness2MinusOne, normalizationTerm);
}





void main()
{   
    vec3 N = normalize(mat3(transpose(inverse(env_ubo.model))) * inNormalWS);
	vec3 T = normalize(mat3(transpose(inverse(env_ubo.model))) * inTangentWS.xyz);
    T = normalize(T-dot(T,N)*N);
	vec3 B = normalize(cross(N, T))*inTangentWS.w;
	mat3 TBN = transpose(mat3(T, B, N));

	//NORMAL TBN
	vec3 normalTS = normalize(texture(normalMap, inUV).xyz * 2.0 - 1.0);
	//vec3 binormalWS = normalize(cross(inNormalWS, inTangentWS.xyz) * inTangentWS.w);
	//mat3 tangent2normal = mat3(normalize(inTangentWS.xyz), binormalWS, normalize(inNormalWS));
	vec3 normalWS = TBN * normalTS;

	//VIEW
	vec3 viewWS = normalize(env_ubo.cameraPosWS.xyz - inPosWS);

	float internalIor = 1.33;//index of refraction
	vec2 uv = inUV;
	float limbusUVWidth = eye_params.irisBorderWidth;
	float depthScale = eye_params.refractionDepthScale;
	float midPlaneDisplacement = texture(midPlaneDisplacementMap, inUV).r * 2.0f;
	vec3 eyeDirectionWS = N;
	float irisUVRadius = eye_params.irisUVRadius;

	float depthPlaneOffset = texture(midPlaneDisplacementMap, vec2(irisUVRadius + 0.5, 0.5)).r * 2.0f;

	float irisMask;
	vec2 refractedUV;
	EyeRefraction(internalIor, uv, limbusUVWidth, depthScale, depthPlaneOffset, midPlaneDisplacement, eyeDirectionWS, irisUVRadius, N, viewWS, inTangentWS.xyz, irisMask, refractedUV);

	float pupilScale = eye_params.pupilScale;
	float pupilShiftX = eye_params.pupilShift.x;
	float pupilShiftY = eye_params.pupilShift.y;

	vec2 scaledUV;
	GetScaledUVByCenter(irisUVRadius, refractedUV, pupilScale, pupilShiftX, pupilShiftY, scaledUV);

	vec3 lerpSclera1A = pow(texture(scleraBaseMap, inUV).rgb * vec3(eye_params.scleraBrightness) * eye_params.scleraTint.rgb, vec3(eye_params.scleraPower));
	vec3 lerpSclera1B = lerpSclera1A * eye_params.irisBleedTint.rgb;
	vec3 lerpSclera2A = mix(lerpSclera1A, lerpSclera1B, irisMask);//irisBleed
	vec3 lerpSclera2B = pow(texture(veinsBaseMap, inUV).rgb, vec3(eye_params.veinsPower));

	vec3 lerpAlbedoA = mix(lerpSclera2A, lerpSclera2B, eye_params.veinsOpacity);//add veinTex

	vec3 irisDetail = texture(irisBaseMap, scaledUV).rgb * clamp(1.0 - pow(length((scaledUV - vec2(0.5)) * eye_params.limbusDarkScale), eye_params.limbusDarkPower), 0.0f, 1.0f);
	vec3 irisAO = pow(texture(irisOcculusionMap, scaledUV).rgb, vec3(eye_params.aoInBaseColorPower));

	vec3 lerpAlbedoB;
	Desaturation(irisDetail * irisAO, 1 - eye_params.irisSaturation, vec3(0.3, 0.59, 0.11), lerpAlbedoB);

	vec3 albedo = mix(lerpAlbedoA, lerpAlbedoB, irisMask);
	float smoothness = 0.92f;
	float metallic = 0.0f;
	float occlusion = 1.0f;

	vec4 highlightSampler = texture(highlightMap, CartesianToPolar(reflect(viewWS, N)));
	vec3 decodeHighlightHDR = highlightSampler.xyz * highlightSampler.w * 6.0f;
	vec4 highlight = vec4(decodeHighlightHDR * eye_params.highlightIntensity, 1.0);

	//shading
	vec3 shadingRes;
	EyeShading(inPosWS, N, viewWS, vec4(albedo, 1.0), smoothness, metallic, occlusion, highlight, shadingRes);

	outFragColor = vec4(shadingRes, 1.0);
}