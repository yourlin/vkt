// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// One pixel-sized quad per instance, tiling a 64 x 64 target; the loop
// keeps the vertex/tiler stream busy and changes nothing.
// The spin count after the fragment shader's 16-byte colour.
layout(push_constant) uniform P { layout(offset = 16) int spin; };
void main()
{
   const vec2 corner[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1), vec2(0, 1));
   vec2 cell = vec2(float(gl_InstanceIndex % 64), float(gl_InstanceIndex / 64));
   float k = 0.0;
   for (int i = 0; i < spin; i++)
      k = fract(sin(k + float(i)) * 43758.5453);
   vec2 p = (cell + corner[gl_VertexIndex]) / 32.0 - 1.0;
   /* k is in [0, 1): never moves the quad, but keeps the loop live. */
   gl_Position = vec4(p + vec2(k > 2.0 ? 1.0 : 0.0), 0.0, 1.0);
}
