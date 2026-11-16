// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Quads, uneven fractional levels. Levels computed from gl_PrimitiveID (0 for the one patch drawn), not known at pipeline creation.
layout(vertices = 4) out;
layout(location = 0) in vec2 v_pos[];
layout(location = 0) out vec2 c_pos[];
void main()
{
   c_pos[gl_InvocationID] = v_pos[gl_InvocationID];
   if (gl_InvocationID == 0) {
      gl_TessLevelOuter[0] = 3.3 + float(gl_PrimitiveID);
      gl_TessLevelOuter[1] = 9.7 + float(gl_PrimitiveID);
      gl_TessLevelOuter[2] = 5.1 + float(gl_PrimitiveID);
      gl_TessLevelOuter[3] = 13.4 + float(gl_PrimitiveID);
      gl_TessLevelInner[0] = 7.6 + float(gl_PrimitiveID);
      gl_TessLevelInner[1] = 4.2 + float(gl_PrimitiveID);
   }
}
