/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

/// @file
/// @brief <b> NVIDIA Media Interface: LDC Utility functions </b>
///

#ifndef NVMEDIA_2D_LDCUTIL_H
#define NVMEDIA_2D_LDCUTIL_H

#include "nvmedia_core.h"
#include "nvmedia_2d.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Supported lens models used to generate a Warp Map.
///
/// @sa NvMedia2DLdcGenWarpMap
///
typedef enum
{
    /// @brief Specifies a polynomial distortion model.
    NVMEDIA_2D_LDC_LENS_MODEL_POLYNOMIAL_DISTORTION,

    /// @brief Specifies a fisheye model: r = 2ftan(theta/2), where theta
    ///        is the angle from the optical axis, f is the focal length,
    ///        and r is the distance of a pixel from the image center.
    NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_EQUIDISTANT,

    /// @brief Specifies a fisheye model: r = f*theta, where theta
    ///        is the angle from the optical axis, f is the focal length,
    ///        and r is the distance of a pixel from the image center.
    NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_EQUISOLID,

    /// @brief Specifies a fisheye model: r = 2fsin(theta/2), where theta
    ///        is the angle from the optical axis, f is the focal length,
    ///        and r is the distance of a pixel from the image center.
    NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_ORTHOGRAPHIC,

    /// @brief Specifies a fisheye model: r = fsin(theta), where theta
    ///        is the angle from the optical axis, f is the focal length,
    ///        and r is the distance of a pixel from the image center.
    NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_STEREOGRAPHIC
} NvMedia2DLdcLensModel;

/// @brief Instrinsic camera parameters.
///
/// Camera model parameters that encompass
/// focal length and optical center.
///
/// @sa NvMedia2DLdcGenWarpMap
///
typedef struct
{
    /// @brief Matrix of camera intrinsic parameters.
    ///
    /// Parameters matrix is defined as follow:
    ///             | -f/s_x     S      o_x  |
    ///     M_int = |    0    -f/s_y    o_y  |
    ///             |    0       0       1   |
    ///     where f is a focal length,
    ///     (o_x, o_y) are principal point coordinates,
    ///     S is axis skew,
    ///     and s_x, s_y is the size of the pixels.
    /// The last row is excluded from the matrixCoeffs.
    float matrixCoeffs[2][3];
} NvMedia2DLdcCameraIntrinsic;

/// @brief Extrinsic camera parameters.
///
/// Camera model parameters that define a transform from real world coordinates
/// to camera coordinates.
///
/// @sa NvMedia2DLdcGenWarpMap
///
typedef struct
{
    /// @brief Rotation matrix.
    float R[3][3];
    /// @brief Translation vector.
    float T[3];
} NvMedia2DLdcCameraExtrinsic;

