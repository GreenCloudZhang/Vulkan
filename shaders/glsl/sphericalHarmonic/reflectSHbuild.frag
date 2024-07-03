#version 450

layout (binding = 1) uniform samplerCube samplerColor;

layout (binding = 0) uniform UBO
{
	mat4 projection;
	mat4 model;
	mat4 invModel;
	vec4  controlP;//x:band y:sampleNum
} ubo;

layout(std430, binding = 2) buffer SHCoefficientIn {
	vec4 shCoefficientIn[ ];
};

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;

layout (location = 0) out vec4 outFragColor;

#define PI 3.14159265358979
#define kCacheSize 13
//0！ 1！ 2！...
int Factorial(int x)
{
	const int factorial_cache[kCacheSize]={1,1,2,6,24,120,720,5040,40320, 362880, 3628800, 39916800, 479001600};
	if(x<kCacheSize)
	{
		return factorial_cache[x];
	}
	else
	{
        int s = factorial_cache[kCacheSize-1];
		for(int n = kCacheSize; n<=x; n++)
	    {
			s *= n;
		}
		return s;
	}
}

//legendre polynomial
//x = cos(theta)
float P(int l, int m, float x)
{
    float pmm = 1.f;//l==m
	if(m>0)
	{
		float somx2 = sqrt((1.0-x)*(1.0+x));
		float fact = 1.f;
		for(int i=0;i<=m;i++)
		{
			pmm *= (-fact)*somx2;
			fact += 2.f;
		}
	}
	if(l == m)return pmm;
	float pmmp1 = x*(2.0*m+1.0)*pmm;//l==m+1
	if(l==m+1)return pmmp1;
	float pll = 0.0;
	for(int ll=m+2;ll<=l;ll++)//ll:temp l
	{
		pll = ((2.0*ll-1.0)*x*pmmp1-(ll+m-1.0)*pmm)/(ll-m);
		pmm = pmmp1;
		pmmp1 = pll;
	}
	return pll;
}

//scale coefficient
//input：m > 0
float K(int l, int m)
{
	float temp = ((2.0*l+1.0)*Factorial(l-m))/(4.0*PI*Factorial(l+m));
	return sqrt(temp);
}

//l is band
//m in the range [-l,...l]
//theta in the range [0, pi]
//phi in the range [0, 2pi]
float SH(int l, int m, float theta, float phi)
{
	float sqrt2 = sqrt(2.0);
	if(m==0)return K(l,0)*P(l,0,cos(theta));
	else if(m>0) return sqrt2*K(l,m)*cos(m*phi)*P(l,m,cos(theta));
	else return sqrt2*K(l,-m)*sin(-m*phi)*P(l,-m,cos(theta));
}

float clampTheta(float theta)
{
    return theta;
}

float clampPhi(float phi)
{
    return phi + PI;
}

