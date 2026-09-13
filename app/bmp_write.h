#ifndef BMP_WRITE_H
#define BMP_WRITE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>

#pragma pack(push, 1)

struct BMPFileHeader {
    uint16_t type{0x4D42};          // BM
    uint32_t size{0};
    uint16_t reserved1{0};
    uint16_t reserved2{0};
    uint32_t offset{54};            // 14 + 40
};

struct BMPInfoHeader {
    uint32_t size{40};
    int32_t width{0};
    int32_t height{0};              // 正数：bottom-up
    uint16_t planes{1};
    uint16_t bit_count{24};
    uint32_t compression{0};
    uint32_t image_size{0};
    int32_t x_pixels_per_meter{0};
    int32_t y_pixels_per_meter{0};
    uint32_t colors_used{0};
    uint32_t colors_important{0};
};
struct BMPColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

#pragma pack(pop)

class BMPWriter {
public:
    BMPWriter(const std::string filename, std::string format = "rgb"): format_(format) {
        std::string bmp_filename;
        size_t lastDot = filename.find_last_of('.');
        if (lastDot != std::string::npos) {
            bmp_filename = filename.substr(0, lastDot);
        }
        bmp_filename = bmp_filename + ".bmp";
        file_.open(bmp_filename, std::ios::out|std::ios::binary);
    }
    ~BMPWriter() {
        file_.close();
    }

    int writeBMP(const uint8_t *data, int width, int height, int n_chan) {
        int extrabytes;
        int paddedsize;
        int y;
        int x;
        extrabytes = (4 - ((width * n_chan) % 4)) % 4;  // How many bytes of padding to add to each
        paddedsize = ((width * n_chan) + extrabytes) * height;

        // Headers...
        BMPFileHeader file_header;
        file_header.size = paddedsize + 54;

        BMPInfoHeader info_header;
        info_header.width = width;
        info_header.height = height;
        if (format_ == "y") {
            info_header.bit_count = 8;
            info_header.colors_used = 256;
        }
        info_header.image_size = paddedsize;
        file_.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
        file_.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
        if (format_ == "y") {
            struct BMPColor palette[256];
            for (int i = 0; i < 256; i++) {
                palette[i].r = palette[i].g = palette[i].b = palette[i].a = i;
            }
            file_.write(reinterpret_cast<const char*>(palette), 256 * sizeof(struct BMPColor));
        }
        // BMP image format is written from bottom to top; each row is packed
        // in a buffer and written in a single call instead of byte-by-byte.
        std::vector<uint8_t> row(static_cast<size_t>(width) * n_chan + extrabytes);
        const size_t plane = static_cast<size_t>(width) * height;  // bytes per component plane
        for (y = height - 1; y >= 0; y--) {
            size_t px = 0;
            if (format_ == "y") {
                const uint8_t* src = data + static_cast<size_t>(y) * width;
                memcpy(row.data(), src, width);
            } else {
                for (x = 0; x < width; x++) {
                    uint8_t r, g, b;
                    if (format_ == "rgb") {
                        r = data[static_cast<size_t>(y) * width + x];
                        g = data[static_cast<size_t>(y) * width + x + plane];
                        b = data[static_cast<size_t>(y) * width + x + plane * 2];
                    } else if (format_ == "bgr") {
                        b = data[static_cast<size_t>(y) * width + x];
                        g = data[static_cast<size_t>(y) * width + x + plane];
                        r = data[static_cast<size_t>(y) * width + x + plane * 2];
                    } else if (format_ == "rgbi") {
                        const uint8_t* c = data + (static_cast<size_t>(y) * width + x) * 3;
                        r = c[0];
                        g = c[1];
                        b = c[2];
                    } else {  // bgri
                        const uint8_t* c = data + (static_cast<size_t>(y) * width + x) * 3;
                        b = c[0];
                        g = c[1];
                        r = c[2];
                    }
                    // Also, it's written in (b,g,r) format...
                    row[px++] = b;
                    row[px++] = g;
                    row[px++] = r;
                }
            }
            file_.write(reinterpret_cast<const char*>(row.data()),
                        static_cast<std::streamsize>(row.size()));
        }

        return 0;
    }

private:
    std::ofstream file_;
    std::string format_;
};


class BMPReader {
public:
    BMPReader(const std::string filename) {
        file_.open(filename, std::ios::in|std::ios::binary);
    }
    ~BMPReader() {
        file_.close();
    }

    uint8_t* readBMP(int *width, int *height, int *n_chan) {
        uint8_t *data;
        BMPFileHeader file_header;
        BMPInfoHeader info_header;
        file_.read(reinterpret_cast<char *>(&file_header), sizeof(file_header));
        file_.read(reinterpret_cast<char *>(&info_header), sizeof(info_header));
        *width = info_header.width;
        *height = info_header.height;
        *n_chan = info_header.bit_count/8;
        if (info_header.colors_used) {
            file_.seekg(info_header.colors_used * sizeof(struct BMPColor), std::ios_base::cur);
        }
        data = reinterpret_cast<uint8_t *>(malloc(info_header.image_size));
        file_.read(reinterpret_cast<char *>(data), info_header.image_size);
        return data;
    }

private:
    std::ifstream file_;
};

#endif // BMP_WRITE_H
