Copyright (c) 2022 NVIDIA Corporation. All rights reserved.

NVIDIA Corporation and its licensors retain all intellectual property and
proprietary rights in and to this software, related documentation and any
modifications thereto. Any use, reproduction, disclosure or distribution
of this software and related documentation without an express license
agreement from NVIDIA Corporation is strictly prohibited.

# FSI-CCPLEX Communication README

FSI-CCPLEX-COM Generic use case enables Data communication in peer-to-peer mode for SWCs/applications running on FSI and CCPLEX using shared memory (FSI CPU NS DRAM carveout). Distinguishing aspect of this use-case is that the raw data would be completely transparent to the communication mechanism and hence this data does not comprise of any signal/service provided over any vehicle communication bus unlike the vIPC use case.

This component provides interfaces to access DRAM carveout which is used as shared memory for communication between CCPLEX and FSI and Mailbox used to notify the message reception/transmissions.

Use case is demonstrated by two demo apps which are DemoAppCom (on CCPLEX) and DemoApp (on FSI).

NvFsiComLib is an interface library on CCPLEX side for FSI-CCPLEX communication.

NvFsiCom is a daemon which is responsible for notifying when data is available to read.

cdCcplexCom – CDD on FSI to access shared memory and handle data receive notification.

DemoAppCom simulates Application on CCPLEX. DemoApp connected to Rte to demonstrate SWC behavior for sending and receiving the data between CCPLEX and FSI. This demo can be used as base for any new development and requirement realization by Customers or for DRIVE AV.

Shared memory of size 16 MB is available to be used.

#FILES

1. DemoApp Sample Source code

   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/Fsi-ccplex-com/DemoAppCom/DemoAppCom.c

2. Configuration DT file:

   $PDK_TOP/drive-t186ref-qnx /bsp/device-tree/hardware/nvidia/platform/t23x/automotive/kernel-dts/common/qnx/tegra234-qnx-safetyservice-fsicom.dtsi

   $PDK_TOP/drive-t186ref-qnx /bsp/device-tree/hardware/nvidia/platform/t23x/automotive/kernel-dts/common/qnx/tegra234-common-qnx-gos0.dtsi

3. Header files

   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/Fsi-ccplex-com/inc/NvFsiCom.h – Application interface for NvFsiCom

   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/Fsi-ccplex-com/inc/NvFsiComTypes.h – Datatypes for NvFsiCom

4. Binary files

   DemoApp binaries:

   $PDK_TOP/drive-t186ref-qnx/nvidia-bsp/aarch64le/sbin/DemoAppCom – Sample demo app to demonstrate FSI-CCPLEX-COM generic use case

   NvFsiCom daemon:

   $PDK_TOP/drive-t186ref-qnx/nvidia-bsp/aarch64le/sbin/NvFsiCom – Resource manager used for FSI-CCPLEX communication

   Libraries:

   $PDK_TOP/drive-t186ref-qnx/nvidia-bsp/aarch64le/usr/lib/libNvFsiComLib.so – Client library to be used for communicating with FSI from CCPLEX

#Configuration

   Refer FSI-CCPLEX-Configuration section in PDK document.

# UseCases

   Refer FSI-CCPLEX-Use-Case section in PDK document.

# Demonstration

  Sending Single message from CCPLEX to FSI

  1. Launch the DemoAppCom from the GOS console. Once started it displays the options as below. For AV+L, launch DemoAppCom using command:
     /home/nvidia/drive-linux/samples/FsiCom/DemoAppCom

  2. Select s for sending a single frame. Each frame consists of Message ID (2 bytes), Message Length (4 bytes) and Message Data. Message ID and Message Length are to demonstrate sample Application protocol to identify different messages. Applications will need to develop their own protocol.

With choice ‘s’ an option is given to select the message ID and message length, for which the maximum value is limited to 58. Along with a single byte which will fill the message Data.

  3. DemoApp on FSI displays the received message from CCPLEX on FSI

  4. FSI DemoApp sends the response by updating the message data by adding 0xA to each received byte and sends it back to the CCPLEX.

  5. DemoAppCom on receiving the data validates the message length and displays the message in GOS console

 Example of output on GOS console -

./DemoAppCom

  DriveOS_FsiComDemoApp main menu
  _____________________________________________

  [m] Display the menu
  [s] Send a frame to FSI
  [p] Periodically send data to FSI
  [q] Terminate the app

 s
 Enter Message Id (0x0 - 0xFFFF)
 21
 Enter Message Length (DemoApp Supports maximum 58 bytes)
 10
 select single byte pattern to fill the message (0x00-0xFF)
 1
 written 16 size on channel_0
 NvFsiComWaitForEvent received for channel_0

 Received Frame from FSI on Channel_0
 Message Id : 0x0021
 Message Length : 10
 Message Data :
 0x0B 0x0B 0x0B 0x0B 0x0B 0x0B 0x0B 0x0B 0x0B 0x0B

Please refer PDK document for more details.