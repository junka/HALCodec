
#include "nvjpegencoder.h"

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <fstream>
#include <nvjpeg.h>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>
#include <regex>

namespace halcodec {
namespace nvjpeg {

NVJPEGEncoder::NVJPEGEncoder() {
    int ret = cuInit(0);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuInit error" << std::endl;
    }
}


void NVJPEGEncoder::Initialize(std::string input, std::string format) {
    format_ = format;
    CUdevice cuDevice_ = 0;
    int idx = 0;
    int ret = cuDeviceGet(&cuDevice_, idx);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGet error" << std::endl;
        return;
    }
    char szDeviceName[80];
    ret = cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGetName error" << std::endl;
        return;
    }
    ret = cuCtxCreate(&cuContext_, 0, cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuCtxCreate error" << std::endl;
        return;
    }
    cudaDeviceProp props;
    ret = cudaGetDeviceProperties(&props, 0);
    if (ret != CUDA_SUCCESS) {
        std::cout << "get device properties error" << std::endl;
        return;
    }
    printf("Using GPU %d (%s, %d SMs, %d th/SM max, CC %d.%d, ECC %s)\n",
         0, props.name, props.multiProcessorCount,
         props.maxThreadsPerMultiProcessor, props.major, props.minor,
         props.ECCEnabled ? "on" : "off");
    nvjpegDevAllocator_t dev_allocator = {&dev_malloc, &dev_free};
    nvjpegPinnedAllocator_t pinned_allocator ={&host_malloc, &host_free};
    ret = nvjpegCreateEx(NVJPEG_BACKEND_GPU_HYBRID, &dev_allocator,
            &pinned_allocator, 0,  &nvjpegHandle_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegCreateEx %d\n", ret);
    }

    ret = nvjpegEncoderStateCreate(nvjpegHandle_, &encoderState_, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderStateCreate %d\n", ret);
        return;
    }
    ret = nvjpegEncoderParamsCreate(nvjpegHandle_, &encode_params_, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsCreate %d\n", ret);
        return;
    }
    ret = nvjpegEncoderParamsSetQuality(encode_params_, 75, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetQuality %d\n", ret);
        return;
    }
    ret = nvjpegEncoderParamsSetEncoding(encode_params_, NVJPEG_ENCODING_BASELINE_DCT, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetEncoding %d\n", ret);
        return;
    }
    ret = nvjpegEncoderParamsSetOptimizedHuffman(encode_params_, 1, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetOptimizedHuffman %d\n", ret);
        return;
    }
    ret = nvjpegEncoderParamsSetSamplingFactors(encode_params_, NVJPEG_CSS_420, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        printf("nvjpegEncoderParamsSetSamplingFactors %d\n", ret);
        return;
    }
    struct stat info;
    if (stat(input.c_str(), &info) != 0) {
        std::cout << "Cannot access " << input << std::endl;
        return;
    }

    if (info.st_mode & S_IFDIR) { // Check if input is a directory
        DIR *dir;
        struct dirent *ent;
        while (!input.empty() && input.back() == '/') {
            input.pop_back();
        }
        if ((dir = opendir(input.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    file_names_.emplace_back(input + '/' + ent->d_name);
                }
            }
            closedir(dir);
        } else {
            std::cout << "Could not open directory " << input << std::endl;
            return;
        }
        std::cout << "Number of files in directory: " << file_names_.size() << std::endl;
    } else if (info.st_mode & S_IFREG) { // Check if input is a regular file
        std::cout << "Input is a file: " << input << std::endl;
        file_names_.emplace_back(input.c_str());
    } else {
        std::cout << "Input is neither a file nor a directory" << std::endl;
    }
    if (file_names_.size() > 16) {
        batch_size_ = 16;
    }

    ret = cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking);
    if (ret != CUDA_SUCCESS) {
        std::cerr << "Fail to create cuda stream" << std::endl;
    }

    file_iter_ = file_names_.begin();
    dev_data_.resize(batch_size_);
    file_len_.resize(batch_size_);
    out_.resize(batch_size_);

    img_widths_.resize(batch_size_);
    img_heights_.resize(batch_size_);
    subsamplings_.resize(batch_size_);

    for (int i = 0; i < out_.size(); i++) {
        for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++) {
            out_[i].channel[c] = NULL;
            out_[i].pitch[c] = 0;
        }
    }
}

void NVJPEGEncoder::Finalize() {
    cudaStreamDestroy(stream_);
    nvjpegEncoderStateDestroy(encoderState_);
    nvjpegDestroy(nvjpegHandle_);
    cuCtxDestroy(cuContext_);
}

