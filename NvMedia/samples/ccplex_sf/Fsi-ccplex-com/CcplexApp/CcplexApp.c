/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto. Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file CcplexApp.c
 * @brief <b> Application on ccplex to receive critical failure information from FSI</b>
 */

/* ==================[Includes]============================================= */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <NvFsiCom.h>
#include <signal.h>
#include <errno.h>
#include <semaphore.h>
#include <SafetyServiceType.h>
/* =================[Macros]================================================= */
/**
 *Configured number of FsiCom channels for DemoApp
 */
#define DA_FSI_COM_CHANNEL 2U
/**
 *Number of frames transmitted in Periodic Tx
 */
#define DA_TX_FRAME_COUNT 10U
/**
 * Periodicity in ms at which Frames are transmitted in Periodic Tx
 */
#define DA_PERIODICITY_MS 1000U

/**
 * Handshake data for Error Report Forwarding
 */
#define FSIAPP_ER_ENABLE 0x44A5A544U
#define FSIAPP_ER_DISABLE 0xAA445A44U

#define MAXIMUM_PAYLOAD_LENGTH(x) ((x) - NVFSICOM_HEADER_LEN)

#ifdef __QNX__
#define CCPLEX_APP "nvidia,tegra-fsicom-qnx-CcplexApp"
#else
#define CCPLEX_APP "nvidia,tegra-fsicom-CcplexApp"
#endif

/* ==================[Declaration of static functions]======================= */

/**
 * @brief Handler for Term Thread
 *
 * - <b>Description</b>\n
 *   Term Handler
 *
 * @param arg (in)
 *
 * @return None
 *
 */

static void lTermination_SigHandler(int32_t sig_num);

/**
 * @brief Function to set up handler for SIGTERM
 *
 * @param None
 *
 * @return EOK if signal handler is set successfully, ENOK otherwise
 *
 */
static int32_t lSet_TerminationHandlers(void);

/**
 * @brief Handler for Rx Thread
 *
 * - <b>Description</b>\n
 *   Rx Handler
 *
 * @param arg (in)
 *
 * @return None
 *
 */
static void * lRxThread(void *arg);

/**
 * @brief Function to transmit frame
 *
 * - <b>Description</b>\n
 *   Transmit of frame over selected channel and wait for FSI response
 *
 * @param ChID (in) Channel ID for transmission
 * @param msgId (in) Message ID
 * @param msgId (in) Message ID
 * @param txBuff (in) Message Data
 *
 * @return lret
 *
 */
static int32_t lSendDataOverFsiCom(uint8_t ChID,
    uint16_t msgId,
    uint32_t msgLength,
    uint8_t*  txBuff);

/*
 * @brief Function to get user input for frame transmission
 *
 * - <b>Description</b>\n
 *   Gets user inputs for frame transmission
 *
 * @param chId (out) Channel ID for transmission
 * @param msgId (out) Message ID
 * @param msgId (out) Message Length
 * @param txBuff (out) Message Data
 *
 * @return lret Valid input status
 *
 */
static int32_t lGet_UserInputs(uint8_t* chId,
    uint32_t* msgId,
    uint32_t* msgLength,
    uint8_t*  txBuff);

/**
 * @brief Function to process user selection
 *
 * - <b>Description</b>\n
 *  process the user selected option
 *
 * @param opt Option selected by user
 *
 * @return void
 *
 */
static void lMenuHandling(char Opt);

/**
 * @brief Function to receive user inputs
 *
 * - <b>Description</b>\n
 *   defines the user menu option
 *
 * @param None
 *
 * @return None
 *
 */
static void lDemoApp_FsiTest(char *argv[]);

/*
 * @brief Function to print the message contents
 *
 * - <b>Description</b>\n
 *   Prints given message.
 *
 * @param msg (out) Message
 *
 * @return none.
 *
 */
static void lPrint_Msg(uint8_t* msg);

/**
 * @brief Function to display instructions for User
 *
 * - <b>Description</b>\n
 *   Prints description for each option
 *
 * @param None
 *
 * @return None
 *
 */
static void lPrint_Menu(void);

/**
 * @brief Function to handshake data to FSI
 *
 * - <b>Description</b>\n
 *   Function to handshake data to FSI
 *
 * @param pattern to enable/disable error report
 *        forwarding
 *
 * @return Handshake status
 *
 */
