// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
layout(location = 0) out vec4 o;
layout(push_constant) uniform P { vec4 col; };
void main() { o = col; }
