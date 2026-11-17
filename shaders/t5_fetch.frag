// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Programmable blending through the colour attachment read as an input
// attachment in the same subpass: red goes up by 4/255 per fragment, so
// every read must see the previous fragment's write.
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput dst;
layout(location = 0) out vec4 o;
void main()
{
   vec4 d = subpassLoad(dst);
   o = vec4(d.r + 4.0 / 255.0, d.g, d.b, 1.0);
}