static inline int32_t lSendFsiAppNotification(uint32_t msg);

/* ==================[Global Variables]====================================== */
/**
 * Receiver thread for FsiCom
 */
static pthread_t sgRxThreadHandle;

/**
 * FsiCom Channel Handle
 */
static NvFsiComHandle_t sgFsiComHanlde[DA_FSI_COM_CHANNEL];

/**
 * semaphore for Termination of DemoApp
 */
static sem_t sgTermFlag;

/* ==================[Definition of static functions]======================== */



int32_t main(int argc, char *argv[])
{
  int32_t lRetVal = 0;
  int32_t lRet;
  bool lEnableNotifSent = false;

  sigset_t lSet;

  (void)sigemptyset(&lSet);
  (void)sigfillset(&lSet);

  lRet = sem_init(&sgTermFlag, 0, 0);
  if (0 != lRet)
  {
    (void)printf("semaphore initialization failed %d\n",lRet);
    lRetVal = -1;
  }

  if (lRetVal == 0)
  {
    /* Read configured application dt node for FSI communication */
    lRet = NvFsiComGetNodeInfo(&sgFsiComHanlde[0],
        DA_FSI_COM_CHANNEL,
        CCPLEX_APP);
    if (0 != lRet)
    {
      (void)printf("ERROR: Reading channel configuration for DemoAppTest failed %d\n",lRet);
      lRetVal = -1;
    }
  }

  if (lRetVal == 0)
  {
    /* FsiCom Channel Initialization */
    lRet = NvFsiComInit(&sgFsiComHanlde[0], DA_FSI_COM_CHANNEL);

    if (0 != lRet)
    {
      if (-EBUSY == lRet)
      {
        (void)printf("ERROR: DemoApp Failure: Relaunch  not supported\n");
      }
      else
      {
        (void)printf("ERROR: Initializing FsiCom Channels for Test failed\n");
      }

      lRetVal = -1;
    }
  }

  if (lRetVal == 0)
  {
    lRet = pthread_sigmask(SIG_BLOCK, &lSet, NULL);

    if (0 != lRet)
    {
      (void)printf("ERROR: pthread_sigmask Test failed\n");
      lRetVal = -1;
    }
  }

  if (lRetVal == 0)
  {
    /* Handler for Rx from FSI */
    lRet = pthread_create(&sgRxThreadHandle, NULL, lRxThread, NULL);
    if (0 != lRet)
    {
      (void)printf("ERROR: Failed to create Rx thread Test: %s\n",
          strerror(lRet));
      lRetVal = -1;
    }
  }

  if (lRetVal == 0)
  {
    lRet = lSet_TerminationHandlers();
    if (0 != lRet)
    {
      (void)printf("Failed to set Termination Handler: %s\n", strerror(lRet));
      lRetVal = -1;
    }
  }

  if (lRetVal == 0)
  {
    if (argc > 1)
    {
      lDemoApp_FsiTest(argv);
    }
    else
    {
      /* send notification to FsiApp for enabling error report forwarding */
      if (0 != lSendFsiAppNotification(FSIAPP_ER_ENABLE))
      {
        (void)printf(" sending notification to FsiApp failed\n");
        (void)sem_post(&sgTermFlag);
        lRetVal = -1;
      }
      else
      {
        (void)printf("waiting for critical failure notifications from FSI\r\n");
        lEnableNotifSent = true;
      }
    }

    /* wait for DemoApp Termination */
    lRet = sem_wait(&sgTermFlag);
    if (-1 == lRet)
    {
      /*return fail if not interrupted by handled signal */
      if ( errno != EINTR)
      {
        (void)printf("sem_wait failed: %s\n", strerror(errno));
        lRetVal = -1;
      }
    }
  }
  /* send notification to FsiApp for disabling error report forwarding */
  if ((lEnableNotifSent == true) &&
        (0 != lSendFsiAppNotification(FSIAPP_ER_DISABLE)))
  {
    (void)printf(" sending notification to FsiApp failed\n");
  }
  lRet = NvFsiComDeinit(&sgFsiComHanlde[0], DA_FSI_COM_CHANNEL);
  if(0 != lRet)
  {
    (void)printf(" DeInitializing FsiCom Channels failed\n");
    lRetVal = -1;
  }
  return lRetVal;
}

/**
 * @brief Handler for Term Thread
 *
 * - <b>Description</b>\n
 *   Term Handler
 *
 * @param arg (in)
 *
 * @return None
 *
 */

