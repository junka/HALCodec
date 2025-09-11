/*
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef NVSOCKETWRAPPER_MESSAGE_HEADER_INCLUDE
#define NVSOCKETWRAPPER_MESSAGE_HEADER_INCLUDE

#include "nvsocketwrapper_types.h"
#include <memory>
#include <string>
#include <vector>

namespace NvSocketWrapper
{
    class Message
    {
        protected:
            Message& operator=(const Message&) = default;

        public:
            virtual ~Message() {};

            virtual ErrorCode SetData(const std::shared_ptr<std::vector<uint8_t>> data) = 0;
            virtual std::shared_ptr<std::vector<uint8_t>> GetData() const = 0;
            virtual uint32_t GetSize() const = 0;
            virtual void SetLocalIPAddr(const std::string inputIpAddr) = 0;
            virtual std::string GetLocalIPAddr() const = 0;
            virtual void SetLocalPort(const uint16_t port) = 0;
            virtual uint16_t GetLocalPort() const = 0;
            virtual void SetRemoteIPAddr(const std::string inputIpAddr) = 0;
            virtual std::string GetRemoteIPAddr() const = 0;
            virtual void SetRemotePort(const uint16_t port) = 0;
            virtual uint16_t GetRemotePort() const = 0;
            virtual bool IsEmpty() const = 0;

            static std::shared_ptr<Message> GetInstance();
            static std::shared_ptr<Message> GetInstance(const uint32_t size);
    };
}

#endif // NVSOCKETWRAPPER_MESSAGE_HEADER_INCLUDE
