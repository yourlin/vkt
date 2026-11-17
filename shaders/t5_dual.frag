// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Dual-source blending: the colour and the second source from push
// constants.
layout(location = 0, index = 0) out vec4 o0;
layout(location = 0, index = 1) out vec4 o1;
layout(push_constant) uniform P { vec4 c0; vec4 c1; };
void main()
{
   o0 = c0;
   o1 = c1;
}
