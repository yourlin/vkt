// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
layout(location = 0) flat in uvec4 v_ids;
layout(location = 0) out uvec4 o;
void main() { o = v_ids; }
