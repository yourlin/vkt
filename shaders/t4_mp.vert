// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// A full-target triangle (or the vertices of a patch) carrying values in
// a relaxed-precision (mediump) output. Each is exact in 16 bits, so a
// 16-bit output is still right; read back as 32-bit it is not.
layout(location = 0) out mediump vec4 v_val;
layout(location = 1) out vec2 v_pos;
void main()
{
   vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
   gl_Position = vec4(p, 0.0, 1.0);
   v_pos = p;
   v_val = vec4(2.0, 0.5, -3.0, 100.0);
}
