// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Quads: the corners' bilinear mix, coloured with the patch's colour.
layout(quads, equal_spacing, ccw) in;
layout(location = 0) in vec2 c_pos[];
layout(location = 1) patch in vec4 p_col;
layout(location = 0) out vec4 g_col;
void main()
{
   vec2 a = mix(c_pos[0], c_pos[1], gl_TessCoord.x);
   vec2 b = mix(c_pos[3], c_pos[2], gl_TessCoord.x);
   gl_Position = vec4(mix(a, b, gl_TessCoord.y), 0.0, 1.0);
   g_col = p_col;
}
