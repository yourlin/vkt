// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// A fixed output: the triangle's first two vertices as they are, the third
// moved right by 0.25 with its colour's red and blue swapped.
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec4 v_col[];
layout(location = 0) out vec4 g_col;
void main()
{
   gl_Position = gl_in[0].gl_Position;
   g_col = v_col[0];
   EmitVertex();
   gl_Position = gl_in[1].gl_Position;
   g_col = v_col[1];
   EmitVertex();
   gl_Position = gl_in[2].gl_Position + vec4(0.25, 0.0, 0.0, 0.0);
   g_col = v_col[2].bgra;
   EmitVertex();
}
