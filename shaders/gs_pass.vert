// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
layout(location = 0) in vec2 pos;
layout(location = 0) out vec4 v_col;
void main()
{
   gl_Position = vec4(pos, 0.0, 1.0);
   int c = gl_VertexIndex % 3;
   v_col = vec4(c == 0 ? 1.0 : 0.0, c == 1 ? 1.0 : 0.0, c == 2 ? 1.0 : 0.0, 1.0);
}
