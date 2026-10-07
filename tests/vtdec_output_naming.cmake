# vtdec_output_naming.cmake — hal_dec's output names, and the two ways the old
# ones were wrong.
#
# 1. The raw output stream used to be opened before the input was read. The
#    default name is the input's own name with the extension replaced by
#    --format, so any --format that matched the input's extension handed the run
#    an output path that was also its input: the open truncated it, the feed then
#    read an empty file, and the run reported "Decode total frames: 0" and exited
#    0 (measured: `hal_dec -i s.h264 -f h264` zeroed s.h264). The stream now
#    opens at the first frame that needs it, and a name that resolves to one of
#    the inputs -- by device and inode, so a symlink counts -- is refused before
#    anything is written.
# 2. The name list holds one entry per input file, but the BMP path consumed one
#    entry per decoded *frame*, and started at the second because the pre-opened
#    stream had taken the first. Every run of `-f bgr`/`rgb`/`rgbi`/`bgri`/`y`
#    read past a one-element vector (rc 139/138/134 depending on what the heap
#    held, and when the garbage happened to parse the picture went to a hidden
#    `./.bmp` in the cwd), and a directory walk shifted each picture onto its
#    neighbour's name. Past the end of the list the last name is numbered now, so
#    a stream of N pictures still leaves N files.
# 3. --format reaches the decoders through CodecParams::outputFormat, and exactly
#    one backend reads it: NVJPEG/layers/nvjpegdecoder.cc. VideoToolbox answers in
#    the layout its stream carries, and the BMP writer then indexed three
#    full-size planes in a frame holding half that many bytes -- a
#    heap-buffer-overflow at bmp_write.h:104 under ASan. The frame's own format is
#    checked now, so the run says which pixels it was handed instead of reading
#    past them.
# 4. An 8-bit BMP has a 256-entry palette in front of its pixels, and the header's
#    pixel offset stayed at the 24-bit value of 54: consumers read the palette as
#    the first 1024 pixels. Measured before the fix -- every frame of the run
#    below scored the same 11.7 dB against every source picture, which is what a
#    shift looks like rather than wrong pixels; after it, each scores ~34-36 dB
#    against its own and 17-20 dB against the others. The palette's own bytes are
#    pinned too: the writer filled each entry's fourth, reserved byte with the
#    grey level, which reads as a ramp of opacities to anything that looks at it.
#    ffprobe calls the result pal8 either way -- an 8-bit palettised BMP is what
#    it is, and ffmpeg's own grey BMP reads back the same -- so the ramp is the
#    only place the greyness is actually recorded.
#
# The BMP case runs on a monochrome 8-bit HEVC stream because that is the one
# picture this backend delivers in a format the grey writer can legitimately read;
# everything else about --format is asserted as a refusal.
# Skips when ffmpeg cannot encode mono HEVC or the vtbox backend cannot load.

if(NOT HAL_DEC OR NOT FFMPEG OR NOT FFPROBE OR NOT INPUT_NV12 OR NOT WORKDIR)
    message(FATAL_ERROR "HAL_DEC, FFMPEG, FFPROBE, INPUT_NV12 and WORKDIR must be set")
endif()

set(FRAMES 3)
set(bytes_per_frame 76800)
math(EXPR frame_bytes "${bytes_per_frame} * ${FRAMES}")
set(W "${WORKDIR}/dnaming_320x240")
file(REMOVE_RECURSE "${W}")
file(MAKE_DIRECTORY "${W}")

# A stray hidden file is what the out-of-bounds name used to turn into, and
# file(GLOB) does not list dotfiles, so it needs its own check.
macro(no_stray_bmp where)
    if(EXISTS "${W}/.bmp")
        file(REMOVE "${W}/.bmp")
        message(FATAL_ERROR "${where}: a picture was written to ${W}/.bmp, which is "
                "what an index past the end of the name list decays into")
    endif()
endmacro()

# Counting the directory is how "the run invented a file" gets caught -- the old
# pre-open created a zero-byte output even when it decoded nothing.
macro(dir_count out)
    file(GLOB _seen "${W}/*")
    list(LENGTH _seen ${out})
endmacro()

