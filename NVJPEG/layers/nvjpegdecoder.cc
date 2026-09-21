#include "nvjpegdecoder.h"

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <fstream>
#include <nvjpeg.h>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <vector>

#include "frame.h"
#include "registry.h"

namespace halcodec {
namespace nvjpeg {

NVJPEGDecoder::NVJPEGDecoder() {
    int ret = cuInit(0);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuInit error" << std::endl;
    }
}

bool NVJPEGDecoder::Initialize(const CodecParams& params) {
    if (params.inputs.empty()) {
        std::cerr << "NVJPEGDecoder: no input specified" << std::endl;
        return false;
    }
    const std::string input = params.inputs[0];
    zeroCopy_ = params.zeroCopy;

    // Map the unified output format to an nvjpeg output format.
    outputfmt_ = NVJPEG_OUTPUT_YUV;
    switch (params.outputFormat) {
        case PixelFormat::RGB: outputfmt_ = NVJPEG_OUTPUT_RGB; break;
        case PixelFormat::BGR: outputfmt_ = NVJPEG_OUTPUT_BGR; break;
        case PixelFormat::GRAY: outputfmt_ = NVJPEG_OUTPUT_Y; break;
        default:
            if (params.outputFormat != PixelFormat::Unknown) {
                std::cout << "NVJPEGDecoder: falling back to YUV output for format "
                          << static_cast<int>(params.outputFormat) << std::endl;
            }
            break;
    }

    CUdevice cuDevice_ = 0;
    int idx = params.deviceIndex;
    int ret = cuDeviceGet(&cuDevice_, idx);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGet error" << std::endl;
        return false;
    }
    char szDeviceName[80];
    ret = cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuDeviceGetName error" << std::endl;
        return false;
    }
    ret = cuCtxCreate(&cuContext_, 0, cuDevice_);
    if (ret != CUDA_SUCCESS) {
        std::cout << "cuCtxCreate error" << std::endl;
        return false;
    }
    cudaDeviceProp props;
    ret = cudaGetDeviceProperties(&props, 0);
    if (ret != CUDA_SUCCESS) {
        std::cout << "get device properties error" << std::endl;
        return false;
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
    nvjpegDecodeBatchedInitialize(nvjpegHandle_, jpegState_, batch_size_, 1, outputfmt_);

    struct stat info;
    if (stat(input.c_str(), &info) != 0) {
        std::cout << "Cannot access " << input << std::endl;
        return false;
    }

    if (info.st_mode & S_IFDIR) { // Check if input is a directory
        DIR *dir;
        struct dirent *ent;
        std::string dirPath = input;
        while (!dirPath.empty() && dirPath.back() == '/') {
            dirPath.pop_back();
        }
        if ((dir = opendir(dirPath.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == DT_REG) { // Regular file
                    file_names_.emplace_back(dirPath + '/' + ent->d_name);
                }
            }
            closedir(dir);
        } else {
            std::cout << "Could not open directory " << input << std::endl;
            return false;
        }
        std::cout << "Number of files in directory: " << file_names_.size() << std::endl;
    } else if (info.st_mode & S_IFREG) { // Check if input is a regular file
        std::cout << "Input is a file: " << input << std::endl;
        file_names_.emplace_back(input.c_str());
    } else {
        std::cout << "Input is neither a file nor a directory" << std::endl;
        return false;
    }
    if (file_names_.size() > 16) {
        batch_size_ = 16;
    }
    if (batch_size_ == 1) {
        create_decouple_api();
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
    return true;
}

void NVJPEGDecoder::create_decouple_api()
{
    int ret = nvjpegDecoderCreate(nvjpegHandle_, NVJPEG_BACKEND_DEFAULT, &decoder_);
    ret = nvjpegDecoderStateCreate(nvjpegHandle_, decoder_, &decoupled_state_);

    ret = nvjpegBufferPinnedCreate(nvjpegHandle_, NULL, &pinned_buffers_[0]);
    ret = nvjpegBufferPinnedCreate(nvjpegHandle_, NULL, &pinned_buffers_[1]);
    ret = nvjpegBufferDeviceCreate(nvjpegHandle_, NULL, &device_buffer_);

    ret = nvjpegJpegStreamCreate(nvjpegHandle_, &jpeg_streams_[0]);
    ret = nvjpegJpegStreamCreate(nvjpegHandle_, &jpeg_streams_[1]);
    ret = nvjpegDecodeParamsCreate(nvjpegHandle_, &decode_params_);
}

void NVJPEGDecoder::destroy_deouple_api() {
    int ret = nvjpegDecodeParamsDestroy(decode_params_);
    ret = nvjpegJpegStreamDestroy(jpeg_streams_[0]);
    ret = nvjpegJpegStreamDestroy(jpeg_streams_[1]);
    ret = nvjpegBufferPinnedDestroy(pinned_buffers_[0]);
    ret = nvjpegBufferPinnedDestroy(pinned_buffers_[1]);
    ret = nvjpegBufferDeviceDestroy(device_buffer_);
    ret = nvjpegJpegStateDestroy(decoupled_state_);
    ret = nvjpegDecoderDestroy(decoder_);
}

void NVJPEGDecoder::Finalize() {
    delete[] combined_host_;
    combined_host_ = nullptr;
    combined_cap_ = 0;
    cudaStreamDestroy(stream_);

    if (batch_size_ == 1) {
        destroy_deouple_api();
    }
    nvjpegJpegStateDestroy(jpegState_);
    nvjpegDestroy(nvjpegHandle_);

    cuCtxDestroy(cuContext_);
}

int NVJPEGDecoder::PullFrames() {
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
    nvjpegChromaSubsampling_t subsampling;
    int widths[NVJPEG_MAX_COMPONENT];
    int heights[NVJPEG_MAX_COMPONENT];

    for (int i = 0; i < data_.size(); i++) {
        nvjpegGetImageInfo(nvjpegHandle_, (const unsigned char*)data_[i].data(), file_len_[i],
            &channels, &subsampling, widths, heights);

        img_widths_[i] = widths[0];
        img_heights_[i] = heights[0];
        subsamplings_[i] = subsampling;
        std::cout << "Image is " << channels << " channels." << std::endl;
        for (int c = 0; c < channels; c++) {
            std::cout << "Channel #" << c << " size: " << widths[c] << " x "
                    << heights[c] << std::endl;
        }
        int mul = 1;
        // in the case of interleaved RGB output, write only to single channel, but
        // 3 samples at once
        if (outputfmt_ == NVJPEG_OUTPUT_RGBI || outputfmt_ == NVJPEG_OUTPUT_BGRI) {
            channels = 1;
            mul = 3;
        } else if (outputfmt_ == NVJPEG_OUTPUT_RGB || outputfmt_ == NVJPEG_OUTPUT_BGR) {
            // in the case of rgb create 3 buffers with sizes of original image
            channels = 3;
            widths[1] = widths[2] = widths[0];
            heights[1] = heights[2] = heights[0];
        } else if (outputfmt_ == NVJPEG_OUTPUT_Y) {
            channels = 1;
            mul = 1;
        }
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

    cudaEventCreate(&startEvent);
    cudaEventCreate(&stopEvent);
    if (batch_size_ == 1) {
        cudaEventRecord(startEvent, stream_);
        for (int i = 0; i < batch_size_; i++) {
            nvjpegDecode(nvjpegHandle_, jpegState_, (const unsigned char *)data_[i].data(), file_len_[i], outputfmt_, &out_[i], stream_);
        }
        cudaEventRecord(stopEvent, stream_);
    } else {
        std::vector<const unsigned char *> raw_inputs;
        for (int i = 0; i < batch_size_; i++) {
            raw_inputs.push_back((const unsigned char *)data_[i].data());
        }
        cudaEventRecord(startEvent, stream_);
        nvjpegDecodeBatched(nvjpegHandle_, jpegState_, raw_inputs.data(), file_len_.data(), out_.data(), stream_);
        cudaEventRecord(stopEvent, stream_);
    }
    num_decoded += batch_size_;
    cudaEventSynchronize(stopEvent);
    cudaEventElapsedTime(&loopTime, startEvent, stopEvent);
    time = static_cast<double>(loopTime);
    std::cout << "Decode time " << time << "ms" << std::endl;
    return num_decoded;
}

bool NVJPEGDecoder::GetFrame(CodecFrame& out) {
    int idx = batch_size_ - num_decoded;

    // Zero-copy: hand the nvjpegImage_t device planes straight out as a
    // CudaDevice frame. nvjpeg decodes Y/U/V into separate cudaMalloc
    // allocations (not contiguous), so this is a multi-plane device frame:
    // cudaPlanes[]/cudaPitches[] with cudaNumPlanes set. Plane 0 also mirrors
    // into cudaPtr/cudaPitch for any single-plane consumer. The device buffers
    // are decoder members reused across frames; release() only decrements the
    // outstanding count (the synchronous PullFrames->GetFrame drain in hal_dec
    // consumes each frame before the next PullFrames can realloc/overwrite
    // out_[]).
    if (zeroCopy_) {
        int nPlanes = 0;
        for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++) {
            if (out_[idx].channel[c] != nullptr) {
                out.device.cudaPlanes[c] =
                    reinterpret_cast<uintptr_t>(out_[idx].channel[c]);
                out.device.cudaPitches[c] = out_[idx].pitch[c];
                nPlanes++;
            } else {
                break;
            }
        }
        out.device.cudaNumPlanes = nPlanes;
        out.device.cudaPtr = out.device.cudaPlanes[0];
        out.device.cudaPitch = out.device.cudaPitches[0];
        out.data = nullptr;
        out.size = 0;  // device-resident; host size unknown until downloaded
        out.width = img_widths_[idx];
        out.height = img_heights_[idx];
        out.strides[0] = out_[idx].pitch[0];
        out.locality = FrameLocality::CudaDevice;
        switch (outputfmt_) {
            case NVJPEG_OUTPUT_YUV:
                out.format = (subsamplings_[idx] == NVJPEG_CSS_420)
                                 ? PixelFormat::I420 : PixelFormat::YUV444P;
                break;
            case NVJPEG_OUTPUT_RGB: out.format = PixelFormat::RGB; break;
            case NVJPEG_OUTPUT_BGR: out.format = PixelFormat::BGR; break;
            case NVJPEG_OUTPUT_Y: out.format = PixelFormat::GRAY; break;
            case NVJPEG_OUTPUT_RGBI: out.format = PixelFormat::RGB; break;
            case NVJPEG_OUTPUT_BGRI: out.format = PixelFormat::BGR; break;
            default: out.format = PixelFormat::Unknown; break;
        }
        out.release = [this]() { num_decoded--; };
        return true;
    }

    int total_size = 0;
    int chanels = 0;

    // Calculate total size for YUV format
    if (outputfmt_ == NVJPEG_OUTPUT_Y) {
        total_size = img_heights_[idx] * out_[idx].pitch[0];
    } else {
        for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++) {
            if (out_[idx].channel[c] != nullptr) {
                total_size += img_heights_[idx] * out_[idx].pitch[c];
                chanels ++;
            }
        }
    }

    // Reuse the member host buffer, growing it only when needed, instead of
    // allocating a fresh buffer on every frame.
    uint8_t* combined_frame = nullptr;
    if (total_size > combined_cap_) {
        delete[] combined_host_;
        combined_host_ = new uint8_t[total_size];
        combined_cap_ = total_size;
    }
    combined_frame = combined_host_;

    // Copy each channel into the combined buffer
    int offset = 0;

    if (outputfmt_ == NVJPEG_OUTPUT_YUV) {
        // Interleave YUV channels based on subsampling
        for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++) {
            if (out_[idx].channel[c] != nullptr) {
                int subsample_factor_h = 1;
                int subsample_factor_w = 1;

                // Determine subsampling factors based on chroma subsampling type
                switch (subsamplings_[idx]) {
                    case NVJPEG_CSS_420:
                        subsample_factor_h = (c == 0) ? 1 : 2; // Y: 1, UV: 2
                        subsample_factor_w = (c == 0) ? 1 : 2;
                        break;
                    case NVJPEG_CSS_422:
                        subsample_factor_h = 1;
                        subsample_factor_w = (c == 0) ? 1 : 2; // Y: 1, UV: 2
                        break;
                    case NVJPEG_CSS_444:
                        subsample_factor_h = 1;
                        subsample_factor_w = 1; // No subsampling
                        break;
                    case NVJPEG_CSS_440:
                        subsample_factor_h = (c == 0) ? 1 : 2; // Y: 1, UV: 2
                        subsample_factor_w = 1;
                        break;
                    case NVJPEG_CSS_411:
                        subsample_factor_h = 1;
                        subsample_factor_w = (c == 0) ? 1 : 4; // Y: 1, UV: 4
                        break;
                    case NVJPEG_CSS_410:
                        subsample_factor_h = (c == 0) ? 1 : 2; // Y: 1, UV: 2
                        subsample_factor_w = (c == 0) ? 1 : 4;
                        break;
                    case NVJPEG_CSS_GRAY:
                        subsample_factor_h = 1;
                        subsample_factor_w = 1; // Grayscale
                        break;
                    default:
                        std::cerr << "Unsupported chroma subsampling type" << std::endl;
                        return false;
                }

                int channel_height = img_heights_[idx] / subsample_factor_h;
                // The source rows (pitch bytes wide) are copied verbatim, so a
                // single 2D transfer replaces the original row-by-row loop.
                cudaMemcpy2D(combined_frame + offset, out_[idx].pitch[c],
                             out_[idx].channel[c], out_[idx].pitch[c],
                             out_[idx].pitch[c], channel_height,
                             cudaMemcpyDeviceToHost);
                offset += channel_height * out_[idx].pitch[c];
            }
        }
    } else if (outputfmt_ == NVJPEG_OUTPUT_Y) {
        if (out_[idx].channel[0] != nullptr) {
            cudaMemcpy2D(combined_frame, img_widths_[idx], out_[idx].channel[0], out_[idx].pitch[0],
                img_widths_[idx], img_heights_[idx], cudaMemcpyDeviceToHost);
            chanels = 1;
        }
    } else if (outputfmt_ == NVJPEG_OUTPUT_BGR || outputfmt_ == NVJPEG_OUTPUT_RGB) {
        for (int c = 0; c < NVJPEG_MAX_COMPONENT; c++) {
            if (out_[idx].channel[c] != nullptr) {
                int channel_size = img_heights_[idx] * out_[idx].pitch[c];
                cudaMemcpy2D(combined_frame + offset, img_widths_[idx], out_[idx].channel[c],
                             out_[idx].pitch[c], img_widths_[idx], img_heights_[idx], cudaMemcpyDeviceToHost);
                offset += channel_size;
            }
        }
    } else if (outputfmt_ == NVJPEG_OUTPUT_BGRI || outputfmt_ == NVJPEG_OUTPUT_RGBI) {
        if (out_[idx].channel[0] != nullptr) {
            cudaMemcpy2D(combined_frame, img_widths_[idx] * 3, out_[idx].channel[0], out_[idx].pitch[0],
                img_widths_[idx] * 3, img_heights_[idx], cudaMemcpyDeviceToHost);
        }
        chanels = 3;
    }

    out.data = combined_frame;
    out.size = total_size;
    out.width = img_widths_[idx];
    out.height = img_heights_[idx];
    out.strides[0] = out_[idx].pitch[0];
    switch (outputfmt_) {
        case NVJPEG_OUTPUT_YUV:
            out.format = (subsamplings_[idx] == NVJPEG_CSS_420)
                             ? PixelFormat::I420 : PixelFormat::YUV444P;
            break;
        case NVJPEG_OUTPUT_RGB: out.format = PixelFormat::RGB; break;
        case NVJPEG_OUTPUT_BGR: out.format = PixelFormat::BGR; break;
        case NVJPEG_OUTPUT_Y: out.format = PixelFormat::GRAY; break;
        case NVJPEG_OUTPUT_RGBI: out.format = PixelFormat::RGB; break;
        case NVJPEG_OUTPUT_BGRI: out.format = PixelFormat::BGR; break;
        default: out.format = PixelFormat::Unknown; break;
    }
    out.release = [this]() {
        // The combined buffer is owned by the decoder (combined_host_) and is
        // reused across frames; only the outstanding-frame count is decremented.
        num_decoded--;
    };

    return true;
}

