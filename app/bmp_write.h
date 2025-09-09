#ifndef BMP_WRITE_H
#define BMP_WRITE_H

#include <cstdint>
#include <string>
#include <fstream>

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

    int writeBMP(const uint8_t *data, int width, int height, int n_chan) {
        FILE *outfile;
        int extrabytes;
        int paddedsize;
        int x;
        int y;
        int n;
        uint8_t red, green, blue;
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
        // BMP image format is written from bottom to top...
        for (y = height - 1; y >= 0; y--) {
            for (x = 0; x <= width - 1; x++) {
                if (format_ == "y") {
                    red = data[y * width + x];
                    file_.write(reinterpret_cast<const char*>(&red), 1);
                } else {
                    if (format_ == "rgb") {
                        red = data[y * width + x];
                        green = data[y * width + x + width * height];
                        blue = data[y * width + x + width * height * 2];
                    } else if (format_ == "bgr") {
                        red = data[y * width + x + width * height * 2];
                        green = data[y * width + x + width * height];
                        blue = data[y * width + x];
                    } else if (format_ == "rgbi") {
                        red = data[(y * width + x) * 3];
                        green = data[(y * width + x) * 3 + 1];
                        blue = data[(y * width + x) * 3 + 2];
                    } else if (format_ == "bgri") {
                        blue = data[(y * width + x) * 3];
                        green = data[(y * width + x) * 3 + 1];
                        red = data[(y * width + x) * 3 + 2];
                    }
                    // Also, it's written in (b,g,r) format...
                    file_.write(reinterpret_cast<const char*>(&blue), 1);
                    file_.write(reinterpret_cast<const char*>(&green), 1);
                    file_.write(reinterpret_cast<const char*>(&red), 1);
                }
            }
            if (extrabytes)  // See above - BMP lines must be of lengths divisible by 4.
            {
                for (n = 1; n <= extrabytes; n++) {
                    const char *pad = "\0";
                    file_.write(pad, 1);
                }
            }
        }

        return 0;
    }


private:
    std::ofstream file_;
    std::string format_;
};

#endif // BMP_WRITE_H
