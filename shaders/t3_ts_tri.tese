// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Triangles: the barycentric combination of the corners, coloured with
// the barycentrics (their sum is 1).
layout(triangles, equal_spacing, cw) in;
layout(location = 0) in vec2 c_pos[];
layout(location = 0) out vec4 g_col;
void main()
{
   vec2 p = c_pos[0] * gl_TessCoord.x + c_pos[1] * gl_TessCoord.y + c_pos[2] * gl_TessCoord.z;
   gl_Position = vec4(p, 0.0, 1.0);
   g_col = vec4(gl_TessCoord, 1.0);
}