# ---------------------------------------------------------------------------
# Material: a 3-frame 8-bit monochrome HEVC stream (for the grey BMP path) and a
# 3-frame 4:2:0 one (for the refusal), both from the bundled NV12. Streams come
# from libx265 rather than from hal_enc so the encoder cannot cover for the
# decoder.
# ---------------------------------------------------------------------------
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240
                -framerate 25 -i ${INPUT_NV12} -frames:v ${FRAMES}
                -pix_fmt gray -f rawvideo -y "${W}/src.gray"
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message("SKIP vtdec.output_naming: ffmpeg cannot produce grey frames (rc=${rc})")
    return()
endif()
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format gray -video_size 320x240
                -framerate 25 -i "${W}/src.gray" -c:v libx265 -preset ultrafast
                -x265-params mono=1 -frames:v ${FRAMES} -f hevc
                -y "${W}/mono.h265"
                RESULT_VARIABLE rc2 OUTPUT_VARIABLE o2 ERROR_VARIABLE e2)
if(NOT rc2 EQUAL 0)
    message("SKIP vtdec.output_naming: this ffmpeg cannot encode a mono 8-bit "
            "stream (${o2}${e2})")
    return()
endif()
execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error
                -select_streams v -show_entries stream=pix_fmt
                -of default=nw=1 "${W}/mono.h265"
                RESULT_VARIABLE rc3 OUTPUT_VARIABLE probe ERROR_QUIET)
if(NOT rc3 EQUAL 0 OR NOT probe MATCHES "pix_fmt=gray")
    message(FATAL_ERROR "the mono stream is not single-component 8-bit (${probe}) -- "
            "the grey BMP case below would be asserting nothing")
endif()

# The 4:2:0 one, whose decoded frames are NV12 -- the layout the BMP writer must
# refuse rather than read 1.5x past.
execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel error
                -f rawvideo -pixel_format nv12 -video_size 320x240
                -framerate 25 -i ${INPUT_NV12} -frames:v ${FRAMES}
                -c:v libx265 -preset ultrafast -f hevc -y "${W}/y420.h265"
                RESULT_VARIABLE rc4 OUTPUT_QUIET ERROR_QUIET)
if(NOT rc4 EQUAL 0)
    message("SKIP vtdec.output_naming: ffmpeg cannot encode 4:2:0 HEVC (rc=${rc4})")
    return()
endif()

# The source pictures, one file each, for the pairing check below.
file(SIZE "${W}/src.gray" src_size)
if(NOT src_size EQUAL ${frame_bytes})
    message(FATAL_ERROR "${W}/src.gray holds ${src_size} bytes, not ${frame_bytes} "
            "(${FRAMES} grey 320x240 frames) -- the pairing check has nothing to "
            "cut pictures out of")
