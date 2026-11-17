// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// The value, to a 32-bit float target.
layout(location = 0) in vec4 g_val;
layout(location = 0) out vec4 o;
void main() { o = g_val; }
