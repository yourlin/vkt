// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Triangles, vertex order ccw, level 1: one triangle over the patch.
layout(triangles, equal_spacing, ccw) in;
layout(location = 0) in vec2 c_pos[];
void main()
{
   vec2 p = c_pos[0] * gl_TessCoord.x + c_pos[1] * gl_TessCoord.y + c_pos[2] * gl_TessCoord.z;
   gl_Position = vec4(p, 0.0, 1.0);
}
