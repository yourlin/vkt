// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// The triangle and a copy shifted right by 1.
layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;
layout(location = 0) in vec4 v_col[];
layout(location = 0) out vec4 g_col;
void main()
{
   for (int k = 0; k < 2; k++) {
      for (int i = 0; i < 3; i++) {
         gl_Position = gl_in[i].gl_Position + vec4(float(k), 0.0, 0.0, 0.0);
         g_col = v_col[i];
         EmitVertex();
      }
      EndPrimitive();
   }
}
