// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// The vertex shader's mediump value straight to the fragment shader (the
// case without geometry or tessellation, for comparison).
layout(location = 0) in mediump vec4 v_val;
layout(location = 0) out vec4 o;
void main() { o = v_val; }
