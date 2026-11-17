// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Green on front faces, red on back faces.
layout(location = 0) out vec4 o;
void main() { o = gl_FrontFacing ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1); }
