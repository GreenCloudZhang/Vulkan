/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

 //API CALL Order == buffer with flags VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
layout(push_constant) uniform BufferReferences {
	uint64_t vertices;
	uint64_t indices;
	uint64_t bufferAddress;
} bufferReferences;

layout(buffer_reference, scalar) buffer Vertices {vec4 v[]; };
layout(buffer_reference, scalar) buffer Indices {uint i[]; };
layout(buffer_reference, scalar) buffer Data {vec4 f[]; };