// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// The interpolated colour times a uniform tint.
layout(location = 0) in vec4 v_col;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform U { vec4 tint; };
void main() { o = v_col * tint; }
