// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
#extension GL_ARB_shader_viewport_layer_array : require
// One triangle covering the target, sent to viewport gl_InstanceIndex.
void main()
{
   vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
   gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
   gl_ViewportIndex = gl_InstanceIndex;
}
