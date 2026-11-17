// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// The triangle four times, the k-th copy to viewport k.
layout(triangles, invocations = 4) in;
layout(triangle_strip, max_vertices = 3) out;
void main()
{
   for (int i = 0; i < 3; i++) {
      gl_Position = gl_in[i].gl_Position;
      gl_ViewportIndex = gl_InvocationID;
      EmitVertex();
   }
}
