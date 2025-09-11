/*
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef NVSOCKETWRAPPER_ENDPOINT_HEADER_INCLUDE
#define NVSOCKETWRAPPER_ENDPOINT_HEADER_INCLUDE

#include "nvsocketwrapper_message.h"
#include "nvsocketwrapper_types.h"
#include <functional>
#include <memory>
#include <map>

namespace NvSocketWrapper
{
    using MsgResponderType = std::function<void (std::shared_ptr<Message>, ErrorCode)>;

    class Endpoint
    {
        protected:
            Endpoint& operator=(const Endpoint&) = default;

        public:
            virtual ~Endpoint() {};
            virtual ErrorCode SendMsg(const std::shared_ptr<Message> msg) = 0;
            virtual ErrorCode RecvMsg(const std::shared_ptr<Message> msg, const int32_t timeoutMs) const = 0;
            virtual ErrorCode InitServer(const std::string inputIpAddr, const uint16_t port) = 0;
            virtual ErrorCode InitClient(const std::string inputIpAddr, const uint16_t port, const std::string remoteServerIpAddr, const uint16_t remoteServerPort) = 0;
            virtual ErrorCode AcceptConnection(const int32_t timeoutMs, const bool throwErrorOnce = false) = 0;
            virtual ErrorCode DeinitServer() = 0;
            virtual ErrorCode DeinitClient() = 0;
            virtual void SetCallback(const MsgResponderType msgResponder) = 0;
            virtual void SetRemoteIpAddr(const std::string inputIpAddr) = 0;
            virtual void SetRemotePort(const uint16_t port) = 0;
            virtual void SetLocalIpAddr(const std::string inputIpAddr) = 0;
            virtual void SetLocalPort(const uint16_t port) = 0;
            virtual bool IsClientConnected(const std::string clientIpAddr, const uint16_t clientPort) = 0;
            virtual void SetMaxMsgSize(const uint32_t size) = 0;
            virtual ErrorCode ConnectToServer() = 0;
            static std::shared_ptr<Endpoint> GetInstance(const TPType inputTpType);
    };
}

#endif // NVSOCKETWRAPPER_ENDPOINT_HEADER_INCLUDE
