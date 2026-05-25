#include "compressor_funcs.h"
#include "error_handlers.h"
#include "libbmp.h"
#include <fstream>
#include <cstring>
#include <climits>
#include <cmath>

void saveAsBMP(const UncompressedImage& img, const std::string& filename) {
    BMP bmp(static_cast<int>(img.width), static_cast<int>(img.height));
    for (size_t y = 0; y < img.height; ++y)
        for (size_t x = 0; x < img.width; ++x) {
            const ColorRGB& c = img.image_data[y][x];
            bmp.set_pixel(static_cast<int>(x), static_cast<int>(y), c.r, c.g, c.b);
        }
    bmp.write(filename.c_str());
}

UncompressedImage loadFromBMP(const std::string& filename) {
    BMP bmp(filename.c_str());
    UncompressedImage img;
    img.width        = static_cast<size_t>(bmp.get_width());
    img.height       = static_cast<size_t>(bmp.get_height());
    img.is_grayscale = false;
    img.image_data.resize(img.height, std::vector<ColorRGB>(img.width));
    for (size_t y = 0; y < img.height; ++y)
        for (size_t x = 0; x < img.width; ++x) {
            uint8_t r, g, b;
            bmp.get_pixel(static_cast<int>(x), static_cast<int>(y), r, g, b);
            img.image_data[y][x] = {r, g, b};
        }
    return img;
}

static const char     RAW_SIG[8]    = {'R','A','W','I','M','A','G','E'};
static const char     RAW_END[9]    = {'R','A','W','I','M','G','E','N','D'};
static const uint8_t  RAW_VER[3]    = {1, 0, 0};

UncompressedImage readUncompressedFile(const std::string& filename) {
    std::fstream f(filename, std::ios::in | std::ios::binary);
    if (!f.is_open()) handleLogMessage("Cannot open: " + filename, Severity::CRITICAL, 1);

    char sig[8]; f.read(sig, 8);
    if (std::memcmp(sig, RAW_SIG, 8) != 0)
        handleLogMessage("Bad RAWIMAGE sig", Severity::CRITICAL, 1);

    uint8_t ver[3]; f.read(reinterpret_cast<char*>(ver), 3);
    uint32_t w, h;
    f.read(reinterpret_cast<char*>(&w), 4);
    f.read(reinterpret_cast<char*>(&h), 4);
    uint8_t gs; f.read(reinterpret_cast<char*>(&gs), 1);

    UncompressedImage img;
    img.width = w; img.height = h; img.is_grayscale = (gs == 1);
    img.image_data.resize(h, std::vector<ColorRGB>(w));

    if (img.is_grayscale) {
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x) {
                uint8_t v; f.read(reinterpret_cast<char*>(&v), 1);
                img.image_data[y][x] = {v, v, v};
            }
    } else {
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x)
                img.image_data[y][x] = readFromFileStream(f);
    }

    char end[9]; f.read(end, 9);
    if (std::memcmp(end, RAW_END, 9) != 0)
        handleLogMessage("Bad RAWIMAGE end sig", Severity::CRITICAL, 1);
    return img;
}

void writeUncompressedFile(const std::string& filename, const UncompressedImage& image) {
    std::fstream f(filename, std::ios::out | std::ios::binary);
    if (!f.is_open()) handleLogMessage("Cannot open for write: " + filename, Severity::CRITICAL, 1);

    f.write(RAW_SIG, 8);
    f.write(reinterpret_cast<const char*>(RAW_VER), 3);
    uint32_t w = image.width, h = image.height;
    f.write(reinterpret_cast<const char*>(&w), 4);
    f.write(reinterpret_cast<const char*>(&h), 4);
    uint8_t gs = image.is_grayscale ? 1 : 0;
    f.write(reinterpret_cast<const char*>(&gs), 1);

    if (image.is_grayscale) {
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x) {
                uint8_t v = image.image_data[y][x].r;
                f.write(reinterpret_cast<const char*>(&v), 1);
            }
    } else {
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x) {
                const ColorRGB& c = image.image_data[y][x];
                f.write(reinterpret_cast<const char*>(&c.r), 1);
                f.write(reinterpret_cast<const char*>(&c.g), 1);
                f.write(reinterpret_cast<const char*>(&c.b), 1);
            }
    }
    f.write(RAW_END, 9);
}


uint8_t findClosestColorId(const ColorRGB& color, const std::map<uint8_t, ColorRGB>& colorTable) {
    uint8_t best_id  = colorTable.begin()->first;
    int64_t best_dist = INT64_MAX;
    for (const auto& [id, c] : colorTable) {
        int64_t d = colorDistanceSq(color, c);
        if (d < best_dist) { best_dist = d; best_id = id; }
    }
    return best_id;
}

CompressedImage toCompressed(
    const UncompressedImage& img,
    const std::map<uint8_t, ColorRGB>& color_table,
    bool approximate,
    bool allow_color_add)
{
    CompressedImage cimg;
    cimg.width  = img.width;
    cimg.height = img.height;

    for (const auto& [id, c] : color_table) {
        cimg.id_to_color[id] = c;
        cimg.color_to_id[c]  = id;
    }

    uint8_t next_id = 0;
    while (cimg.id_to_color.count(next_id)) ++next_id;

    cimg.image_data.resize(img.height, std::vector<uint8_t>(img.width, 0));

    for (size_t y = 0; y < img.height; ++y) {
        for (size_t x = 0; x < img.width; ++x) {
            const ColorRGB& pixel = img.image_data[y][x];

            auto it = cimg.color_to_id.find(pixel);
            if (it != cimg.color_to_id.end()) {
                cimg.image_data[y][x] = it->second;
                continue;
            }

            if (allow_color_add) {
                cimg.id_to_color[next_id] = pixel;
                cimg.color_to_id[pixel]   = next_id;
                cimg.image_data[y][x]     = next_id;
                ++next_id;
                while (cimg.id_to_color.count(next_id)) ++next_id;
            } else if (approximate && !cimg.id_to_color.empty()) {
                cimg.image_data[y][x] = findClosestColorId(pixel, cimg.id_to_color);
            } else if (!cimg.id_to_color.empty()) {
                cimg.image_data[y][x] = findClosestColorId(pixel, cimg.id_to_color);
            }
        }
    }
    return cimg;
}