static void lTermination_SigHandler(int32_t sig_num)
{
  /* Signal handler has standard functioN PROtotype, suppress MISRA violation */
  (void)sig_num;
  (void)sem_post(&sgTermFlag);
}
/**
 * @brief Function to set up handler for SIGTERM
 *
 * @param None
 *
 * @return EOK if signal handler is set successfully, ENOK otherwise
 *
 */
static int32_t lSet_TerminationHandlers(void)
{
  struct sigaction lSigAction;
  sigset_t lSigSet;
  int32_t lReturn = -1;

  if (sigemptyset(&lSigSet) != 0)
  {
    (void)printf("sigemptyset() retured with error: %s", strerror(errno));
  }
  else
  {
     (void)sigaddset(&lSigSet, SIGINT);
     (void)sigaddset(&lSigSet, SIGTERM);
     (void)sigaddset(&lSigSet, SIGQUIT);
     (void)sigaddset(&lSigSet, SIGHUP);

    lSigAction.sa_flags = 0;
    lSigAction.sa_mask = lSigSet;
    lSigAction.sa_handler = &lTermination_SigHandler;

    /* update signal action */
    (void)sigaction(SIGTERM, &lSigAction, NULL);
    (void)sigaction(SIGINT, &lSigAction, NULL);
    (void)sigaction(SIGQUIT, &lSigAction, NULL);
    (void)sigaction(SIGHUP, &lSigAction, NULL);

    if (pthread_sigmask(SIG_UNBLOCK, &lSigSet, NULL) !=0)
    {
      (void)printf("Error in unblocking signal SIGTERM");
    }
    else
    {
      lReturn = 0;
    }
  }
  return lReturn;
}

static void * lRxThread(void *arg)
{
  int32_t lRet;
  uint8_t  lRxBuff[1024];
  uint32_t lReadSize;
  uint32_t lMsgLength;
  uint32_t lMsgId = 0U;

  NvFsiComChId_t lchId;

  while(true)
  {
    /* waiting for Receive event from Fsi-ccplex-com
       RECEIVE-blocked state until a message arrives */
    lRet = NvFsiComWaitForEvent(&lchId);

    if(lRet != 0)
    {
      if (lRet == -EINTR)
      {
        continue;
      }
      /* Exit from Rx thread */
      break;
    }
    while(true)
    {
        /* Read data frame from triggered FsiCom Channel */
        lRet = NvFsiComRead(&sgFsiComHanlde[lchId],
            (void*)lRxBuff,
            MAXIMUM_PAYLOAD_LENGTH(sgFsiComHanlde[lchId].IvcQueue.FrameSize),
            &lReadSize);

        /* Validate the FsiCom Read and display the received frame */
        if(lRet != 0)
        {
          /* No data queued for read */
          if(lRet == -ENODATA)
          {
            break;
          }
          (void)printf("ERROR: Test FsiComRead failed %d\n",lRet);
        }
        else
        {
          lMsgId |= ((uint32_t)lRxBuff[1U] << 8U);
          lMsgId |= lRxBuff[0U];
          (void)memcpy(&lMsgLength, &lRxBuff[2], 4);
          /* Validate the Message Length with channel frame size */
          if((sgFsiComHanlde[lchId].IvcQueue.FrameSize - 6) < lMsgLength)
          {
            (void)printf("ERROR: Invalid Message length received for Test\n");
          }
          else
          {
            (void)printf("INFO: Received Messages Details:\n");
            lPrint_Msg(lRxBuff);
            /* exit if received msg is not error frame */
            if( lMsgId != 0xCDABU)
            {
              /* signal main thread for termination */
              (void)sem_post(&sgTermFlag);
              break;
            }
          }
        }
    }
  }

  return NULL;
}

