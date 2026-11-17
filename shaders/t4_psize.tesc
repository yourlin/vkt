// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// A small square at the mean of the patch's input vertices, red for
// patches of 3 vertices and green for 4: with the vertex shader fused into
// this shader for one patch size, another size reads the wrong vertices.
layout(vertices = 4) out;
layout(location = 0) in vec2 v_pos[];
layout(location = 0) out vec2 c_pos[];
layout(location = 1) patch out vec4 p_col;
void main()
{
   vec2 m = vec2(0.0);
   for (int i = 0; i < gl_PatchVerticesIn; i++)
      m += v_pos[i];
   m /= float(gl_PatchVerticesIn);
   const vec2 corner[4] = vec2[4](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, 1));
   c_pos[gl_InvocationID] = m + corner[gl_InvocationID] * 0.03;
   if (gl_InvocationID == 0) {
      gl_TessLevelOuter[0] = 1.0;
      gl_TessLevelOuter[1] = 1.0;
      gl_TessLevelOuter[2] = 1.0;
      gl_TessLevelOuter[3] = 1.0;
      gl_TessLevelInner[0] = 1.0;
      gl_TessLevelInner[1] = 1.0;
      p_col = gl_PatchVerticesIn == 3 ? vec4(1, 0, 0, 1) : vec4(0, 1, 0, 1);
   }
}
