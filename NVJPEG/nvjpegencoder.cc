
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

namespace halcodec {
namespace nvjpeg {

NVJPEGEncoder::NVJPEGEncoder() {
    int ret = cuInit(0);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuInit error" << std::endl;
    }
}


void NVJPEGEncoder::Initialize(std::string input, std::string format) {
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
    nvjpegCreateEx(NVJPEG_BACKEND_HYBRID, &dev_allocator,
            &pinned_allocator, 0,  &nvjpegHandle_);
    nvjpegJpegStateCreate(nvjpegHandle_, &jpegState_);
    nvjpegEncoderStateCreate(nvjpegHandle_, &encoderState_, stream_);
    nvjpegEncoderParamsCreate(nvjpegHandle_, &encode_params_, stream_);
    nvjpegEncoderParamsSetQuality(encode_params_, 75, stream_);
    nvjpegEncoderParamsSetOptimizedHuffman(encode_params_, 1, stream_);

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
    data_.resize(batch_size_);
    file_len_.resize(batch_size_);
    out_.resize(batch_size_);
    // output buffer sizes, for convenience
    isz_.resize(batch_size_);
    img_widths_.resize(batch_size_);
    img_heights_.resize(batch_size_);
    subsamplings_.resize(batch_size_);

    for (int i = 0; i < out_.size(); i++) {
        for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++) {
            out_[i].channel[c] = NULL;
            out_[i].pitch[c] = 0;
            isz_[i].pitch[c] = 0;
        }
    }
}

void NVJPEGEncoder::Finalize() {
    cudaStreamDestroy(stream_);
    nvjpegEncoderStateDestroy(encoderState_);
    nvjpegJpegStateDestroy(jpegState_);
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
        std::ifstream input_file(*file_iter_, std::ios::in | std::ios::binary | std::ios::ate);
        if (!input_file) {
            std::cerr << "Cannot open file: " << *file_iter_ << std::endl;
            return -1;
        }
        std::streamsize size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);

        data_[i].resize(size);
        if (!input_file.read(data_[i].data(), size)) {
            std::cerr << "Failed to read file: " << *file_iter_ << std::endl;
            return -1;
        }
        file_len_[i] = size;
        
        std::cout << "Processing: " << *file_iter_ << std::endl;
    }

    int channels;
    nvjpegInputFormat_t input_format = NVJPEG_INPUT_BGR;
    nvjpegChromaSubsampling_t subsampling = NVJPEG_CSS_420;
    int widths[NVJPEG_MAX_COMPONENT];
    int heights[NVJPEG_MAX_COMPONENT];

    for (int i = 0; i < data_.size(); i++) {
        
        img_widths_[i] = widths[0];
        img_heights_[i] = heights[0];
        subsamplings_[i] = subsampling;

        switch (subsampling) {
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
        int mul = 1;
        // in the case of interleaved RGB output, write only to single channel, but
        // 3 samples at once
         
        for (int c = 0; c < channels; c++) {
            int aw = mul * widths[c];
            int ah = heights[c];
            int sz = aw * ah;
            out_[i].pitch[c] = aw;
            if (sz > isz_[i].pitch[c]) {
                if (out_[i].channel[c]) {
                    cudaFree(out_[i].channel[c]);
                }
                cudaMalloc((void**)&out_[i].channel[c], sz);
                isz_[i].pitch[c] = sz;
            }
        }
    }

    // Image buffers.
    unsigned char * pBuffer = NULL;
    cudaMalloc((void**)&pBuffer, widths[0] * heights[0] * NVJPEG_MAX_COMPONENT);
    cudaEventCreate(&startEvent);
    cudaEventCreate(&stopEvent);

    std::vector<const unsigned char *> raw_inputs;
    for (int i = 0; i < batch_size_; i++) {
        raw_inputs.push_back((const unsigned char *)data_[i].data());
    }
    cudaEventRecord(startEvent, stream_);
    nvjpegImage_t imgdesc = {
        {
            pBuffer,
            pBuffer + widths[0]*heights[0],
            pBuffer + widths[0]*heights[0]*2,
            pBuffer + widths[0]*heights[0]*3
        },
        {
            (unsigned int)((outputfmt_ == NVJPEG_OUTPUT_RGBI || outputfmt_ == NVJPEG_OUTPUT_BGRI) ? widths[0] * 3 : widths[0]),
            (unsigned int)widths[0],
            (unsigned int)widths[0],
            (unsigned int)widths[0]
        }
    };
    if (outputfmt_ == NVJPEG_OUTPUT_YUV) {
        // For YUV output, use nvjpegEncodeYUV
        nvjpegEncodeYUV(nvjpegHandle_, encoderState_, encode_params_, &imgdesc, subsampling, widths[0], heights[0], stream_);
    } else {
        // For other formats, use nvjpegEncodeImage
        nvjpegEncodeImage(nvjpegHandle_, encoderState_, encode_params_, &imgdesc, input_format, widths[0], heights[0], stream_);
    }
    cudaEventRecord(stopEvent, stream_);

    num_decoded += batch_size_;
    cudaEventSynchronize(stopEvent);
    cudaEventElapsedTime(&loopTime, startEvent, stopEvent);
    time = static_cast<double>(loopTime);
    std::cout << "Encode time " << time << std::endl;
    return num_decoded;
}

uint8_t* NVJPEGEncoder::GetFrame(int *framesize, int *height, int *width, int *n_chan) {
    int idx = batch_size_ - num_decoded;
    int total_size = 0;
    nvjpegEncodeRetrieveBitstream(nvjpegHandle_, encoderState_, nullptr, (size_t*)&total_size, stream_);

    uint8_t* combined_frame = (uint8_t*)malloc(total_size);
    nvjpegEncodeRetrieveBitstream(nvjpegHandle_, encoderState_, combined_frame, (size_t*)&total_size, stream_);

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