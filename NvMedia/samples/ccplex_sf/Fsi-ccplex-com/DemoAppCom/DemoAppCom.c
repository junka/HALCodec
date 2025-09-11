/*
 * Copyright (c) 2021-23, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto. Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file DemoAppCom.c
 * @brief <b> Demo application to demonstrate API usage of FSI-CCPLEX generic
 * communicaton on ccplex</b>
 *
 * This file will demonstrate FSI-CCPLEX COM library APIs which are restricted
 * to be used by DriveOS user
 */

/* ==================[Includes]============================================= */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <NvFsiCom.h>
#include <signal.h>
#include <errno.h>
#include <semaphore.h>
/* =================[Macros]================================================= */
/**
 *Configured number of FsiCom channels for DemoApp
 */
#define DA_FSI_COM_CHANNEL 1U
/**
 *Number of frames transmitted in Periodic Tx
 */
#define DA_TX_FRAME_COUNT 10U
/**
 * Periodicity in ms at which Frames are transmitted in Periodic Tx
 */
#define DA_PERIODICITY_MS 999U

#define MAXIMUM_PAYLOAD_LENGTH(x) ((x) - NVFSICOM_HEADER_LEN)

#ifdef __QNX__
#define COMPATIBLE_SAMPLE_APP "nvidia,tegra-fsicom-qnx-sampleApp1"
#else
#define COMPATIBLE_SAMPLE_APP "nvidia,tegra-fsicom-sampleApp1"
#endif

/*==================[static variables]========================================*/
/**
 * semaphore for Termination of DemoApp
 */
static sem_t sgTermFlag;

/**
 * Receiver thread for DemoApp
 */
static pthread_t sgRxThreadHandle;
/**
 * Transmitter thread for DemoApp
 */
static pthread_t sgTxThreadHandle;

/**
 * FsiCom Channel Handle
 */
static NvFsiComHandle_t sgFsiComHandle[DA_FSI_COM_CHANNEL];

/* ==================[Definition of static functions]======================== */

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
   sem_post(&sgTermFlag);
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
      printf("sigemptyset() retured with error: %s", strerror(errno));
    }
    else
    {
      sigaddset(&lSigSet, SIGINT);
      sigaddset(&lSigSet, SIGTERM);
      sigaddset(&lSigSet, SIGQUIT);
      sigaddset(&lSigSet, SIGHUP);

      lSigAction.sa_flags = 0;
      lSigAction.sa_mask = lSigSet;
      lSigAction.sa_handler = &lTermination_SigHandler;

      /* update signal action */
      sigaction(SIGTERM, &lSigAction, NULL);
      sigaction(SIGINT, &lSigAction, NULL);
      sigaction(SIGQUIT, &lSigAction, NULL);
      sigaction(SIGHUP, &lSigAction, NULL);

      if (pthread_sigmask(SIG_UNBLOCK, &lSigSet, NULL) !=0)
      {
        printf("Error in unblocking signal SIGTERM");
      }
      else
      {
        lReturn = 0;
      }
    }
    return lReturn;
}

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
static void * lRxThread(void *arg)
{
  int32_t lRet;
  uint8_t  lRxBuff[1024];
  uint32_t lReadSize;
  uint32_t lMessageLength;
  uint8_t* lMsgData;
  NvFsiComChId_t lChId;

  while(1)
  {
     /* waiting for Receive event from Fsi-ccplex-com
       RECEIVE-blocked state until a message arrives */
    lRet = NvFsiComWaitForEvent(&lChId);
    printf("NvFsiComWaitForEvent received for channel_%d\n",lChId);

    if(lRet != 0)
    {
      if (lRet == -EINTR)
      {
        continue;
      }
      if (lRet == -ECANCELED)
      {
        break;
      }
      printf("NvFsiComWaitForEvent failed %x\n",lRet);
      break;
    }
    while(1)
    {
      /* Read data frame from triggered FsiCom Channel */
      lRet = NvFsiComRead(&sgFsiComHandle[lChId],
                          (void*)lRxBuff,
                          MAXIMUM_PAYLOAD_LENGTH(sgFsiComHandle[lChId].IvcQueue.FrameSize),
                          &lReadSize);
      /* Validate the FsiCom Read and display the received frame */
      if(lRet != 0)
      {
          /* No data queued for read */
          if(lRet == -ENODATA)
          {
            break;
          }
         printf("Read failed %d\n",lRet);
      }
      else
      {
         memcpy(&lMessageLength, &lRxBuff[2], 4);
         lMsgData = &lRxBuff[6];
         /* Validate the Message Length with channel frame size */
         if(((uint64_t)lMessageLength + 6) >sgFsiComHandle[lChId].IvcQueue.FrameSize)
         {
            printf("Invalid Message length received\n");
         }
         else
         {
            printf("Received Frame from FSI on Channel_%d\n",lChId);
            printf("Message Id : 0x%02X%02X \n",lRxBuff[1], lRxBuff[0]);
            printf("Message Length : %u\n", lMessageLength);
            printf("Message Data :");
            for(uint32_t i = 0 ; i < lMessageLength ; i++)
            {
               if(!(i % 16))
               {
                  printf("\n");
               }
               printf("0x%02X ",lMsgData[i]);
            }
            printf("\n");
            /* clear the receive buffer  */
            memset(lRxBuff, 0xFF, sizeof(lRxBuff));
         }
      }
    }
  }
  return NULL;
}