endif()
math(EXPR LAST "${FRAMES} - 1")
foreach(i RANGE 0 ${LAST})
    # Each dd operand is one whole quoted argument: a quote in the middle of a
    # token survives into argv, and dd then looks for a file whose name has quote
    # characters in it.
    execute_process(COMMAND dd "if=${W}/src.gray" "of=${W}/r${i}.gray"
                    "bs=${bytes_per_frame}" "skip=${i}" count=1
                    RESULT_VARIABLE rcd OUTPUT_VARIABLE odst ERROR_VARIABLE ddst)
    if(NOT rcd EQUAL 0)
        message(FATAL_ERROR "dd cannot cut reference frame ${i} out of src.gray "
                "(rc=${rcd}):\n${odst}${ddst}")
    endif()
    file(SIZE "${W}/r${i}.gray" rsz)
    if(NOT rsz EQUAL ${bytes_per_frame})
        message(FATAL_ERROR "reference frame ${i} holds ${rsz} bytes, not "
                "${bytes_per_frame}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# 1. Grey BMP output: three pictures, three numbered files, each one the picture
#    it is named after, and each header saying where its pixels really start.
# ---------------------------------------------------------------------------
dir_count(before1)
execute_process(COMMAND ${HAL_DEC} -b vtbox -i "${W}/mono.h265" -f y
                WORKING_DIRECTORY "${W}"
                RESULT_VARIABLE rcA OUTPUT_VARIABLE outA ERROR_VARIABLE errA)
if(rcA EQUAL 137 OR rcA GREATER 0)
    message(FATAL_ERROR "hal_dec -f y exited with ${rcA}\n${outA}${errA}")
endif()
no_stray_bmp("hal_dec -f y")
if(NOT outA MATCHES "Decode total frames: ${FRAMES}")
    message(FATAL_ERROR "-f y decoded not ${FRAMES} pictures:\n${outA}${errA}")
endif()
# The name the run prints is the file it makes: for a BMP format the derived
# extension is bmp, not the format string, because that is what the bytes are.
string(FIND "${outA}" "${W}/mono.bmp" echoed)
if(echoed EQUAL -1)
    message(FATAL_ERROR "-f y printed no ${W}/mono.bmp in its name list:\n${outA}")
endif()

# The header, read off the bytes. bfSize is at 2..5 and bfOffBits at 10..13, both
# little-endian; a 320x240 grey picture is 54 + 1024 + 76800 bytes with the pixels
# starting after the palette.
math(EXPR gray_frame_bytes "320 * 240")
math(EXPR bmp_bytes "54 + 256 * 4 + ${gray_frame_bytes}")
math(EXPR pixel_offset "54 + 256 * 4")

# What the 1024 palette bytes have to be: entry i is (i,i,i) and the fourth byte,
# which the format reserves, reads as zero. This is the check that gives the file
# its greyness -- ffprobe reports pal8 for an 8-bit BMP however it was made, and
# measured, ffmpeg's own grey BMP reads back the same way, so the pix_fmt line
# below cannot carry this claim.
set(hexdigits 0 1 2 3 4 5 6 7 8 9 a b c d e f)
set(expected_pal "")
foreach(v RANGE 0 255)
    math(EXPR vhi "${v} / 16")
    math(EXPR vlo "${v} % 16")
    list(GET hexdigits ${vhi} hch)
    list(GET hexdigits ${vlo} lch)
    string(APPEND expected_pal "${hch}${lch}${hch}${lch}${hch}${lch}00")
endforeach()
string(LENGTH "${expected_pal}" expected_pal_len)
if(NOT expected_pal_len EQUAL 2048)
    message(FATAL_ERROR "the reference palette came out ${expected_pal_len} hex "
            "characters wide, not 2048")
endif()

foreach(i RANGE 1 ${FRAMES})
    if(i EQUAL 1)
        set(bmp "${W}/mono.bmp")
    else()
        set(bmp "${W}/mono_${i}.bmp")
    endif()
    if(NOT EXISTS "${bmp}")
        message(FATAL_ERROR "picture ${i} left no ${bmp}: the name list is one entry "
                "per input file, and this stream is one file with ${FRAMES} "
                "pictures -- they need numbered names (${outA})")
    endif()
    file(SIZE "${bmp}" bsz)
    if(NOT bsz EQUAL ${bmp_bytes})
        message(FATAL_ERROR "${bmp} holds ${bsz} bytes, not ${bmp_bytes} "
                "(54 header + 1024 palette + ${gray_frame_bytes} pixels)")
    endif()
    file(READ "${bmp}" hdr LIMIT 14 HEX)
    string(REGEX REPLACE "[^0-9a-fA-F]" "" hdr "${hdr}")
    string(LENGTH "${hdr}" hdrlen)
    if(NOT hdrlen EQUAL 28)
        message(FATAL_ERROR "cannot read ${bmp}'s 14-byte file header as hex (${hdr})")
    endif()
    string(SUBSTRING "${hdr}" 4 8 size_le)
    string(SUBSTRING "${hdr}" 20 8 off_le)
    string(SUBSTRING "${size_le}" 0 2 s0)
    string(SUBSTRING "${size_le}" 2 2 s1)
    string(SUBSTRING "${size_le}" 4 2 s2)
    string(SUBSTRING "${size_le}" 6 2 s3)
    string(SUBSTRING "${off_le}" 0 2 o0)
    string(SUBSTRING "${off_le}" 2 2 o1)
    string(SUBSTRING "${off_le}" 4 2 o2)
    string(SUBSTRING "${off_le}" 6 2 o3)
    math(EXPR declared_size "0x${s3}${s2}${s1}${s0}")
    math(EXPR declared_off "0x${o3}${o2}${o1}${o0}")
    if(NOT declared_size EQUAL bsz)
        message(FATAL_ERROR "${bmp} says it is ${declared_size} bytes and is ${bsz}")
    endif()
    if(NOT declared_off EQUAL pixel_offset)
        message(FATAL_ERROR "${bmp} says its pixels start at ${declared_off}, but a "
                "256-entry palette after the 54-byte headers puts them at "
                "${pixel_offset} -- at ${declared_off} a reader takes the palette for "
                "the first ${pixel_offset} pixels")
    endif()
    # The palette itself, off the same bytes. OFFSET and LIMIT count bytes here
    # even though HEX asks for hex characters.
    file(READ "${bmp}" pal HEX OFFSET 54 LIMIT 1024)
    string(REGEX REPLACE "[^0-9a-fA-F]" "" pal "${pal}")
    if(NOT pal STREQUAL expected_pal)
        string(SUBSTRING "${expected_pal}" 0 32 exp_head)
        string(SUBSTRING "${pal}" 0 32 got_head)
        message(FATAL_ERROR "${bmp}'s palette is not the grey ramp with a zero "
                "reserved byte that its 8-bit header promises. Expected ${exp_head}... "
                "(entry i is i,i,i then 00), read ${got_head}...")
    endif()
    execute_process(COMMAND ${FFPROBE} -hide_banner -loglevel error
                    -select_streams v -show_entries stream=codec_name,width,height,pix_fmt
                    -of default=nw=1 "${bmp}"
                    RESULT_VARIABLE rcp OUTPUT_VARIABLE pr ERROR_QUIET)
    if(NOT rcp EQUAL 0)
        message(FATAL_ERROR "ffprobe cannot read ${bmp}")
    endif()
    foreach(need codec_name=bmp width=320 height=240 pix_fmt=pal8)
        if(NOT pr MATCHES "${need}")
            message(FATAL_ERROR "${bmp} is not a 320x240 8-bit palettised image (${pr})")
        endif()
    endforeach()
endforeach()

# Pairing: picture i's file has to match source frame i better than any other,
# and well. This is the check that catches the palette offset (which shifted every
# frame by the same 1024 bytes and scored 11.7 dB everywhere), a bottom-up/top-down
# flip gone wrong, and names landing one picture apart.
foreach(i RANGE 1 ${FRAMES})
    if(i EQUAL 1)
        set(bmp "${W}/mono.bmp")
        set(own_idx 0)
    else()
        set(bmp "${W}/mono_${i}.bmp")
        math(EXPR own_idx "${i} - 1")
    endif()
    set(own_psnr -1)
    set(rival_psnr -1)
    foreach(k RANGE 0 ${LAST})
        execute_process(COMMAND ${FFMPEG} -hide_banner -loglevel info
                        -s 320x240 -f rawvideo -pixel_format gray
                        -i "${W}/r${k}.gray" -i "${bmp}"
                        -lavfi "[0:v]settb=AVTB[a];[a][1:v]psnr" -f null -
                        RESULT_VARIABLE rcp ERROR_VARIABLE psnr_out OUTPUT_QUIET)
        if(NOT rcp EQUAL 0)
            message(FATAL_ERROR "psnr comparison failed for picture ${i}: ${psnr_out}")
        endif()
        string(REGEX MATCH "average:([0-9]+\\.?[0-9]*)" _m "${psnr_out}")
        if(NOT CMAKE_MATCH_1)
            message(FATAL_ERROR "no psnr average for picture ${i}: ${psnr_out}")
        endif()
        set(score "${CMAKE_MATCH_1}")
        if(k EQUAL own_idx)
            set(own_psnr "${score}")
        elseif(score GREATER rival_psnr)
            set(rival_psnr "${score}")
        endif()
    endforeach()
    if(NOT own_psnr GREATER 30)
        message(FATAL_ERROR "picture ${i} does not match its own source frame (PSNR "
                "${own_psnr} dB, best rival ${rival_psnr} dB)")
    endif()
    if(NOT rival_psnr LESS own_psnr)
        message(FATAL_ERROR "picture ${i} matches another source frame better than "
                "its own (${rival_psnr} dB vs ${own_psnr} dB) -- the pictures are "
                "not landing in the files they were named after")
    endif()
    message("vtdec.output_naming: picture ${i} ok (PSNR ${own_psnr} dB against its "
            "own frame, best rival ${rival_psnr} dB)")
endforeach()

# Nothing invented beside them, and the stream is still what it was.
dir_count(after1)
math(EXPR want1 "${before1} + ${FRAMES}")
if(NOT after1 EQUAL want1)
    message(FATAL_ERROR "${W} holds ${after1} files after the -f y run, which began "
            "with ${before1} and should have added ${FRAMES}")
endif()
file(SIZE "${W}/mono.h265" insize)
if(NOT insize GREATER 1000)
    message(FATAL_ERROR "${W}/mono.h265 is down to ${insize} bytes")
endif()

# ---------------------------------------------------------------------------
# 2. An output that names an input is refused before it can truncate it. Both the
#    derived form (--format is the input's own extension) and the explicit one
#    (-o repeats the input) are the same file by device and inode.
# ---------------------------------------------------------------------------
file(SIZE "${W}/y420.h265" vref_size)
execute_process(COMMAND ${CMAKE_COMMAND} -E md5sum "${W}/y420.h265"
                OUTPUT_VARIABLE vref_md5 ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "[0-9a-f]+" vref_md5 "${vref_md5}")

foreach(collide "-f;h265" "-o;${W}/y420.h265;-f;nv12")
    dir_count(beforec)
    execute_process(COMMAND ${HAL_DEC} -b vtbox -i "${W}/y420.h265" ${collide}
                    WORKING_DIRECTORY "${W}"
                    RESULT_VARIABLE rcc OUTPUT_VARIABLE outc ERROR_VARIABLE errc)
    no_stray_bmp("refusal case '${collide}'")
    if(NOT rcc EQUAL 255)
        message(FATAL_ERROR "hal_dec ${collide} should refuse and exit -1 (255); it "
                "exited with ${rcc} -- anything else is the old behavior of decoding "
                "the file it had just truncated\n${outc}${errc}")
    endif()
    if(NOT errc MATCHES "is one of the inputs")
        message(FATAL_ERROR "hal_dec ${collide} exited with ${rcc} but not with the "
                "refusal:\n${outc}${errc}")
    endif()
    if(outc MATCHES "Decode total frames")
        message(FATAL_ERROR "hal_dec ${collide} decoded and wrote something after "
                "naming an input as its output:\n${outc}")
    endif()
    file(SIZE "${W}/y420.h265" size_after)
    if(NOT size_after EQUAL vref_size)
        message(FATAL_ERROR "${W}/y420.h265 was ${vref_size} bytes and the run wrote "
                "${size_after} -- the output was opened before the input was read")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E md5sum "${W}/y420.h265"
                    OUTPUT_VARIABLE md5_after ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "[0-9a-f]+" md5_after "${md5_after}")
    if(NOT md5_after STREQUAL vref_md5)
        message(FATAL_ERROR "${W}/y420.h265 still holds ${size_after} bytes but not "
                "the same bytes (${md5_after} vs ${vref_md5})")
    endif()
    dir_count(afterc)
    if(NOT afterc EQUAL beforec)
        message(FATAL_ERROR "the refused run '${collide}' left ${afterc} files in ${W}, "
                "which held ${beforec} -- it made an output it never wrote")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# 3. A format the decoder was not asked to produce is refused, not read past.
