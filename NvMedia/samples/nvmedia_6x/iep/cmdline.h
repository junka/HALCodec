/*
* SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
* SPDX-License-Identifier: LicenseRef-NvidiaProprietary
*
* NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
* property and proprietary rights in and to this material, related
* documentation and any modifications thereto. Any use, reproduction,
* disclosure or distribution of this material and related documentation
* without an express license agreement from NVIDIA CORPORATION or
* its affiliates is strictly prohibited.
*/

#ifndef _NVMEDIA_TEST_CMD_LINE_H_
#define _NVMEDIA_TEST_CMD_LINE_H_

#include <stdbool.h>

#include "nvmedia_common_encode.h"

#define DEFAULT_GOP_SIZE                30
#define DEFAULT_FRAME_SIZE              15000
#define ARRAYS_ALOCATION_SIZE           1000
#define FILE_NAME_SIZE                  256
#define FILE_NAME_SIZE_EXTRA            (FILE_NAME_SIZE+32)
#define MAX_CONFIG_SECTIONS             32
#define INTERVAL_PATTERN_MAX_LENGTH     1000
#define MAX_PAYLOAD_ARRAY_SIZE          10
#define FILE_PATH_LENGTH_MAX            256
#define IMAGE_BUFFERS_POOL_SIZE         5
#define IMAGE_BUFFERS_POOL_SIZE_MAX     16
#define IMAGE_BUFFERS_POOL_SIZE_MIN     1
#define RC_SLIDING_WINDOW               60

#if defined(__QNX__)
// Cycle count measurement can be converted to nanosecond by multiplication with 32.
#define CYCLECOUNT_TO_NS                32
#endif

typedef struct {
    char        crcFilename[FILE_PATH_LENGTH_MAX];
    bool crcGenMode;
    bool crcCheckMode;
} CRCOptions;

typedef struct _EncodePicParams {
    unsigned int            encodePicFlags;
    unsigned long long      inputDuration;
    NvMediaEncodePicType    pictureType;
    unsigned int            PicParamsSectionNum;
    unsigned int            rcParamsSectionNum;
} EncodePicParams;

typedef struct _EncodePicParamsH264 {
    bool     refPicFlag;
    unsigned int    forceIntraRefreshWithFrameCnt;
    unsigned char   sliceTypeData[ARRAYS_ALOCATION_SIZE];
    unsigned int    sliceTypeArrayCnt;
    char            payloadArrayIndexes[MAX_PAYLOAD_ARRAY_SIZE];
    unsigned int    payloadArraySize;
    unsigned int    mvcPicParamsSectionNum;
} EncodePicParamsH264;

typedef struct _EncodeH264SEIPayload {
    unsigned int    payloadSize;
    unsigned int    payloadType;
    unsigned char   payload[ARRAYS_ALOCATION_SIZE];
} EncodeH264SEIPayload;

typedef struct _EncodePicParamsH265 {
    bool     refPicFlag;
    unsigned int    forceIntraRefreshWithFrameCnt;
    unsigned char   sliceTypeData[ARRAYS_ALOCATION_SIZE];
    unsigned int    sliceTypeArrayCnt;
    char            payloadArrayIndexes[MAX_PAYLOAD_ARRAY_SIZE];
    unsigned int    payloadArraySize;
} EncodePicParamsH265;

typedef struct _EncodeH265SEIPayload {
    unsigned int    payloadSize;
    unsigned int    payloadType;
    unsigned char   payload[ARRAYS_ALOCATION_SIZE];
} EncodeH265SEIPayload;

typedef struct _EncodeConfig {
    unsigned char   profile;
    bool            enableExtProfile;
    NvMediaEncodeExtProfile extProfile;
    unsigned char   level;
    int             gopPattern;
    unsigned int    gopLength;
    unsigned int    encodeWidth;
    unsigned int    encodeHeight;
    unsigned int    darWidth;
    unsigned int    darHeight;
    unsigned int    frameRateNum;
    unsigned int    frameRateDen;
    unsigned char   maxNumRefFrames;
    bool            enableROIEncode;
    bool            enableAnonEncode;
    bool            useBFramesAsRef;
    bool            enableAllIFrames;
    bool            enableMemoryOptimization;
    bool            enableSsimRdo;
    bool            enableTileEncode;
    unsigned int    log2NumTilesInRow;
    unsigned int    log2NumTilesInCol;
    unsigned int    vp9SkipChroma;
    unsigned int    frameRestorationType;
    bool            enableBiCompound;
    bool            enableUniCompound;
    unsigned int    numEpCores;
    bool            ampDisable;
    bool            enableExternalMEHints;
    unsigned int    ulNumPIIRegions;
} EncodeConfig;

typedef struct _EncodeRCParams {
    NvMediaEncodeParamsRCMode   rcMode;
    unsigned int                rcConstQPSectionNum;
    unsigned int                averageBitRate;
    unsigned int                maxBitRate;
    unsigned int                vbvBufferSize;
    unsigned int                vbvInitialDelay;
    bool                        enableMinQP;
    bool                        enableMaxQP;
    unsigned int                rcMinQPSectionNum;
    unsigned int                rcMaxQPSectionNum;
} EncodeRCParams;