static int32_t lSendDataOverFsiCom(uint8_t ChID,
    uint16_t msgId,
    uint32_t msgLength,
    uint8_t*  txBuff)
{
  int32_t lRet;
  uint8_t lTxBuff[2048]={0};
  uint32_t sizewrite;

  /* Prepare the frame
     lTxBuff[0-1] message id
     lTxBuff[2-5] message length
     lTxBuff[6-msgLength] selected pattern */
  lTxBuff[0] = (uint8_t) msgId ;
  lTxBuff[1] = (uint8_t) (msgId >> 8);
  (void)memcpy(&lTxBuff[2], &msgLength, 4);

  (void)memcpy(&lTxBuff[6], txBuff, msgLength);

  (void)printf("INFO: Sending Message with Following Details to FSI:\n");
  lPrint_Msg(lTxBuff);

  /* Write to FsiCom Channel */
  lRet = NvFsiComWrite(&sgFsiComHanlde[ChID],
      (void*)lTxBuff,
      (msgLength + 6U),
      &sizewrite);
  /* Validate FsiCom write and display size written */
  if(lRet != 0)
  {
    (void)printf("ERROR: Write failed %d  %d  %d\n",lRet, sizewrite, ChID);
  }
  else
  {
  }

  return lRet;
}

static int32_t lGet_UserInputs(uint8_t* chId,
    uint32_t* msgId,
    uint32_t* msgLength,
    uint8_t*  txBuff)
{
  int32_t lRetVal = 0;
  int32_t lValidIp = 0;
  uint32_t lLoopCnt = 0U;

  /* DemoApp for FSI-CCPLEX generic communication supports only channel 0*/
  *chId = 1;
  /* Read msg id */
  (void)printf("Enter Message Id (0x0 - 0xFFFF) \r\n");
  lValidIp = scanf(" %x", msgId);
  (void)scanf("%*[^\n]%*c");

  /* validate message id */
  if(( 0xFFFFU < *msgId)||
      ( 1 != lValidIp))
  {
    (void)printf("ERROR: Invalid Message ID Entered \n");
    lRetVal = -1;
  }

  if (lRetVal == 0)
  {
    /* Read msg length */
    (void)printf("Enter Message Length (DemoApp Supports maximum 54 bytes) \r\n");
    lValidIp = scanf(" %u", msgLength);
    (void)scanf("%*[^\n]%*c");
    /* validate message length */
    if(((sgFsiComHanlde[*chId].IvcQueue.FrameSize - 6) < *msgLength)||
        (1 != lValidIp))
    {
      (void)printf("ERROR: Invalid Message Length Entered \n");
      lRetVal = -1;
    }
  }

  if (lRetVal == 0)
  {
    /* Read msg pattern */
    (void)printf("Enter Message Data byte by byte (Range: 0x00-0xFF):\r\n");
    for (lLoopCnt = 0U ; lLoopCnt < *msgLength ; lLoopCnt++)
    {
      lValidIp = scanf(" %hhx", &txBuff[lLoopCnt]);
      /* validate message pattern */
      if((1 != lValidIp))
      {
        (void)printf("ERROR: Invalid Message Data Entered \n");
        lRetVal = -1;
        break;
      }
    }
  }

  return lRetVal;
}

static void lSendStateChangeNotification(uint32_t State)
{
  uint8_t lchId = 0x0U;
  uint32_t lMsgId = 0xFADE;
  uint32_t lMsgLength = 0x4;
  uint8_t lTxBuff[2048] = {0};
  uint32_t sizewrite;
  int32_t lRet = 0;

  if (State == 3U)
  {
    (void)printf("Sending Suspend state notification to FSI\n");
  }
  else if (State == 5U)
  {
    (void)printf("Sending DeInit state notification to FSI\n");
  }
  else
  {
    (void)printf("Wrong state value\n");
    lRet = -1;
  }

  if (lRet == 0)
  {
    lTxBuff[0] = (uint8_t) (lMsgId & 0xFFU);
    lTxBuff[1] = (uint8_t) ((lMsgId >> 8) & 0xFFU);
    (void)memcpy(&lTxBuff[2], (uint8_t*)&lMsgLength, 4);

    (void)memcpy(&lTxBuff[6], (uint8_t*)&State, lMsgLength);

    /* Write to FsiCom Channel */
    lRet = NvFsiComWrite(&sgFsiComHanlde[lchId],
        (void*)lTxBuff,
        (lMsgLength + 6U),
        &sizewrite);
    /* Validate FsiCom write and display size written */
    if(lRet != 0)
    {
      (void)printf("ERROR: Write failed %d  %u  %u\n",lRet, sizewrite, lchId);
    }
    else
    {
    }
  }

  /* WAR: sleep should be removed after adding handshake with FSI */
  (void)sleep(1U);

  /* signal main thread for termination */
  (void)sem_post(&sgTermFlag);
  return;
}

