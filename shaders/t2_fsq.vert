// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// One triangle covering the whole target.
void main()
{
   vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
   gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