/// @brief Distortion coefficients for the lens model.
///
/// Distortion coefficients are defined in the following way:
/// - k1, k2, k3, k4, k5, k6 are radial distortion coeffcients.
/// - p1 and p2 are tangential distortion coeffcients.
///
/// Setting any coefficient to 0 implies that it is not used in computation.
///
/// If we denote a point without distortion as [x, y, 1] and the corresponding
/// point with distortion as [xd, yd, 1], then the distortion model is defined
/// as follows:
///
/// When #NvMediaLdcLensModel is #NVMEDIA_2D_LDC_LENS_MODEL_POLYNOMIAL_DISTORTION,
/// the control parameters are k1, k2, k3, k4, k5, k6, p1, and p2.
/// - \f$r = sqrt (x ^ 2 + y ^ 2)\f$
/// - \f$kr = (1 + k1 * r^2 + k2 * r^4 + k3 * r^6) /
///           (1 + k4 * r^2 + k5 * r^4 + k6 * r^6)\f$
/// - \f$xd = x * kr + p1 * (2 * x * y) + p2 * (r^2 + 2 * x^2)\f$
/// - \f$yd = y * kr + p1 * (r^2 + 2 * y^2) + p2 * (2 * x * y)\f$
///
/// When NvMediaLensModel is #NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_EQUIDISTANT,
/// #NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_EQUISOLID,
/// #NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_ORTHOGRAPHIC, or
/// #NVMEDIA_2D_LDC_LENS_MODEL_FISHEYE_STEREOGRAPHIC,
/// the control parameters are k1, k2, k3, and k4.
/// - \f$r = sqrt (x ^ 2 + y ^ 2)\f$
/// - \f$theta = atan(r)\f$
/// - \f$theta_d = theta * (1 + k1 * theta^2 +
///                             k2 * theta^4 +
///                             k3 * theta^6 +
///                             k4 * theta^8)\f$
///
/// @sa NvMedia2DLdcGenWarpMap
///
typedef struct
{
    /// @brief Camera model.
    NvMedia2DLdcLensModel model;

    /// @brief Holds the radial distortion coefficient.
    float k1;
    /// @brief Holds the radial distortion coefficient.
    float k2;
    /// @brief Holds the radial distortion coefficient.
    float k3;
    /// @brief Holds the radial distortion coefficient.
    float k4;
    /// @brief Holds the radial distortion coefficient.
    float k5;
    /// @brief Holds the radial distortion coefficient.
    float k6;
    /// @brief Holds the tangential distortion coefficient.
    float p1;
    /// @brief Holds the tangential distortion coefficient.
    float p2;
} NvMedia2DLdcLensDistortion;

/// @brief Helper function to calculate the number of control points
///        from the defined sparse warp map region parameters.
///
/// The result is valid only on the same HW platform where this function was
/// called.
///
/// @param[in] regionParams Pointer to #NvMedia2DLdcRegionParameters
///                         that contains parameters for regions of a Warp Map.
///
/// @param[out] numControlPoints Number of Control Points calculated.
///
/// @return An #NvMediaStatus return code.
/// @retval NVMEDIA_STATUS_OK Number of Control Points has been calculated
///                           successfully.
/// @retval NVMEDIA_STATUS_BAD_PARAMETER One of parameters is invalid.
///                                      This could be either:
///                                      - @a regionParams is NULL
///                                      - @a numControlPoints is NULL
///                                      - @a regionParams has values that
///                                           out of acceptable domain.
/// @retval NVMEDIA_STATUS_NOT_SUPPORTED Current HW platform is not supported.
/// @retval NVMEDIA_STATUS_ERROR An internal failure occurred.
///
/// @pre None.
///
/// @usage
/// - Allowed context for the API call
///   - Interrupt handler: No
///   - Signal handler: No
///   - Thread-safe: Yes, with the following conditions:
///     - Each thread uses different numControlPoints object
///   - Re-entrant: No
///   - Async/Sync: Sync
/// - Required privileges: None
/// - API group
///   - Init: Yes
///   - Runtime: Yes
///   - De-Init: Yes
///
NvMediaStatus
NvMedia2DGetNumLdcControlPoints(NvMedia2DLdcRegionParameters const *const regionParams,
                                uint32_t *numControlPoints);

