Copyright (c) 2022 NVIDIA Corporation. All rights reserved.

NVIDIA Corporation and its licensors retain all intellectual property and
proprietary rights in and to this software, related documentation and any
modifications thereto. Any use, reproduction, disclosure or distribution
of this software and related documentation without an express license
agreement from NVIDIA Corporation is strictly prohibited.

# EPL - README

EPL (Error Propagation Library) provides an interface to clients running on CCPLEX to report errors. It is a dynamic linked shared object and it forwards error report packets to daemon (EPD).

EPD (Error propagation daemon) synchronizes access to HSP IP from different clients (running on QNX VMs). This daemon provides an interface for different instances of error propagation library to connect. It serializes error reports from all instances of EPL and sends it to EPS (FSI) via TOP2_HSP.

For AV+L EPD is built as part of the linux kernel driver. Once the linux kernel driver boots up it provides an interface for different instances of error propagation library to connect. It serializes error reports from all instances of EPL and sends it to EPS (FSI) via TOP2_HSP.

Error reporting use case is demonstrated by demo app – DemoAppSwErr which runs on CCPLEX and demonstrates usage of Error Propagation Library to report errors.

NvCCPLEX_FSI_App is running on CCPLEX. It is for demonstrating the reception of critical failures reported from SEH to CCPLEX over FSI-CCPLEX-COM generic channel.

For more information refer - PDK document.
# FILES -

This section lists files with exposed interfaces or configuration for the component.

1. DemoApp Sample Source code
   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/ErrorPropagation/DemoAppSwErr/DemoAppSwErr.c

2. NvCCPLEX_FSI_App Source code
   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/Fsi-ccplex-com/CcplexApp/CcplexApp.c

3. DemoAppSwErr_QM Sample App Source code
   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/ErrorPropagation/DemoAppSwErr_qm/DemoAppSwErr_QM.c

4. Header files
   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/ErrorPropagation/lib/NvEpl.h - Application interface for EPL
   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/ErrorPropagation/inc/SafetyServiceType.h - Datatypes for EPL
   $PDK_TOP/drive-t186ref-qnx/auto-safety/ccplex_sf/ErrorPropagation/lib_qm/NvEpl_QM.h

5. Binary files
   a. DemoApp binaries:
      $PDK_TOP/drive-t186ref-qnx/nvidia-bsp/aarch64le/sbin/DemoAppSwErr – Sample demo app to demonstrate error reporting use case using EPL.
   b. NvCCPLEX_FSI_App binary:
      $PDK_TOP/drive-t186ref-qnx/nvidia-bsp/aarch64le/sbin/CcplexApp - Sample demo app to demonstrate the reception of critical failures reported from SEH to CCPLEX
   c. EPD Daemon:
      $PDK_TOP/drive-t186ref-qnx/nvidia-bsp/aarch64le/sbin/Epd – Resource manager used for managing TOP2_HSP communication between FSI and CCPLEX.
   d. Libraries:
      $PDK_TOP/drive-t186ref-qnx/nvidia-bsp/aarch64le/usr/lib/libNvEpl.so – Client library to be used for reporting errors.

6. Configuration files
   a. EPS configuration DT file:
      $PDK_TOP/drive-t186ref-qnx/hardware/nvidia/soc/t23x/kernel-dts/tegra234-soc/tegra234-safetyservice-cfg.dtsi

For more information refer - PDK document.

# USE CASES

Refer fsi_use_cases section in PDK document.

Usage of DemoAppSwErr

1. Launch CcplexApp on GOS console in background.
    For AV+L CcplexApp is present in
    /home/nvidia/drive-linux/samples/FsiCom.
    Use command 'CcplexApp &'.

2. Using DemoAppSwErr report SW error by running
    this command from GOS console:

    DemoAppSwErr 0x8005 0x12345678 0xABCDEF99

    By running this command we are reporting SW error with
     ReporterId = 0x8005
     ErrorCode = 0x12345678
     ErrorAttribute = 0xABCDEF99

  Same can be achived by selecting option s from the DemoAppSwErr menu and providing the required details

3. On FSI console, the reported error is displayed as below (prints from SafetyServicesDemoApp.c on FSI) -

DemoApp: ErrCode-0x12345678 ReptrId-0x8005 ErrAttr-0xabcdef99
This verifies error report from CCPLEX to FSI

4. On MCU, console reported critical failure report is displayed as below (prints from MCU FOH):

MCU_FOH: SOC error pin is asserted
MCU_FOH: ErrReport: ErrorCode-0x12345678 ReporterId-0x8005 Error_Attribute-0xabcdef99
This verifies SOC error pin assertion detection and failure notification on MCU.

5. On GOS console, CcplexApp receives the critical failure notification, and the reported critical failure report is displayed as below:

INFO: Received Messages Details:
ErrReport: ErrorCode-0x12345678 ReporterId-0x8005 Error_Attribute-0xabcdef99

Refer for more details fsi_demonstration section in PDK.
