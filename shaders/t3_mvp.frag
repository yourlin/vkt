// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// The colour of the viewport: red, green, blue, yellow.
layout(location = 0) out vec4 o;
void main()
{
   const vec4 c[4] = vec4[4](vec4(1, 0, 0, 1), vec4(0, 1, 0, 1), vec4(0, 0, 1, 1), vec4(1, 1, 0, 1));
   o = c[gl_ViewportIndex];
}
