#version 450

layout (location = 0) in VertexInput {
  vec4 positionWS_RootHeight;//rgbpositionWS  w:rootHeight
  vec4 normalWS_Height;
  vec4 groundNormalWS;
} vertexInput;

layout(location = 0) out vec4 outFragColor;

void main()
{ 
     float selfshadow = clamp(pow((-vertexInput.positionWS_RootHeight.y - vertexInput.positionWS_RootHeight.w)/vertexInput.normalWS_Height.w, 1.5), 0, 1);
	outFragColor = vec4(vec3(0.327,0.5,0.26)*selfshadow,1);
}