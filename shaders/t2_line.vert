// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// A horizontal line through the middle, from edge to edge.
void main()
{
   gl_Position = vec4(gl_VertexIndex == 0 ? -1.0 : 1.0, 0.0, 0.0, 1.0);
}
