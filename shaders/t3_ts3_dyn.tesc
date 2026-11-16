// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Triangles, level 3. Levels computed from gl_PrimitiveID (0 for the one patch drawn), not known at pipeline creation.
layout(vertices = 3) out;
layout(location = 0) in vec2 v_pos[];
layout(location = 0) out vec2 c_pos[];
void main()
{
   c_pos[gl_InvocationID] = v_pos[gl_InvocationID];
   if (gl_InvocationID == 0) {
      gl_TessLevelOuter[0] = 3.0 + float(gl_PrimitiveID);
      gl_TessLevelOuter[1] = 3.0 + float(gl_PrimitiveID);
      gl_TessLevelOuter[2] = 3.0 + float(gl_PrimitiveID);
      gl_TessLevelInner[0] = 3.0 + float(gl_PrimitiveID);
   }
}