#    Every one of these used to be a crash or a picture written under a name taken
#    from past the end of the list.
# ---------------------------------------------------------------------------
foreach(need "bgr;asks for BGR pixels" "rgb;asks for RGB pixels"
             "rgbi;interleaved")
    list(GET need 0 fmt)
    list(GET need 1 phrase)
    dir_count(before2)
    execute_process(COMMAND ${HAL_DEC} -b vtbox -i "${W}/y420.h265" -f ${fmt}
                    WORKING_DIRECTORY "${W}"
                    RESULT_VARIABLE rcB OUTPUT_VARIABLE outB ERROR_VARIABLE errB)
    no_stray_bmp("-f ${fmt} on a 4:2:0 stream")
    if(NOT rcB EQUAL 255)
        message(FATAL_ERROR "hal_dec -f ${fmt} should answer with a reason and exit -1 "
                "(255); it exited with ${rcB} -- a signal death is what reading the "
                "frame past its end looks like\n${outB}${errB}")
    endif()
    if(NOT errB MATCHES "${phrase}")
        message(FATAL_ERROR "hal_dec -f ${fmt} exited with ${rcB} without saying why "
                "(${phrase}):\n${outB}${errB}")
    endif()
    # An NV12 frame is what VideoToolbox hands back here, and the message has to
    # name it: that is the part a user can act on.
    if(fmt STREQUAL "bgr" OR fmt STREQUAL "rgb")
        if(NOT errB MATCHES "NV12")
            message(FATAL_ERROR "hal_dec -f ${fmt} refused without naming the pixels "
                    "it was handed:\n${errB}")
        endif()
    endif()
    dir_count(after2)
    if(NOT after2 EQUAL before2)
        message(FATAL_ERROR "hal_dec -f ${fmt} refused but still made a file "
                "(${before2} -> ${after2} in ${W}) -- the output was opened before "
                "there was a frame to write")
    endif()
    message("vtdec.output_naming: -f ${fmt} refused as expected (${errB})")