// DownloadToHost hook for nvjpeg's multi-plane CudaDevice frames. nvjpeg
// decodes Y/U/V into separate cudaMalloc allocations (not one contiguous
// block), so the NVDEC single-block download hook does not apply. This hook
// claims only multi-plane frames (cudaNumPlanes > 0) and copies each plane
// row-by-row into a tightly-packed host buffer, mirroring the host-path
// layout GetFrame() produces in non-zero-copy mode (so the downstream file/BMP
// writers see identical bytes either way). Returns false for single-block
// device frames so the NVDEC hook handles them.
namespace {
bool DownloadNVJPEGCudaFrame(CodecFrame& f) {
    if (f.locality != FrameLocality::CudaDevice
        || f.device.cudaNumPlanes <= 0) {
        return false;
    }
    const int w = f.width;
    const int h = f.height;
    const int nPlanes = f.device.cudaNumPlanes;

    // Per-plane row bytes and line counts. nvjpeg pitches planes to the full
    // image width (x bpp for interleaved), so the tight row bytes is just the
    // pitch's data width, not the pitch itself.
    struct Plane { int rowBytes; int lines; };
    Plane planes[4] = {};
    int bpp = 1;
    switch (f.format) {
        case PixelFormat::I420:
            planes[0] = {w, h};
            planes[1] = {w / 2, h / 2};
            planes[2] = {w / 2, h / 2};
            break;
        case PixelFormat::YUV444P:
            planes[0] = {w, h};
            planes[1] = {w, h};
            planes[2] = {w, h};
            break;
        case PixelFormat::GRAY:
            planes[0] = {w, h};
            break;
        case PixelFormat::RGB:
        case PixelFormat::BGR:
            // nvjpeg RGB/BGR output is planar: 3 full-size planes.
            planes[0] = {w, h};
            planes[1] = {w, h};
            planes[2] = {w, h};
            break;
        default:
            return false;  // layout unknown: leave the frame device-resident
    }

    size_t total = 0;
    for (int c = 0; c < nPlanes; c++) {
        total += static_cast<size_t>(planes[c].rowBytes) * planes[c].lines;
    }
    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(total ? total : 1));
    if (!buffer) {
        return false;
    }

