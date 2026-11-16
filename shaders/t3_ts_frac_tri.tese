// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Triangles, fractional even spacing, coloured with the barycentrics
// (continuous, so a sample does not land on a colour edge).
layout(triangles, fractional_even_spacing, cw) in;
layout(location = 0) in vec2 c_pos[];
layout(location = 0) out vec4 g_col;
void main()
{
   vec2 p = c_pos[0] * gl_TessCoord.x + c_pos[1] * gl_TessCoord.y + c_pos[2] * gl_TessCoord.z;
   gl_Position = vec4(p, 0.0, 1.0);
   g_col = vec4(gl_TessCoord * 0.5 + 0.5, 1.0);
}
