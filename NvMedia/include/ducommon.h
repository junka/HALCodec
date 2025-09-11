/*
 * Copyright (c) 2019-2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/*!
 * @file
 *
 * @brief Common definitions and declarations for NVIDIA DRIVE&reg; Update.
 */

#ifndef DUCOMMON_H_
#define DUCOMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/*!
 * @defgroup du_common_group Common Definitions
 *
 * Common definitions and declarations for NVIDIA DRIVE&reg; Update
 *
 * @ingroup drive_update_top
 * @{
 */
/*!
 * Drive Update unit ids.
 *
 * The unit id can be used to generate a unique error code for each unit. The
 * range of the unit id is from 0 to 255, in which 0-127 are reserved by DRIVE
 * Update's released units, and 128-255 can be used by the plug-ins implemented
 * by the user.
 */
/*! Defines the common code unit id. */
#define DU_UID_COMMON       (0U)
/*! Defines the DU-Transport unit id. */
#define DU_UID_TRANSPORT    (1U)
/*! Defines the DU-Link unit id. */
#define DU_UID_LINK         (2U)
/*! Defines the DU-TII unit id. */
#define DU_UID_TII          (3U)
/*! Defines the DU-Master unit id. */
#define DU_UID_MASTER       (4U)
/*! Defines the DU-BHC unit id. */
#define DU_UID_BHC          (5U)
/*! Defines the DU-RPE unit id. */
#define DU_UID_RPE          (6U)
/*! Defines the DU-CCHECK unit id. */
#define DU_UID_CCHECK       (7U)
/*! Defines the DU-AUTH unit id. */
#define DU_UID_AUTH         (8U)
/*! Defines the DU content provider unit id. */
#define DU_UID_CONTENT      (9U)
/*! Defines the DU-DECOMP unit id. */
#define DU_UID_DECOMP       (10U)

/*!
 * Drive Update error codes.
 *
 * The error code type is defined as an unsigned 32-bit integer. There are two
 * classes of error codes: common codes and unit specific codes. The common
 * error codes can be applied to multiple units, while the unit specific error
 * codes is applied to one certain unit only.
 */
typedef uint32_t DU_RCODE;

/*!
 * Helper macro to define a unique error code, in which @c uid is the unit id,
 * @c suid is the sub-unit id and @c err is an integer code.
 */
#define DU_ECODE(uid, suid, err) DU_ERR_CODE(uid, suid, err)

/*! Offset of UID in DU_ERR_CODE. */
#define DU_ERR_UID_OFFSET (24U)

/*! Mask of UID in DU_ERR_CODE. */
#define DU_ERR_UID_MASK ((uint32_t) 0xFFU)

/*! Offset of SUID in DU_ERR_CODE. */
#define DU_ERR_SUID_OFFSET (20U)

/*! Mask of SUID in DU_ERR_CODE. */
#define DU_ERR_SUID_MASK ((uint32_t) 0xFU)

/*! Mask of ERR in DU_ERR_CODE. */
#define DU_ERR_MASK ((uint32_t) 0xFFFFFU)

/*! Generates a unique error code. */
#define DU_ERR_CODE(uid, suid, err) \
    (DU_RCODE)((((uid)  & DU_ERR_UID_MASK)  << DU_ERR_UID_OFFSET)  | \
               (((suid) & DU_ERR_SUID_MASK) << DU_ERR_SUID_OFFSET) | \
               ((err)   & DU_ERR_MASK))

/*! Gets the unit id. */
#define DU_ECODE_GET_UID(rcode)  (((rcode) >> 24U) & 0xFFU)
/*! Gets the sub-unit. */
#define DU_ECODE_GET_SUID(rcode) (((rcode) >> 20U) & 0xFU)
/*! Gets the integer error code. */
#define DU_ECODE_GET_ERR(rcode)  ((rcode) & 0xFFFFFU)

/*! \brief Defines the success code. */
#define DU_OK (DU_RCODE)(0U)

// Common error codes used by shared components only
#define DUCOMMON_ECODE(err)            DU_ECODE(DU_UID_COMMON, 0U, (err))

#define DUCOMMON_ERR_GENERIC           DUCOMMON_ECODE(1U)
#define DUCOMMON_ERR_INVALID_ARGUMENT  DUCOMMON_ECODE(2U)
#define DUCOMMON_ERR_NOT_FOUND         DUCOMMON_ECODE(3U)
#define DUCOMMON_ERR_BUFFER_TOO_SMALL  DUCOMMON_ECODE(4U)
#define DUCOMMON_ERR_SYNTAX            DUCOMMON_ECODE(5U)

/*! \brief Defines the default long string buffer size. */
#define DU_STR_LONG_BUF_SIZE  (128U)

/*! \brief Defines the default short string buffer size. */
#define DU_STR_SHORT_BUF_SIZE (32U)

/*! \brief Defines the max path size. */
#define DU_PATH_MAX           (512U)

/*! \brief Defines the max command length. */
#define DU_MAX_CMD_LEN        (512U)

/*! \brief Defines the buffer size of 1KB. */
#define DU_1KB   (1024U)
/*! \brief Defines the buffer size of 2KB. */
#define DU_2KB   (2U * DU_1KB)
/*! \brief Defines the buffer size of 4KB. */
#define DU_4KB   (4U * DU_1KB)
/*! \brief Defines the buffer size of 8KB. */
#define DU_8KB   (8U * DU_1KB)
/*! \brief Defines the buffer size of 16KB. */
#define DU_16KB  (16U * DU_1KB)
/*! \brief Defines the buffer size of 32KB. */
#define DU_32KB  (32U * DU_1KB)
/*! \brief Defines the buffer size of 64KB. */
#define DU_64KB  (64U * DU_1KB)
/*! \brief Defines the buffer size of 128KB. */
#define DU_128KB (128U * DU_1KB)
/*! \brief Defines the buffer size of 256KB. */
#define DU_256KB (256U * DU_1KB)
/*! \brief Defines the buffer size of 512KB. */
#define DU_512KB (512U * DU_1KB)
/*! \brief Defines the buffer size of 1MB. */
#define DU_1MB   (1024U * DU_1KB)


/*! \brief Defines the run level. */
typedef enum DU_RUN_LEVEL
{
    /*!
     * Dormant run level (default). No connectivity or any activity allowed. 
     * The only command that can be issued in this runlevel is a transition to a higher runlevel.
     */
    RL_DORMANT = 0U,
    /*!
     * Monitor run level. Allowed for connection to the cloud, checks for new available updates and Small size telemetrics upload.
     * No large data transfers or update installations.
     */
    RL_MONITOR,
    /*!
     * Telecast run level. Similar to RL_MONITOR and allowed for larger telemetrics uploads.
     * No installation or download of updates allowed.
     * System performance may be affected due to shared resource utilization such as internet and disk activity.
     */
    RL_TELECAST,
    /*!
     * Transfer run level. Similar to RL_TELECAST and allowed for Update download/pre-fetch.
     * No installation of updates allowed.
     * System performance may further be affected due to shared resource utilization such as internet and disk activity.
     */
    RL_XFER,
    /*!
     * Background apply run level. Similar to RL_XFER and allowed for Background installation of updates.
     * No functionality may be disabled at any time.
     * System performance may be affected due to heavy shared resource utilization such as internet, disk and other resource activity.
     */
    RL_BG_APPLY,
    /*!
     * Full apply run level. Similar to RL_BG_APPLY and allowed for Complete update installation activities.
     * System may temporarily be unavailable (e.g. reboots or taking components offline to update).
     */
    RL_FULL_APPLY,
    /*!
     * Unrestricted run level. Similar to RL_FULL_APPLY and allowed for Unrestricted update installation activities.
     * Only used for very major updates, such as first level repartitioning or storage failure recovery.
     */
    RL_UNRESTRICTED,
    /*!
     *Invalid run level.
     */
    RL_INVALID,
} DU_RUN_LEVEL;

/*! \brief Defines the max size of the run level string. */
#define DU_RUNLEVEL_STR_MAX_SIZE      (16U)
/** @} */

#ifdef __cplusplus
}
#endif

#endif // DUCOMMON_H_