    // Each plane is its own device allocation; copy with its own pitch into a
    // tightly-packed destination region. cudaMemcpy2D handles the row-by-row
    // detile. The context owning the pointers is discovered via
    // cuPointerGetAttribute so this works on a thread with no current context.
    CUcontext ctx = nullptr;
    CUdeviceptr src0 = static_cast<CUdeviceptr>(f.device.cudaPlanes[0]);
    if (cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, src0)
            != CUDA_SUCCESS || !ctx) {
        std::free(buffer);
        return false;
    }
    CUcontext prev = nullptr;
    bool pushed = cuCtxPushCurrent(ctx) == CUDA_SUCCESS;
    size_t dstOff = 0;
    bool ok = true;
    for (int c = 0; c < nPlanes; c++) {
        CUDA_MEMCPY2D m = {};
        m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        m.srcDevice = static_cast<CUdeviceptr>(f.device.cudaPlanes[c]);
        m.srcPitch = f.device.cudaPitches[c];
        m.dstMemoryType = CU_MEMORYTYPE_HOST;
        m.dstHost = buffer + dstOff;
        m.dstPitch = static_cast<size_t>(planes[c].rowBytes);
        m.WidthInBytes = static_cast<size_t>(planes[c].rowBytes);
        m.Height = static_cast<unsigned>(planes[c].lines);
        if (cuMemcpy2D(&m) != CUDA_SUCCESS) {
            ok = false;
            break;
        }
        dstOff += static_cast<size_t>(planes[c].rowBytes) * planes[c].lines;
    }
    if (pushed) {
        cuCtxPopCurrent(&prev);
    }
    // The device frame is no longer needed once the copy completed.
    if (f.release) {
        f.release();
        f.release = nullptr;
    }
    if (!ok) {
        std::free(buffer);
        return false;
    }
    f.data = buffer;
    f.size = total;
    f.strides[0] = static_cast<size_t>(planes[0].rowBytes);
    f.locality = FrameLocality::Host;
    f.device = {};
    f.release = [buffer]() { std::free(buffer); };
    return true;
}

const bool g_nvjpegDownloadRegistered = [] {
    RegisterCudaFrameDownload(&DownloadNVJPEGCudaFrame);
    return true;
}();
}  // namespace

HALCODEC_CONNECT(Decoder, nvjpeg, NVJPEGDecoder);

} // namespace nvjpeg
} // namespace halcodec