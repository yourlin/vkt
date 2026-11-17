// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Passes the triangle and its mediump value through.
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in mediump vec4 v_val[];
layout(location = 0) out vec4 g_val;
void main()
{
   for (int i = 0; i < 3; i++) {
      gl_Position = gl_in[i].gl_Position;
      g_val = v_val[i];
      EmitVertex();
   }
}
