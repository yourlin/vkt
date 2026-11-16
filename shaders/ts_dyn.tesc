// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Levels computed by the shader (from gl_PrimitiveID), not known at
// pipeline creation.
layout(vertices = 4) out;
layout(location = 0) in vec2 v_pos[];
layout(location = 0) out vec2 c_pos[];
void main()
{
   c_pos[gl_InvocationID] = v_pos[gl_InvocationID];
   if (gl_InvocationID == 0) {
      float l = 4.0 + float(gl_PrimitiveID);
      gl_TessLevelOuter[0] = l;
      gl_TessLevelOuter[1] = l;
      gl_TessLevelOuter[2] = l;
      gl_TessLevelOuter[3] = l;
      gl_TessLevelInner[0] = l;
      gl_TessLevelInner[1] = l;
   }
}