static void lMenuHandling(char Opt)
{
  uint8_t lchId = 0xFF;
  uint32_t lMsgId = 0x0000;
  uint32_t lMsgLength = 0x00;
  uint8_t lTxBuff[2048] = {0};

  switch (Opt)
  {
    case( 'p' ):
      /* Tx of single frame */
      if(0 == lGet_UserInputs(&lchId,
            &lMsgId,
            &lMsgLength,
            &lTxBuff[0U]))
      {
        (void)lSendDataOverFsiCom(lchId,
            (uint16_t)lMsgId,
            lMsgLength,
            &lTxBuff[0U]);
      }
      else
      {
        (void)printf("ERROR: Invalid Entry!\r\n");
      }
      break;
    case( 'h' ):
      lPrint_Menu();
      break;
    case( 's' ):
      lSendStateChangeNotification(3U);
      break;
    case( 'd' ):
      lSendStateChangeNotification(5U);
      break;
    default:
      (void)printf("ERROR: Invalid input. Please try again\n");
      break;
  }

}

static void lDemoApp_FsiTest(char *argv[])
{
  char lOpt;

  lOpt = *argv[1];
  lMenuHandling(lOpt);

}

static void lPrint_Msg(uint8_t* msg)
{
  uint32_t lLoopCnt = 0U;
  uint32_t lMsgId = 0U, lMsgLength = 0U;
  SS_NvSehCriticalFailure_t *lpFailureReport;

  lMsgId |= ((uint32_t)msg[1U] << 8U);
  lMsgId |= msg[0U];
  (void)memcpy(&lMsgLength, &msg[2], 4);
  lpFailureReport =(SS_NvSehCriticalFailure_t *) &msg[6];

  if( lMsgId == 0xCDAB)
  {
    (void)printf("ErrReport: ErrorCode-0x%x ReporterId-0x%x Error_Attribute-0x%x Timestamp-0x%x  \n",
        lpFailureReport->ErrorReportFrame.ErrorCode, lpFailureReport->ErrorReportFrame.ReporterId,
        lpFailureReport->ErrorReportFrame.Error_Attribute, lpFailureReport->ErrorReportFrame.timestamp);
    (void)printf("SystemFailure Id-0x%x MaturationState-0x%x \n", lpFailureReport->SystemFailureId, lpFailureReport->MaturationState);
  }
  else
  {
    (void)printf("Message ID: 0x%x\n", lMsgId);
    (void)printf("Message Length: %d\n", lMsgLength);
    (void)printf("Message Data:\n");

    for (lLoopCnt = 0U ; lLoopCnt < lMsgLength ; lLoopCnt++)
    {
      (void)printf("0x%x\t", msg[(lLoopCnt + 6U)]);
    }
    (void)printf("\n");
  }
}
static void lPrint_Menu(void)
{
  (void)printf("\n   CcplexApp option:  \n");
  (void)printf("   _____________________________________________ \n\n");
  (void)printf("   CcplexApp h [Display help]\r\n");
  (void)printf("   CcplexApp p [Send a frame to FSI]\r\n");
  (void)printf("   CcplexApp s [Send suspend state change notification to FsiApp]\r\n");
  (void)printf("   CcplexApp d [Send de-init state change notification to FsiApp]\r\n");
}

static inline int32_t lSendFsiAppNotification(uint32_t msg)
{
  uint8_t lchId = 0x1U;
  uint32_t lMsgId = 0xFF0E;
  uint32_t lMsgLength = 0x4;
  uint8_t lTxBuff[2048] = {0};
  uint32_t sizewrite;
  int32_t lRet = 0;

  lTxBuff[0] = (uint8_t) (lMsgId & 0xFFU);
  lTxBuff[1] = (uint8_t) ((lMsgId >> 8) & 0xFFU);
  (void)memcpy(&lTxBuff[2], (uint8_t*)&lMsgLength, 4);

  (void)memcpy(&lTxBuff[6], (uint8_t*)&msg, lMsgLength);

  /* Write to FsiCom Channel */
  lRet = NvFsiComWrite(&sgFsiComHanlde[lchId],
      (void*)lTxBuff,
      (lMsgLength + 6U),
      &sizewrite);
  if(lRet != 0)
  {
    (void)printf("ERROR: Write failed %d  %u  %u\n",lRet, sizewrite, lchId);
  }

  return lRet;
}