/**
 * @brief Function to transmit frame
 *
 * - <b>Description</b>\n
 *   Transmit of frame over selected channel and wait for FSI response
 *
 * @param ChID (in) Channel ID for transmission
 * @param MsgId (in) Message ID
 * @param MsgLenght (in) Message Length
 * @param FillPattern (in) Pattern to fill the message
 *
 * @return lret
 *
 */
static int32_t lSendDataOverFsiCom(uint8_t ChID, uint16_t MsgId,
                                  uint32_t MsgLength, uint8_t FillPattern)
{
    int32_t lRet;
    uint8_t lTxBuff[2048]={0};
    uint32_t sizewrite;

    /* Prepare the frame
       lTxBuff[0-1] message id
       lTxBuff[2-5] message length
       lTxBuff[6-MsgLength] selected pattern */
    lTxBuff[0] = (uint8_t) MsgId ;
    lTxBuff[1] = (uint8_t) (MsgId >> 8);
    memcpy(&lTxBuff[2], &MsgLength, 4);
    memset(&lTxBuff[6], FillPattern, MsgLength);

    /* Write to FsiCom Channel */
    lRet = NvFsiComWrite(&sgFsiComHandle[ChID],
              (void*)lTxBuff, MsgLength+6, &sizewrite);
    /* Validate FsiCom write and display size written */
    if(lRet != 0)
    {
      printf("Write failed %d\n",lRet);
    }
    else
    {
      printf("written %d size on channel_%d\n",sizewrite, ChID);
    }

    return lRet;
}
/**
 * @brief Function to periodic transmit frame
 *
 * - <b>Description</b>\n
 *   Periodic Transmit of frame over selected channel
 *
 * @param ChID (in) Channel ID for transmission
 * @param MsgId (in) Message ID
 * @param MsgLength (in) Message Length
 * @param FillPattern (in) Pattern to fill the message
 *
 * @return lret
 *
 */
static int32_t lPeriodic_Communication(uint8_t ChID, uint16_t MsgId,
                                     uint32_t MsgLength, uint8_t FillPattern)
{
    int32_t lRet;

    printf("Sending %d Frames on Channel_%d with periodicity of %dms \r\n",
                                DA_TX_FRAME_COUNT, ChID, DA_PERIODICITY_MS );
    for(unsigned int i = 0; i < DA_TX_FRAME_COUNT; i++)
    {
       lRet = lSendDataOverFsiCom(ChID, MsgId, MsgLength, FillPattern);
       usleep(DA_PERIODICITY_MS * 1000);
    }

    return lRet;
}
#ifdef MULTI_CHANNEL_SUPPORT
/**
 * @brief Function to display Channel details
 *
 * - <b>Description</b>\n
 *   List configured FsiCom Channel details
 *
 * @param None
 *
 * @return None
 *
 */
