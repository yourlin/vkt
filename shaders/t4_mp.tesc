// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Passes the patch's positions and mediump values through, level 1.
layout(vertices = 3) out;
layout(location = 0) in mediump vec4 v_val[];
layout(location = 1) in vec2 v_pos[];
layout(location = 0) out vec4 c_val[];
layout(location = 1) out vec2 c_pos[];
void main()
{
   c_val[gl_InvocationID] = v_val[gl_InvocationID];
   c_pos[gl_InvocationID] = v_pos[gl_InvocationID];
   if (gl_InvocationID == 0) {
      gl_TessLevelOuter[0] = 1.0;
      gl_TessLevelOuter[1] = 1.0;
      gl_TessLevelOuter[2] = 1.0;
      gl_TessLevelInner[0] = 1.0;
   }
}
