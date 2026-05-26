#include "image_transforms.h"
#include "error_handlers.h"

#include <algorithm>
#include <cmath>
#include <vector>

void negative(UncompressedImage& img) {
    for (auto& row : img.image_data)
        for (auto& pixel : row) {
            pixel.r = static_cast<uint8_t>(255 - pixel.r);
            pixel.g = static_cast<uint8_t>(255 - pixel.g);
            pixel.b = static_cast<uint8_t>(255 - pixel.b);
        }
}

void negative(CompressedImage& img) {
    for (auto& [id, color] : img.id_to_color) {
        color.r = static_cast<uint8_t>(255 - color.r);
        color.g = static_cast<uint8_t>(255 - color.g);
        color.b = static_cast<uint8_t>(255 - color.b);
    }
    img.color_to_id.clear();
    for (auto& [id, color] : img.id_to_color)
        img.color_to_id[color] = id;
}

void toGrayscale(UncompressedImage& img) {
    if (img.is_grayscale) return;
    for (auto& row : img.image_data)
        for (auto& pixel : row) {
            uint8_t v = colorToGrayscale(pixel);
            pixel.r = v; pixel.g = v; pixel.b = v;
        }
    img.is_grayscale = true;
}

void toGrayscale(CompressedImage& img) {
    img.color_to_id.clear();
    for (auto& [id, color] : img.id_to_color) {
        uint8_t v = colorToGrayscale(color);
        color.r = v; color.g = v; color.b = v;
        img.color_to_id[color] = id;
    }
}

void fillGapPixels(UncompressedImage&, std::vector<std::vector<bool>>&) {}

void rotate(UncompressedImage&, int, ColorRGB, bool) {}

void applyKernel(UncompressedImage& img, const std::vector<std::vector<int>>& kernel, int divisor) {
    int kh = (int)kernel.size(), kw = (int)kernel[0].size();
    int hkh = kh / 2, hkw = kw / 2;
    std::vector<std::vector<ColorRGB>> output(img.height, std::vector<ColorRGB>(img.width));
    for (size_t y = 0; y < img.height; ++y) {
        for (size_t x = 0; x < img.width; ++x) {
            int64_t sr = 0, sg = 0, sb = 0;
            for (int ky = 0; ky < kh; ++ky)
                for (int kx = 0; kx < kw; ++kx) {
                    int sy = std::clamp((int)y + ky - hkh, 0, (int)img.height - 1);
                    int sx = std::clamp((int)x + kx - hkw, 0, (int)img.width  - 1);
                    sr += kernel[ky][kx] * img.image_data[sy][sx].r;
                    sg += kernel[ky][kx] * img.image_data[sy][sx].g;
                    sb += kernel[ky][kx] * img.image_data[sy][sx].b;
                }
            auto cb = [](int64_t v) -> uint8_t {
                return static_cast<uint8_t>(std::clamp(v, int64_t(0), int64_t(255)));
            };
            output[y][x] = {cb(sr / divisor), cb(sg / divisor), cb(sb / divisor)};
        }
    }
    img.image_data = std::move(output);
}

void sharpen(UncompressedImage& img) {
    applyKernel(img, {{ 0,-1, 0},{-1, 5,-1},{ 0,-1, 0}}, 1);
}
void gaussianBlurApprox(UncompressedImage& img, bool hard_blur) {
    if (hard_blur) applyKernel(img, {{1,2,1},{2,4,2},{1,2,1}}, 16);
    else           applyKernel(img, {{1,1,1},{1,1,1},{1,1,1}}, 9);
}
void edgeDetect(UncompressedImage& img) {
    applyKernel(img, {{-1,-1,-1},{-1,8,-1},{-1,-1,-1}}, 1);
}