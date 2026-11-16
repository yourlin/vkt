// SPDX-FileCopyrightText: 2026 yourlin
// SPDX-License-Identifier: MIT-0
#version 450
// Three triangles side by side, columns 0..2 of the target:
//   0: cull distance -1 at every vertex (culled);
//   1: cull distance -1 at its apex only (kept whole);
//   2: clip distance -1 at its apex (the apex half clipped).
out gl_PerVertex {
   vec4 gl_Position;
   float gl_ClipDistance[1];
   float gl_CullDistance[1];
};
void main()
{
   int tri = gl_VertexIndex / 3, v = gl_VertexIndex % 3;
   const vec2 c[3] = vec2[3](vec2(0.1, 0.1), vec2(0.9, 0.1), vec2(0.5, 0.9));
   vec2 p = vec2((float(tri) + c[v].x) / 3.0, c[v].y);
   gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
   gl_CullDistance[0] = tri == 0 ? -1.0 : (tri == 1 && v == 2) ? -1.0 : 1.0;
   gl_ClipDistance[0] = (tri == 2 && v == 2) ? -1.0 : 1.0;
}