static void lListChannelInfo(void)
{
  printf("DriveOS_FsiComDemoApp FsiCom Channels Initialized \r\n");

  for(uint8_t i=0; i < DA_FSI_COM_CHANNEL; i++)
  {
    printf("Name: Channel_%d No of Frames: %d FrameSize: %d\n",
                      sgFsiComHandle[i].ChId,
                      sgFsiComHandle[i].IvcQueue.nFrames,
                      sgFsiComHandle[i].IvcQueue.FrameSize);
  }
}
#endif

/**
 * @brief Function to get user input for frame transmission
 *
 * - <b>Description</b>\n
 *   Gets user inputs for frame transmission
 *
 * @param ChId (out) Channel ID for transmission
 * @param MsgId (out) Message ID
 * @param MsgId (out) Message Length
 * @param FillPattern (out) Pattern to fill the message
 *
 * @return lret Valid input status
 *
 */
static int32_t lGet_UserInputs(uint8_t* ChId, uint32_t* MsgId,
                              uint32_t* MsgLength,  uint8_t* FillPattern)
{
   int32_t lret = -1;
   int lValidIp;
#ifdef MULTI_CHANNEL_SUPPORT
   /* List initialized FsiCom Channels */
   lListChannelInfo();

   printf("Select Channel \r\n");
   lValidIp = scanf(" %hhu", ChId);
   scanf("%*[^\n]%*c");
   /* Validate the channel selection */
   if(( DA_FSI_COM_CHANNEL <= *ChId)||
      ( 1 != lValidIp ))
   {
     return lret;
   }
#endif
   /* DemoApp for FSI-CCPLEX generic communication supports only channel 0*/
   *ChId = 0;
   /* Read msg id */
   printf("Enter Message Id (0x0 - 0xFFFF) \r\n");
   lValidIp = scanf(" %x", MsgId);
   scanf("%*[^\n]%*c");
   /* validate message id */
   if(( 0xFFFFU < *MsgId)||
     ( 1 != lValidIp))
   {
     return lret;
   }
   /* Read msg length */
   printf("Enter Message Length (DemoApp Supports maximum 54 bytes) \r\n");
   lValidIp = scanf(" %u", MsgLength);
   scanf("%*[^\n]%*c");
   /* validate message length */
   if(((uint64_t)*MsgLength + 6 > sgFsiComHandle[*ChId].IvcQueue.FrameSize)||
        (1 != lValidIp))
   {
     return lret;
   }
   /* Read msg pattern */
   printf("select single byte pattern to fill the message (0x00-0xFF)\r\n");
   lValidIp = scanf(" %hhx", FillPattern);
   scanf("%*[^\n]%*c");
   /* validate message pattern */
   if((1 == lValidIp))
   {
     lret = 0;
   }
   return lret;
}

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
static void lPrint_Menu(void)
{
  printf("\n   DriveOS_FsiComDemoApp main menu  \n");
  printf("   _____________________________________________ \n\n");
  printf("   [m] Display the menu\r\n");
  printf("   [s] Send a frame to FSI\r\n");
  printf("   [p] Periodically send data to FSI\r\n");
  printf("   [q] Terminate the app\n\n");
}

/**
 * @brief Function to process user selection
 *
 * - <b>Description</b>\n
 *  process the user selected option
 *
 * @param opt Option selected by user
 *
 * @return None
 *
 */
