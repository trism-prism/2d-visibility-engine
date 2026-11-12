#pragma once
// 32-bit draw indices: a stress scene (thousands of walls, shadow quads, gradient rings) puts well
// over 65535 vertices into one ImDrawList. The OpenGL3 backend handles either index size.
#define ImDrawIdx unsigned int
