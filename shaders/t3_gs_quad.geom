// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Points to quads 0.5 wide: even primitive ids only, coloured
// (1, id / 4, 0); odd ones emit nothing.
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
layout(location = 0) out vec4 g_col;
void main()
{
   if ((gl_PrimitiveIDIn & 1) != 0)
      return;
   vec4 col = vec4(1.0, float(gl_PrimitiveIDIn) / 4.0, 0.0, 1.0);
   for (int i = 0; i < 4; i++) {
      vec2 o = vec2(float(i & 1), float(i >> 1)) * 0.5 - vec2(0.25);
      gl_Position = gl_in[0].gl_Position + vec4(o, 0.0, 0.0);
      g_col = col;
      EmitVertex();
   }
   EndPrimitive();
}