int NVJPEGEncoder::FillData() {
    float loopTime = 0;
    double time;
    cudaEvent_t startEvent = NULL;
    cudaEvent_t stopEvent = NULL;

    int ret = cudaStreamSynchronize(stream_);
    if (ret != CUDA_SUCCESS) {
        std::cerr << "Fail to sync cuda stream" << std::endl;
        return -1;
    }
    if (file_iter_ == file_names_.end()) {
        return 0;
    }

    for (int i = 0; i < batch_size_ && file_iter_ != file_names_.end(); i++, file_iter_++) {
        std::string ending = ".bmp";
        int n_chan;
        std::ifstream input_file(*file_iter_, std::ios::in | std::ios::binary | std::ios::ate);
        if (!input_file) {
            std::cerr << "Cannot open file: " << *file_iter_ << std::endl;
            return -1;
        }

        if (std::equal(ending.rbegin(), ending.rend(), (*file_iter_).rbegin())) {
            input_file.seekg(18, std::ios::beg);
            input_file.read(reinterpret_cast<char *>(&img_widths_[i]), 4);
            input_file.read(reinterpret_cast<char *>(&img_heights_[i]), 4);
            input_file.seekg(2, std::ios::cur);
            input_file.read(reinterpret_cast<char *>(&n_chan), 2);
            n_chan /= 8;
            input_file.seekg(4, std::ios::cur);
            uint32_t size;
            input_file.read(reinterpret_cast<char *>(&size), 4);
            input_file.seekg(16, std::ios::cur);

            cudaError_t err = cudaMalloc((void**)(&dev_data_[i]), size);
            if(cudaSuccess != err) {
                std::cout << "error for cuda malloc" << std::endl;
                return -1;
            }
            std::vector<uint8_t> data(size);
            if (!input_file.read(reinterpret_cast<char *>(data.data()), size)) {
                std::cerr << "Failed to read file: " << *file_iter_ << std::endl;
                return -1;
            }
            err = cudaMemcpy(dev_data_[i], data.data(), size, cudaMemcpyHostToDevice);
            if(cudaSuccess != err) {
                cudaFree(dev_data_[i]);
                std::cout << "error for cuda copy" << std::endl;
                return -1;
            }
            file_len_[i] = size;
            subsamplings_[i] =  NVJPEG_CSS_420;
        } else {
            // assume it is yuv, try get width and height from filename
            std::regex pattern(R"((\d+)[xX](\d+))");
            std::smatch match;
            if (std::regex_search(*file_iter_, match, pattern)) {
                img_widths_[i] = std::stoi(match[1].str());
                img_heights_[i] = std::stoi(match[2].str());
            } else {
                std::cerr << "Failed to match resolution in: " << *file_iter_ << std::endl;
            }
            std::streamsize size = input_file.tellg();
            input_file.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(size);
            if (!input_file.read(reinterpret_cast<char *>(data.data()), size)) {
                std::cerr << "Failed to read file: " << *file_iter_ << std::endl;
                return -1;
            }
            cudaMemcpy(dev_data_[i], data.data(), size, cudaMemcpyHostToDevice);   
            file_len_[i] = size;
            if (size == img_heights_[i] * img_widths_[i] * 3) {
                subsamplings_[i] =  NVJPEG_CSS_444;
            } else if (size == img_heights_[i] * img_widths_[i] * 2) {
                subsamplings_[i] =  NVJPEG_CSS_440;
            } else if (size == img_heights_[i] * img_widths_[i] * 3/2) {
                subsamplings_[i] =  NVJPEG_CSS_420;
            } else if (size == img_heights_[i] * img_widths_[i] * 5/4) {
                subsamplings_[i] =  NVJPEG_CSS_411;
            } else if (size == img_heights_[i] * img_widths_[i] * 9/8) {
                subsamplings_[i] =  NVJPEG_CSS_410;
            }
        }
        std::cout << "Processing: " << *file_iter_ << std::endl;
    }

    int channels;
    //defaul bmp file should be BGR and 444
    if (format_ == "bmp") {
        inputfmt_ = NVJPEG_INPUT_BGRI;
    } if (format_ == "bgri") {
        inputfmt_ = NVJPEG_INPUT_BGRI;
    } else if (format_ == "rgbi") {
        inputfmt_ = NVJPEG_INPUT_RGBI;
    } else if (format_ == "rgb") {
        inputfmt_ = NVJPEG_INPUT_RGB;
    } else if (format_ == "bgr") {
        inputfmt_ = NVJPEG_INPUT_BGR;
    }
    for (int i = 0; i < dev_data_.size(); i++) {

        switch (subsamplings_[i]) {
        case NVJPEG_CSS_444:
            std::cout << "YUV 4:4:4 chroma subsampling" << std::endl;
            break;
        case NVJPEG_CSS_440:
            std::cout << "YUV 4:4:0 chroma subsampling" << std::endl;
            break;
        case NVJPEG_CSS_422:
            std::cout << "YUV 4:2:2 chroma subsampling" << std::endl;
            break;
        case NVJPEG_CSS_420:
            std::cout << "YUV 4:2:0 chroma subsampling" << std::endl;
            break;
        case NVJPEG_CSS_411:
            std::cout << "YUV 4:1:1 chroma subsampling" << std::endl;
            break;
        case NVJPEG_CSS_410:
            std::cout << "YUV 4:1:0 chroma subsampling" << std::endl;
            break;
        case NVJPEG_CSS_GRAY:
            std::cout << "Grayscale JPEG " << std::endl;
            break;
        case NVJPEG_CSS_UNKNOWN:
            std::cout << "Unknown chroma subsampling" << std::endl;
            return EXIT_FAILURE;
        }
    }

    cudaDeviceSynchronize();
    for (int i = 0; i < batch_size_; i++) {
        
        ret = nvjpegEncoderParamsSetSamplingFactors(encode_params_, subsamplings_[i], stream_);
        if (ret != NVJPEG_STATUS_SUCCESS) {
            std::cout << "fail to nvjpegEncoderParamsSetSamplingFactors" << ret << std::endl;
        }
        ret = cudaEventCreate(&startEvent);
        if (ret != NVJPEG_STATUS_SUCCESS) {
            std::cout << "fail to cudaEventCreate start event" << ret << std::endl;
        }
        ret = cudaEventCreate(&stopEvent);
        if (ret != NVJPEG_STATUS_SUCCESS) {
            std::cout << "fail to cudaEventCreate stop event" << ret << std::endl;
        }
        ret = cudaEventRecord(startEvent, stream_);
        if (ret != NVJPEG_STATUS_SUCCESS) {
            std::cout << "fail to cudaEventRecord start event" << ret << std::endl;
        }
        nvjpegImage_t imgdesc = {
            {
                dev_data_[i],
                dev_data_[i] + img_widths_[i] * img_heights_[i],
                dev_data_[i] + img_widths_[i] * img_heights_[i] * 3/2,
                dev_data_[i] + img_widths_[i] * img_heights_[i] * 3
            },
            {
                (unsigned int)((inputfmt_ == NVJPEG_INPUT_RGBI || inputfmt_ == NVJPEG_INPUT_BGRI) ? img_widths_[i] * 3 : img_widths_[i]),
                (unsigned int)img_widths_[i],
                (unsigned int)img_widths_[i],
                (unsigned int)img_widths_[i]
            }
        };
        printf("encode height %d, width %d\n", img_heights_[i], img_widths_[i]);
        if (format_ == "yuv") {
            // For YUV output, use nvjpegEncodeYUV
            ret = nvjpegEncodeYUV(nvjpegHandle_, encoderState_, encode_params_, &imgdesc, subsamplings_[i], img_widths_[i], img_heights_[i], stream_);
            if (ret != NVJPEG_STATUS_SUCCESS) {
                std::cout << "fail to nvjpegEncodeYUV " << ret << std::endl;
                return 0;
            }
        } else {
            // For other formats, use nvjpegEncodeImage
            ret = nvjpegEncodeImage(nvjpegHandle_, encoderState_, encode_params_, &imgdesc, inputfmt_, img_widths_[i], img_heights_[i], stream_);
            if (ret != NVJPEG_STATUS_SUCCESS) {
                std::cout << "fail to nvjpegEncodeImage " << ret << std::endl;
                return 0;
            }
        }
        ret = cudaEventRecord(stopEvent, stream_);
        ret = cudaEventSynchronize(stopEvent);
        ret = cudaEventElapsedTime(&loopTime, startEvent, stopEvent);
        time = static_cast<double>(loopTime);
        std::cout << "Encode time " << time << std::endl;
    }

    num_decoded += batch_size_;
    return num_decoded;
}

