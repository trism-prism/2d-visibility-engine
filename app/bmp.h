#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Writes an uncompressed 24-bit BMP. `rgb` is tightly packed RGB, rows bottom-up (the order
// glReadPixels returns). Returns false if the file cannot be written.
bool writeBmp24(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb);
