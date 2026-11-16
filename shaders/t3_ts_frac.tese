// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Quads, fractional odd spacing, coloured with the tessellation
// coordinates (continuous, so a sample does not land on a colour edge).
layout(quads, fractional_odd_spacing, ccw) in;
layout(location = 0) in vec2 c_pos[];
layout(location = 0) out vec4 g_col;
void main()
{
   vec2 a = mix(c_pos[0], c_pos[1], gl_TessCoord.x);
   vec2 b = mix(c_pos[3], c_pos[2], gl_TessCoord.x);
   gl_Position = vec4(mix(a, b, gl_TessCoord.y), 0.0, 1.0);
   g_col = vec4(gl_TessCoord.xy * 0.5 + 0.5, 0.5, 1.0);
}