typedef struct _EncodeRCStats {
    bool bInitialized;
    uint32_t  frame_number;
    uint64_t  totalBits;
    uint64_t  stuffedBits;
    // cbr deviation
    int32_t  cbr_mvavg_deviation;
    int32_t  min_cbr_mvavg_deviation;
    int32_t  max_cbr_mvavg_deviation;
    int32_t  cbr_vbv_deviation;
    int32_t  min_cbr_vbv_deviation;
    int32_t  max_cbr_vbv_deviation;
    // vbr deviation
    int32_t  vbr_deviation;
    int32_t  min_vbr_deviation;
    int32_t  max_vbr_deviation;
    // vbv parameters
    int32_t  vbv_fullness_percent;
    uint32_t  vbv_violations;
    // sliding window
    int32_t  slidingWindowIdx;
    int32_t  removingPicIdx;
    int32_t  slidingWindowSize;
    int32_t  pictureBits[RC_SLIDING_WINDOW];
    int32_t  vbvFullnessHist[RC_SLIDING_WINDOW];
    int32_t  movingAvgBits;
    double stuffingOverhead;
    int32_t  vbvRate;
    int32_t  vbvFullness;
    int32_t  minVbvFullness;
    // init
    int32_t  bit_rate;
    int32_t  hrdBits;
    int32_t  frame_rate;
    int32_t  vbvSize;
    int32_t  vbvBitrate;
    // vbv_fullness calculated from encoder RC
    uint32_t  rc_vbv_fullness;
}EncoderRCStats;

typedef struct _TestArgs {
    char                        infile[FILE_NAME_SIZE];
    char                        outfile[FILE_NAME_SIZE];
#if !NV_IS_SAFETY
    char                        PIIParamFileName[FILE_NAME_SIZE];
    char                        frameStatsFileName[FILE_NAME_SIZE];
    char                        RCStatsFileName[FILE_NAME_SIZE];
    char                        mvDataFileName[FILE_NAME_SIZE];
    char                        extradataFileName[FILE_NAME_SIZE];
    char                        dynResFileName[FILE_NAME_SIZE];
    char                        ROIParamFileName[FILE_NAME_SIZE];
    char                        qpDeltaFileBaseName[FILE_NAME_SIZE];
    char                        infiledrc[FILE_NAME_SIZE];
    char                        frmTypeChangeFileName[FILE_NAME_SIZE];
    char                        dynBitrateFileName[FILE_NAME_SIZE];
    char                        dynFpsFileName[FILE_NAME_SIZE];
    char                        fslFileName[FILE_NAME_SIZE];
    char                        extHintFileBaseName[FILE_NAME_SIZE];
    unsigned int                dynResFrameNum;
    unsigned int                dynResFrameWidth;
    unsigned int                dynResFrameHeight;
    unsigned int                qpDeltaMapBufferEnabled;
    unsigned int                drcBufRealloc;
#endif
    unsigned int                inputFileFormat;
    unsigned int                startFrame;
    unsigned int                framesToBeEncoded;
    unsigned int                videoCodec;
    unsigned int                maxOutputBuffering;
    unsigned int                rateControlSectionNum;
    char                        frameIntervalPattern[INTERVAL_PATTERN_MAX_LENGTH];
    unsigned int                frameIntervalPatternLength;
    EncodeConfig                configParams;
    NvMediaEncodeConfigH264     configH264Params;
    EncodePicParams             picParamsCollection[MAX_CONFIG_SECTIONS];
    EncodePicParamsH264         picH264ParamsCollection[MAX_CONFIG_SECTIONS];
    EncodeRCParams              rcParamsCollection[MAX_CONFIG_SECTIONS];
    EncodeH264SEIPayload        payloadsCollection[MAX_CONFIG_SECTIONS];
    NvMediaEncodeQP             quantizationParamsCollection[MAX_CONFIG_SECTIONS];
    EncoderRCStats              rcStats;

    NvMediaEncodeConfigH265     configH265Params;
    EncodePicParamsH265         picH265ParamsCollection[MAX_CONFIG_SECTIONS];
    EncodeH265SEIPayload        payloadsH265Collection[MAX_CONFIG_SECTIONS];

    NvMediaEncodeConfigVP9      configVP9Params;
    NvMediaEncodeConfigAV1      configAV1Params;
    unsigned long long int      sumCycleCount;

    CRCOptions                  crcoption;
    bool                        eventDataRecorderMode;
    int                         eventDataRecorderRecordingTime;
    int                         logLevel;

    unsigned int                instanceId;

    bool                        preFetchBuffer;
    bool                        negativeTest;
    bool                        skipImageRegister;
    bool                        version;
    uint32_t                    alternateCreateAPI;
    bool                        enableInternalHighBitDepth;
#if !NV_IS_SAFETY
    bool                        enableExtradata;
    bool                        dumpFrameSizeLog;
    unsigned char               dumpFslLevel;
#endif
#if ENABLE_PROFILING
    int                         profileEnable;
    int                         limitFPS;
    char                        profileStatsFilePath[FILE_NAME_SIZE];
    int                         profileTestEnable;
    double                      initLat;
    double                      submitLat;
    double                      execLat;
#endif
    uint64_t                    loopCount;
    bool                        enableVMSuspend;
} TestArgs;

void PrintUsage(void);
int  ParseArgs(int argc, char **argv, TestArgs *args);

#endif /* _NVMEDIA_TEST_CMD_LINE_H_ */
