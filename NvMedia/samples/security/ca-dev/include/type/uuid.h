//
// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// NVIDIA CORPORATION and its licensors retain all intellectual property
// and proprietary rights in and to this software, related documentation
// and any modifications thereto.  Any use, reproduction, disclosure or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA CORPORATION is strictly prohibited.
//

#ifndef UUID_H
#define UUID_H

/// \brief Universally unique identifier (UUID)
struct UUID {
    /// \brief integer giving the low 32 bits of the time
    uint32_t timeLow;
    /// \brief integer giving the middle 16 bits of the time
    uint16_t timeMid;
    /// \brief 4-bit "version" followed by the high 12 bits of the time
    uint16_t timeHiAndVersion;
    /// \brief 1–3-bit "variant" and 13–15-bit clock sequence followed by nodes
    uint8_t clockSeqAndNode[8];
};

#endif /* UUID_H */
