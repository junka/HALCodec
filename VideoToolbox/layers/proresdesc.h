#ifndef LAYERS_PRORESDESC_H_
#define LAYERS_PRORESDESC_H_

#include <cstddef>
#include <cstdint>

namespace halcodec {
namespace vtbox {

// The ProRes picture frame at the front of a buffer, plus what its frame header
// says about the picture.
//
// A ProRes codestream is self-delimiting in a way no elementary video stream is:
// every frame opens with a 32-bit count of its own bytes, so frames split with no
// marker walk at all. That header is also the only place the picture size lives --
// a description built from the wrong dimensions is accepted without complaint and
// hands back a buffer of the described size rather than the coded one, so width
// and height have to be read here.
struct ProresFrame {
    size_t size = 0;       // the frame's byte count, length field included
    bool complete = false; // false while that many bytes have not arrived yet
    int width = 0;
    int height = 0;
    // 4:4:4 rather than 4:2:2. VideoToolbox decodes the two into different pixel
    // formats ('sv44' and 'sv22'), which is what the sample description is built
    // from; the frames themselves decide what comes back.
    bool is444 = false;
    // ProRes RAW, which announces itself with a different frame tag ('prrf') and
    // decodes to a Bayer sensel grid rather than to YUV. It shares everything else
    // with the other flavours: the same self-delimiting count, the same place for
    // the picture size, the same split.
    bool isRaw = false;
};

// True when `data` begins with a ProRes frame: the 'icpf' or 'prrf' frame tag
// sitting behind a byte count large enough to be that frame's own header.
bool LooksLikeProRes(const uint8_t* data, size_t size);

// Describes the frame at the front of `data`. Returns false when that is not a
// ProRes frame, when its header has not fully arrived, or when the header
// contradicts itself (a count shorter than the header, a picture of zero size).
// An incomplete frame still reports its full `size`, which is how a decode pump
// knows how many more bytes to wait for: a truncated frame handed to
// VideoToolbox comes back as -12902 with no picture.
bool ReadProresFrame(const uint8_t* data, size_t size, ProresFrame* out);

}  // namespace vtbox
}  // namespace halcodec

#endif  // LAYERS_PRORESDESC_H_