static void lMenuHandling(char Opt)
{
  uint8_t lFillPattern = 0xAA;
  uint8_t lChId = 0xFF;
  uint32_t lMsgId = 0x0000;
  uint32_t lMsgLength = 0x00;

  switch (Opt)
  {
    case( 'p' ):
      /* Periodic Tx of DA_TX_FRAME_COUNT frames  */
      if(!(lGet_UserInputs(&lChId, &lMsgId, &lMsgLength,
                                         &lFillPattern)))
      {
        (void)lPeriodic_Communication(lChId, lMsgId,
                               lMsgLength, lFillPattern);
      }
      else
      {
        printf(" Invalid Entry!\r\n");
      }
      break;
    case( 's' ):
      /* Tx of single frame */
      if(!(lGet_UserInputs(&lChId, &lMsgId, &lMsgLength,
                                         &lFillPattern)))
      {
        (void)lSendDataOverFsiCom(lChId, lMsgId,
                                 lMsgLength, lFillPattern);
      }
      else
      {
        printf(" Invalid Entry!\r\n");
      }
      break;
    case( 'q' ):
      sem_post(&sgTermFlag);
      break;
    case( 'm' ):
      break;
    default:
      printf("Invalid input. Please try again\n");
      break;
  }
}

/**
 * @brief Handler for Tx Thread
 *
 * - <b>Description</b>\n
 *   Tx Handler
 *
 * @param arg (in)
 *
 * @return None
 *
 */
static void * lTxThread(void* arg)
{
  char lOpt;

  while(1)
  {
    lPrint_Menu();
    scanf(" %c", &lOpt);
    scanf("%*[^\n]%*c");
    lMenuHandling(lOpt);
    if ('q' == lOpt)
    {
      break;
    }
  }
  return NULL;
}

int32_t main(int argc, char *argv[])
{
  int32_t lRet;

  sigset_t lSet;

  (void)sigemptyset(&lSet);
  (void)sigfillset(&lSet);

  lRet = sem_init(&sgTermFlag, 0, 0);
  if (0 != lRet)
  {
    printf("semaphore initialization failed %d\n",lRet);
    return -1;
  }
  /* Read configured application dt node for FSI communication */
  lRet = NvFsiComGetNodeInfo(&sgFsiComHandle[0],
                             DA_FSI_COM_CHANNEL,
                             COMPATIBLE_SAMPLE_APP);
  if (0 != lRet)
  {
    printf("Reading channel configuration failed %d\n",lRet);
    return -1;
  }
  /* FsiCom Channel Initialization */
  lRet = NvFsiComInit(&sgFsiComHandle[0], DA_FSI_COM_CHANNEL);

  if (0 != lRet)
  {
    if (-EBUSY == lRet)
    {
      printf(" DemoApp Init: Fsi channel reconnect failed\n");
    }
    else
    {
      printf("Initializing FsiCom Channels failed\n");
    }
    return -1;
  }

  lRet = pthread_sigmask(SIG_BLOCK, &lSet, NULL);
  if (0 != lRet)
  {
    printf("pthread_sigmask failed\n");
    return -1;
  }

  /* Handler for Rx from FSI */
  lRet = pthread_create(&sgRxThreadHandle, NULL, lRxThread, NULL);
  if (0 != lRet)
  {
    printf("Failed to create Rx thread: %s\n", strerror(lRet));
    return -1;
  }
  /* Handler for Tx to FSI */
  lRet = pthread_create(&sgTxThreadHandle, NULL, lTxThread, NULL);
  if (0 != lRet)
  {
    printf("Failed to create Tx thread: %s\n", strerror(lRet));
    return -1;
  }
  lRet = lSet_TerminationHandlers();
  if (0 != lRet)
  {
    printf("Failed to set Termination Handler: %s\n", strerror(lRet));
    return -1;
  }
  /* wait for DemoApp Termination */
  lRet = sem_wait(&sgTermFlag);
  if (-1 == lRet)
  {
    /*return fail if not interrupted by handled signal */
    if ( errno != EINTR)
    {
      printf("sem_wait failed: %s\n", strerror(errno));
      return -1;
    }
  }
  lRet = NvFsiComDeinit(&sgFsiComHandle[0], DA_FSI_COM_CHANNEL);
  if(0 != lRet)
  {
      printf(" DeInitializing FsiCom Channels failed\n");
  }

  pthread_join(sgTxThreadHandle, (void*)&lRet);
  pthread_join(sgRxThreadHandle, (void*)&lRet);
  return 0;
}