void main()
{
	vec3 cI = normalize (inPos);
	vec3 cR = reflect (cI, normalize(inNormal));

	cR = normalize(vec3(ubo.invModel * vec4(cR, 0.0)));//RIGHT HAND
	// Convert cubemap coordinates into Vulkan coordinate space
	//cR.xy *= -1.0;

	float theta = clampTheta(acos(cR.z));//RETURN [0,PI]
	float phi = clampPhi(atan(cR.y/cR.x));//RETURN [-PI,PI]
	int n_band = int(ubo.controlP.x);
	int n_coeff = n_band*n_band;

	vec4 color = vec4(0,0,0,1);
	//for(int l=0;l<n_band;l++)
	//{
	//	for(int m=-l;m<=l;m++)
	//	{
	//		int index = l*(l+1)+m;
	//		color.rgb += shCoefficientIn[index].xyz * SH(l, m, theta, phi);
	//	}
	//}
	float x = cR.x;
    float y = cR.y;
    float z = cR.z;
    float x2 = x*x;
    float y2 = y*y;
    float z2 = z*z;
    
	float basis[16];
    //basis[0]  = 1.f / 2.f * sqrt(1.f / PI);

	//right HAND
    //basis[1]  = sqrt(3.f / (4.f*PI))*y;
    //basis[2]  = sqrt(3.f / (4.f*PI))*z;
    //basis[3]  = sqrt(3.f / (4.f*PI))*x;

    //basis[4]  = 1.f / 2.f * sqrt(15.f / PI) * x * y;
    //basis[5]  = 1.f / 2.f * sqrt(15.f / PI) * z * y;
    //basis[6]  = 1.f / 4.f * sqrt(5.f / PI) * (3*z2-1);
    //basis[7]  = 1.f / 2.f * sqrt(15.f / PI) * z * x;
    //basis[8]  = 1.f / 4.f * sqrt(15.f / PI) * (x*x - y*y);

    //basis[9]  = 1.f / 4.f * sqrt(35.f / (2.f*PI))*(3 * x2 - y2)*y;
    //basis[10] = 1.f / 2.f * sqrt(105.f / PI)*x*y*z;
    //basis[11] = 1.f / 4.f * sqrt(21.f / (2.f*PI))*y*(4 * z2 - x2 - y2);
    //basis[12] = 1.f / 4.f * sqrt(7.f / PI)*z*(2 * z2 - 3 * x2 - 3 * y2);
    //basis[13] = 1.f / 4.f * sqrt(21.f / (2.f*PI))*x*(4 * z2 - x2 - y2);
    //basis[14] = 1.f / 4.f * sqrt(105.f / PI)*(x2 - y2)*z;
    //basis[15] = 1.f / 4.f * sqrt(35.f / (2 * PI))*(x2 - 3 * y2)*x;

	//left hand switch y & z
	basis[0]  = 1.f / 2.f * sqrt(1.f / PI);
    basis[1]  = sqrt(3.f / (4.f*PI))*z;
    basis[2]  = sqrt(3.f / (4.f*PI))*y;
    basis[3]  = sqrt(3.f / (4.f*PI))*x;
    basis[4]  = 1.f / 2.f * sqrt(15.f / PI) * x * z;
    basis[5]  = 1.f / 2.f * sqrt(15.f / PI) * z * y;
    basis[6]  = 1.f / 4.f * sqrt(5.f / PI) * (-x*x - z*z + 2 * y*y);
    basis[7]  = 1.f / 2.f * sqrt(15.f / PI) * y * x;
    basis[8]  = 1.f / 4.f * sqrt(15.f / PI) * (x*x - z*z);
    basis[9]  = 1.f / 4.f * sqrt(35.f / (2.f*PI))*(3 * x2 - z2)*z;
    basis[10] = 1.f / 2.f * sqrt(105.f / PI)*x*z*y;
    basis[11] = 1.f / 4.f * sqrt(21.f / (2.f*PI))*z*(4 * y2 - x2 - z2);
    basis[12] = 1.f / 4.f * sqrt(7.f / PI)*y*(2 * y2 - 3 * x2 - 3 * z2);
    basis[13] = 1.f / 4.f * sqrt(21.f / (2.f*PI))*x*(4 * y2 - x2 - z2);
    basis[14] = 1.f / 4.f * sqrt(105.f / PI)*(x2 - z2)*y;
    basis[15] = 1.f / 4.f * sqrt(35.f / (2 * PI))*(x2 - 3 * z2)*x;

    for (int i = 0; i < 16; i++)
    {
        color.xyz += shCoefficientIn[i].xyz * basis[i];
    }

	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 R = reflect(-L, N);
	vec3 ambient = vec3(0.5) * color.rgb;
	vec3 diffuse = max(dot(N, L), 0.0) * vec3(1.0);
	vec3 specular = pow(max(dot(R, V), 0.0), 16.0) * vec3(0.5);
	outFragColor = vec4(ambient + diffuse * color.rgb + specular, 1.0);
}