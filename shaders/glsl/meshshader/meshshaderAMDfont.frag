/* Copyright (c) 2021, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

#version 450
#extension GL_NV_fragment_shader_barycentric : require
 
const uint SOLID = 0;
const uint CONVEX = 1;
const uint CONCAVE = 2;

uint primAttr[] =
{
    CONVEX,  //0 
    CONVEX,  // 1
    CONCAVE, // 2
    CONCAVE, // 3
    SOLID,   // 4    
    SOLID,   // 5    
    SOLID,   // 6    
    SOLID,   // 7
    SOLID,   // 8
    SOLID,   // 9
};

vec2 computeUV(const vec3 bary)
{
    const float u = bary.x * 0 + bary.y * 0.5f + bary.z * 1;
    const float v = bary.x * 0 + bary.y * 0.0f + bary.z * 1;
    return vec2(u, v);
}

float computeQuadraticBezierFunction(const vec2 uv)
{
    return uv.x * uv.x - uv.y;
}

//layout (location = 0) in VertexInput {
//  uint attriFlag;
//} vertexInput;

layout (location = 0) in VertexInput {
  float color;
} vertexInput;

layout(location = 0) out vec4 outFragColor;

void main()
{
    float flag  = vertexInput.color;
    uint t = SOLID;
    if(flag==0.0)
    {
     t=CONVEX;
    }
    if(flag > 0.5)
    {
    t=CONCAVE;
    }
    const vec2 uv = computeUV(gl_BaryCoordNV);
    const float y  = computeQuadraticBezierFunction(uv);
    if (((t == CONVEX) && (y < 0.0f)) || ((t == CONCAVE) && (y > 0.0f)))
    {
        discard;                        
    }
	outFragColor = vec4(1,0,0,1);
    //outFragColor = vertexInput.color;
    //if(flag==0.0)
    //{
    // outFragColor = vec4(1,0,0,1);
    //}
    //else if(flag > 0.5)
    //{
    //outFragColor = vec4(0,1,0,1);
    //}
    //else
    //{
    //outFragColor = vec4(0,0,1,1);
    //}
}