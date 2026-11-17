// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
layout(triangles, equal_spacing, ccw) in;
layout(location = 0) in vec4 c_val[];
layout(location = 1) in vec2 c_pos[];
layout(location = 0) out vec4 g_val;
void main()
{
   vec2 p = c_pos[0] * gl_TessCoord.x + c_pos[1] * gl_TessCoord.y + c_pos[2] * gl_TessCoord.z;
   gl_Position = vec4(p, 0.0, 1.0);
   g_val = c_val[0] * gl_TessCoord.x + c_val[1] * gl_TessCoord.y + c_val[2] * gl_TessCoord.z;
}