endforeach()

# ---------------------------------------------------------------------------
# 4. The path that worked before still works: a raw decode into one file named
#    after the input, with the stream itself left alone.
# ---------------------------------------------------------------------------
dir_count(before3)
execute_process(COMMAND ${HAL_DEC} -b vtbox -i "${W}/y420.h265" -f nv12
                WORKING_DIRECTORY "${W}"
                RESULT_VARIABLE rcC OUTPUT_VARIABLE outC ERROR_VARIABLE errC)
if(rcC EQUAL 137 OR rcC GREATER 0)
    message(FATAL_ERROR "hal_dec -f nv12 exited with ${rcC}\n${outC}${errC}")
endif()
if(NOT outC MATCHES "Decode total frames: ${FRAMES}")
    message(FATAL_ERROR "-f nv12 decoded not ${FRAMES} frames:\n${outC}${errC}")
endif()
math(EXPR raw_bytes "320 * 240 * 3 / 2 * ${FRAMES}")
file(SIZE "${W}/y420.nv12" rawsz)
if(NOT rawsz EQUAL raw_bytes)
    message(FATAL_ERROR "${W}/y420.nv12 holds ${rawsz} bytes, not ${raw_bytes} "
            "(${FRAMES} NV12 frames)")
endif()
dir_count(after3)
math(EXPR want3 "${before3} + 1")
if(NOT after3 EQUAL want3)
    message(FATAL_ERROR "the -f nv12 run left ${after3} files where it began with "
            "${before3} and should have added one")