UncompressedImage toUncompressed(const CompressedImage& img) {
    UncompressedImage uimg;
    uimg.width        = img.width;
    uimg.height       = img.height;
    uimg.is_grayscale = false;
    uimg.image_data.resize(img.height, std::vector<ColorRGB>(img.width));
    for (size_t y = 0; y < img.height; ++y)
        for (size_t x = 0; x < img.width; ++x)
            uimg.image_data[y][x] = getColor(img, static_cast<int>(x), static_cast<int>(y));
    return uimg;
}

ColorRGB getColor(const CompressedImage& img, int x, int y) {
    uint8_t id = img.image_data[static_cast<size_t>(y)][static_cast<size_t>(x)];
    return img.id_to_color.at(id);
}

static const char    CMPR_SIG[9]    = {'C','M','P','R','I','M','A','G','E'};
static const uint8_t CMPR_NULL_BYTE = 0x00;
static const uint8_t CMPR_VER[3]    = {6, 6, 6};
static const char    CMPR_END[10]   = {'C','M','P','R','I','M','G','E','N','D'};

CompressedImage readCompressedFile(const std::string& filename) {
    std::fstream f(filename, std::ios::in | std::ios::binary);
    if (!f.is_open()) handleLogMessage("Cannot open: " + filename, Severity::CRITICAL, 1);

    char sig[9]; f.read(sig, 9);
    if (std::memcmp(sig, CMPR_SIG, 9) != 0)
        handleLogMessage("Bad CMPRIMAGE sig", Severity::CRITICAL, 1);

    uint8_t nb; f.read(reinterpret_cast<char*>(&nb), 1);
    uint8_t ver[3]; f.read(reinterpret_cast<char*>(ver), 3);

    uint32_t w, h;
    f.read(reinterpret_cast<char*>(&w), 4);
    f.read(reinterpret_cast<char*>(&h), 4);

    uint8_t pow_val; f.read(reinterpret_cast<char*>(&pow_val), 1);
    size_t palette_size = size_t(1) << pow_val;

    CompressedImage cimg;
    cimg.width = w; cimg.height = h;

    for (size_t i = 0; i < palette_size; ++i) {
        ColorRGB c;
        f.read(reinterpret_cast<char*>(&c.r), 1);
        f.read(reinterpret_cast<char*>(&c.g), 1);
        f.read(reinterpret_cast<char*>(&c.b), 1);
        cimg.id_to_color[static_cast<uint8_t>(i)] = c;
        cimg.color_to_id[c] = static_cast<uint8_t>(i);
    }

    cimg.image_data.resize(h, std::vector<uint8_t>(w, 0));
    for (size_t y = 0; y < h; ++y)
        for (size_t x = 0; x < w; ++x) {
            uint8_t id; f.read(reinterpret_cast<char*>(&id), 1);
            cimg.image_data[y][x] = id;
        }

    char end[10]; f.read(end, 10);
    if (std::memcmp(end, CMPR_END, 10) != 0)
        handleLogMessage("Bad CMPRIMAGE end sig", Severity::CRITICAL, 1);

    return cimg;
}

void writeCompressedFile(const std::string& filename, const CompressedImage& image) {
    std::fstream f(filename, std::ios::out | std::ios::binary);
    if (!f.is_open()) handleLogMessage("Cannot open for write: " + filename, Severity::CRITICAL, 1);

    f.write(CMPR_SIG, 9);
    f.write(reinterpret_cast<const char*>(&CMPR_NULL_BYTE), 1);
    f.write(reinterpret_cast<const char*>(CMPR_VER), 3);

    uint32_t w = image.width, h = image.height;
    f.write(reinterpret_cast<const char*>(&w), 4);
    f.write(reinterpret_cast<const char*>(&h), 4);
    size_t pal_size = image.id_to_color.size();
    uint8_t pow_val = 0;
    while ((size_t(1) << pow_val) < pal_size) ++pow_val;
    f.write(reinterpret_cast<const char*>(&pow_val), 1);

    size_t total = size_t(1) << pow_val;
    for (size_t i = 0; i < total; ++i) {
        ColorRGB c{0, 0, 0};
        auto it = image.id_to_color.find(static_cast<uint8_t>(i));
        if (it != image.id_to_color.end()) c = it->second;
        f.write(reinterpret_cast<const char*>(&c.r), 1);
        f.write(reinterpret_cast<const char*>(&c.g), 1);
        f.write(reinterpret_cast<const char*>(&c.b), 1);
    }

    for (size_t y = 0; y < h; ++y)
        for (size_t x = 0; x < w; ++x) {
            uint8_t id = image.image_data[y][x];
            f.write(reinterpret_cast<const char*>(&id), 1);
        }

    f.write(CMPR_END, 10);
}