/// @brief Helper function to fills the given warp map with an identity mapping.
///
/// This function is useful if the user wants to specify their own mapping.
/// It sets the control points coordinates to the destination coordinates as
/// defined implicitly by the #NvMedia2DLdcRegionParameters.
/// The user then can iterate through these points and apply a custom
/// mapping function to each one.
///
/// The result is valid only on the same HW platform where this function was
/// called.
///
/// @param[in] dstRect Pointer to the destination rectangle.
/// @param[in] regionParams Pointer to #NvMedia2DLdcRegionParameters for Warp Map.
/// @param[in] numControlPoints Number of control points in Warp Map.
/// @param[in] controlPoints Pointer to the control points array.
///
/// @return An #NvMediaStatus return code.
/// @retval NVMEDIA_STATUS_OK Warp Map was successfully configured.
/// @retval NVMEDIA_STATUS_BAD_PARAMETER One of parameters is invalid.
///                                      This could be either:
///                                      - @a dstRect is NULL
///                                      - @a regionParams is NULL
///                                      - @a numControlPoints is 0
///                                      - @a controlPoints is NULL
///                                      - @a regionParams has values that
///                                           out of acceptable domain.
///                                      - @a number of control points
///                                           is inconsistent with
///                                           regions config.
/// @retval NVMEDIA_STATUS_NOT_SUPPORTED Current HW platform is not supported.
/// @retval NVMEDIA_STATUS_ERROR An internal failure occurred.
///
/// @pre None.
///
/// @usage
/// - Allowed context for the API call
///   - Interrupt handler: No
///   - Signal handler: No
///   - Thread-safe: Yes, with the following conditions:
///     - Each thread uses different controlPoints object
///   - Re-entrant: No
///   - Async/Sync: Sync
/// - Required privileges: None
/// - API group
///   - Init: Yes
///   - Runtime: Yes
///   - De-Init: Yes
///
NvMediaStatus
NvMedia2DGenLdcWarpMapIdentity(NvMediaRect const *const dstRect,
                               NvMedia2DLdcRegionParameters const *const regionParams,
                               uint32_t numControlPoints,
                               NvMedia2DLdcControlPoint *const controlPoints);

/// @brief Generates Warp Map for a particular lens model.
///
/// The result is valid only on the same HW platform where this function was
/// called.
///
/// @param[in] Kin Pointer to #NvMedia2DLdcCameraIntrinsic parameters.
/// @param[in] X Pointer to #NvMedia2DLdcCameraExtrinsic parameters.
/// @param[in] Kout Pointer to #NvMedia2DLdcCameraIntrinsic parameters.
/// @param[in] distModel Pointer to #NvMedia2DLdcLensDistortion struct.
/// @param[in] dstRect Pointer to the destination rectangle.
/// @param[in] regionParams Pointer to #NvMedia2DLdcRegionParameters for Warp Map.
/// @param[in] numControlPoints Number of control points in Warp Map.
/// @param[in] controlPoints Pointer to the control points array.
///
/// @return An #NvMediaStatus return code.
/// @retval NVMEDIA_STATUS_OK Warp Map is successfully generated.
/// @retval NVMEDIA_STATUS_BAD_PARAMETER One of parameters is invalid.
///                                      This could be either:
///                                      - @a Kin is NULL
///                                      - @a X is NULL
///                                      - @a Kout is NULL
///                                      - @a distModel is NULL
///                                      - @a dstRect is NULL
///                                      - @a regionParams is NULL
///                                      - @a numControlPoints is 0
///                                      - @a controlPoints is NULL
///                                      - @a regionParams has values that
///                                           out of acceptable domain.
///                                      - @a number of control points
///                                           is inconsistent with
///                                           regions config.
///                                      - @a X is invalid, determinant is 0.
/// @retval NVMEDIA_STATUS_NOT_SUPPORTED Current HW platform is not supported.
/// @retval NVMEDIA_STATUS_ERROR An internal failure occurred.
///
/// @pre None.
///
/// @usage
/// - Allowed context for the API call
///   - Interrupt handler: No
///   - Signal handler: No
///   - Thread-safe: Yes, with the following conditions:
///     - Each thread uses different controlPoints object
///   - Re-entrant: No
///   - Async/Sync: Sync
/// - Required privileges: None
/// - API group
///   - Init: Yes
///   - Runtime: Yes
///   - De-Init: Yes
///
NvMediaStatus
NvMedia2DGenLdcWarpMap(
    NvMedia2DLdcCameraIntrinsic const *const Kin,
    NvMedia2DLdcCameraExtrinsic const *const X,
    NvMedia2DLdcCameraIntrinsic const *const Kout,
    NvMedia2DLdcLensDistortion const *const distModel,
    NvMediaRect const *const dstRect,
    NvMedia2DLdcRegionParameters const *const regionParams,
    uint32_t numControlPoints,
    NvMedia2DLdcControlPoint *const controlPoints);

#ifdef __cplusplus
}
#endif

#endif