endif()
message("vtdec.output_naming: ${FRAMES} NV12 frames, ${rawsz} bytes, one file")

# ... and the same run with -o naming the file: the name given is the name used.
# The derived name from the case above is still on disk here, so what says
# "-o won" is that the directory grew by the one file and no numbered companion
# appeared beside it -- this backend appends every picture into the file it was
# given rather than making one per picture.
dir_count(before4)
execute_process(COMMAND ${HAL_DEC} -b vtbox -i "${W}/y420.h265" -f nv12
                -o "${W}/explicit.nv12" WORKING_DIRECTORY "${W}"
                RESULT_VARIABLE rcD OUTPUT_VARIABLE outD ERROR_VARIABLE errD)
if(rcD EQUAL 137 OR rcD GREATER 0)
    message(FATAL_ERROR "hal_dec -f nv12 -o explicit.nv12 exited with ${rcD}\n${outD}${errD}")
endif()
file(SIZE "${W}/explicit.nv12" dsz)
if(dsz EQUAL 0)
    message(FATAL_ERROR "${W}/explicit.nv12 is empty: -o was taken as the name but "
            "the frames went somewhere else")
endif()
if(NOT dsz EQUAL raw_bytes)
    message(FATAL_ERROR "${W}/explicit.nv12 holds ${dsz} bytes, not ${raw_bytes}")
endif()
if(EXISTS "${W}/explicit_2.nv12")
    file(REMOVE "${W}/explicit_2.nv12")
    message(FATAL_ERROR "-o named one output and the run numbered a second one next "
            "to it")
endif()
dir_count(after4)
math(EXPR want4 "${before4} + 1")
if(NOT after4 EQUAL want4)
    message(FATAL_ERROR "the -o run left ${after4} entries in ${W}, which held "
            "${before4}: it should have added exactly the file it was given")
endif()
message("vtdec.output_naming: -o names the output file, ${dsz} bytes")