uint8_t* NVJPEGEncoder::GetFrame(int *framesize, int *height, int *width, int *n_chan) {
    int idx = batch_size_ - num_decoded;
    int total_size = 0;
    nvjpegStatus_t ret;
    ret = nvjpegEncodeRetrieveBitstream(nvjpegHandle_, encoderState_, nullptr, (size_t*)&total_size, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        std::cout << "fail to retrieve bitstream for probe " << ret << std::endl;
        return nullptr;
    }
    uint8_t* combined_frame = (uint8_t*)malloc(total_size);
    ret = nvjpegEncodeRetrieveBitstream(nvjpegHandle_, encoderState_, combined_frame, (size_t*)&total_size, stream_);
    if (ret != NVJPEG_STATUS_SUCCESS) {
        std::cout << "fail to retrieve bitstream" << std::endl;
        return nullptr;
    }
    *height = img_heights_[idx];
    *width = img_widths_[idx];
    *n_chan = 1;
    cudaFree(dev_data_[idx]);
    dev_data_[idx] = nullptr;
    *framesize = total_size;
    return combined_frame;
}

void NVJPEGEncoder::ReleaseFrame(uint8_t **pFrame) {
    num_decoded --;
    free(*pFrame);
}

static bool registered = []() -> bool {
    NVJPEGEncoder::Register();
    return true;
}();

} // namespace nvjpeg
} // namespace halcodec