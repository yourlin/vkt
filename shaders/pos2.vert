// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
layout(location = 0) in vec2 pos;
void main()
{
   gl_Position = vec4(pos, 0.0, 1.0);
   gl_PointSize = 3.0;
}
