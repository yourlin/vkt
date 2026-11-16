// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
#extension GL_ARB_shader_draw_parameters : require
// Draw i covers column i of 4 (a strip of 4 vertices) and passes on its
// draw parameters.
layout(location = 0) flat out uvec4 v_ids;
void main()
{
   int corner = gl_VertexIndex - gl_BaseVertexARB;
   vec2 uv = vec2(float(corner & 1), float((corner >> 1) & 1));
   vec2 p = vec2((float(gl_DrawIDARB) + uv.x) / 4.0, uv.y);
   gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
   v_ids = uvec4(uint(gl_DrawIDARB), uint(gl_BaseVertexARB), uint(gl_BaseInstanceARB),
                 uint(gl_InstanceIndex));
}
