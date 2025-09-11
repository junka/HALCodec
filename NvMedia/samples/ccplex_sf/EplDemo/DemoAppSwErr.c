/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto. Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file DemoAppiSwErr.c
 * @brief <b> Demo application to demonstrate API usage
 *            of Safety Services on ccplex</b>
 *
 * This file will demonstrate usage of APIs of Error Propagation library, which
 * are restricted to be used by DriveOS user
 */

/* ==================[Includes]============================================= */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <NvEpl.h>
#include <inttypes.h>
#include <errno.h>

#if __QNX__
#include <sys/neutrino.h>
#endif
/* =================[Macros]================================================= */

#define REPORT_INTERVAL (10U)

/* =================[Typedefs]================================================= */

/* Typedef for multierror reports with perodicity */
typedef struct sehInputError
{
   SS_ErrorReportFrame_t lErrReport ;
   uint32_t interval;  // in ms
   uint32_t startTime; // in ms, default: 0
   uint32_t endTime;   // in ms, default: 0
} sehInputError_t;

/* ==================[Definition of static functions]======================== */

/**
 * @brief Function to get timestamp
 *
 * - <b>Description</b>\n
 *      Reads 32 bit LSB CCPLEX TSC counter
 *
 * @return uint32_t
 */
static uint32_t lGetTimestamp(void)
{
#ifdef __QNX__
    return ClockCycles();
#else
    uint32_t lTimestamp;
    asm volatile("mrs %0, cntvct_el0" : "=r" (lTimestamp));
    return lTimestamp;
#endif
}



/**
 * @brief Function to get software error details
 *
 * - <b>Description</b>\n
 *    Reads the software error id and Attribute from user
 *    and reports to Fsi
 *
 * @param None
 *
 * @return None
 *
 */
static void lReportSWError(void)
{
   SS_ReturnType_t lRet ;
   SS_ErrorReportFrame_t lErrReport ;

   printf("Enter the Software Error Reporter ID (0x0000 - 0xFFFF) \n");
   scanf(" %hX",&lErrReport.ReporterId);
   scanf("%*[^\n]%*c");
   printf("Enter the Software Error Code (0x00000000 - 0xFFFFFFFF) \n");
   scanf(" %X",&lErrReport.ErrorCode);
   scanf("%*[^\n]%*c");
   printf("Enter the Software Error Attribute (0x00000000 - 0xFFFFFFFF)\n");
   scanf(" %X",&lErrReport.Error_Attribute);
   scanf("%*[^\n]%*c");

   lErrReport.timestamp = lGetTimestamp();

   lRet = NvEpl_ReportError(lErrReport);

   if( SS_E_OK == lRet )
   {
     printf("Reported SW Error \r\n");
     printf("Software Error ReporterId : 0x%04X\n", lErrReport.ReporterId);
     printf("Software Errror ErrorCode : 0x%04X\n", lErrReport.ErrorCode);
     printf("Software Error Attribute   : 0x%08X\n", lErrReport.Error_Attribute);
   }
   else
   {
     printf("Error Reporting failed \n");
   }
}

#if __QNX__
static void lReportMiscEcError(void)
{
    SS_ReturnType_t lRet ;
    uint8_t lMiscECNum = 0U;
    uint32_t lErrorCode = 0U;
    bool lErrStatus = false;

    printf("Enter the Generic SW Err number (0 to 4) to be used:\n");
    scanf(" %hhX",&lMiscECNum);
    scanf("%*[^\n]%*c");
    printf("Enter the error code to be passed along with generic sw error \n");
    scanf(" %X",&lErrorCode);
    scanf("%*[^\n]%*c");

    lRet = NvEpl_GetMiscEcErrStatus(lMiscECNum, &lErrStatus);
    if ((SS_E_OK == lRet) && (lErrStatus == true))
    {
        lRet = NvEpl_Report_MISC_EC_Error(lMiscECNum, lErrorCode);
        if( SS_E_OK == lRet )
        {
            printf("Reported SW generic error #%d with error code 0x%x \r\n", lMiscECNum, lErrorCode);
        }
        else
        {
            printf("Error Reporting failed \n");
        }
    }
    else
    {
        printf("Error Reporting failed becuase of error status\n");
    }
}
#endif

/**
 * @brief Function to display instructions for User SW
 *
 * - <b>Description</b>\n
 *   Prints description for each option
 *
 * @param None
 *
 * @return None
 *
 */
static inline void lPrint_Menu(void)
{
    printf("\n   DriveOS_ErrPropagationDemoApp main menu  \n");
    printf("   _____________________________________________ \n\n");
    printf("   [m] Display the menu\r\n");
    printf("   [s] Report Software Error\r\n");
#if __QNX__
    printf("   [e] Report Misc EC Generic SW Error\r\n");
#endif
    printf("   [q] Terminate the app\n\n");
}

/**
 * @brief Function to handle user option
 *
 * @param opt(in) User selected option
 *
 * @return lTerm Termination status
 *
 */
static bool lMenuHandling(char Opt)
{
    bool lTerm = false;

    switch (Opt)
    {
        case( 's' ):
            lReportSWError();
            break;
#if __QNX__
        case( 'e' ):
            lReportMiscEcError();
            break;
#endif
        case( 'q' ):
            lTerm = true;
            break;
        case( 'm' ):
            break;
        default:
            printf("Invalid input. Please try again\n");
            break;
    }

    return lTerm;
}

/**
 * @brief Function to receive user inputs
 *
 * @param None
 *
 * @return None
 *
 */

static void lDemoApp_ErrPropagation(void)
{
    char lOpt;
    bool lTerm = false;
    while(1)
    {
       lPrint_Menu();
       scanf(" %c", &lOpt);
       scanf("%*[^\n]%*c");
       lTerm = lMenuHandling(lOpt);

       if (lTerm == true)
       {
         break;
       }
    }
}

/**
 * @brief Function to Report Software error
 *
 * - <b>Description</b>\n
 *   Report error passed by command-line argument
 *
 * @param argc command-line argument count
 * @param argv list of command-line arguments
 *
 * @return None
 *
 */

static void lProcessCmdLineErrorReport(char *argv[])
{
   SS_ReturnType_t lRet ;
   SS_ErrorReportFrame_t lErrReport ;

   lErrReport.ReporterId = strtoul(argv[1], NULL, 16);
   lErrReport.ErrorCode = strtoul(argv[2], NULL, 16);
   lErrReport.Error_Attribute = strtoul(argv[3], NULL, 16);
   lErrReport.timestamp = lGetTimestamp();

   lRet = NvEpl_ReportError(lErrReport);

   if( SS_E_OK == lRet )
   {
     printf("Reported SW Error \r\n");
     printf("Software Error ReporterId : 0x%04X\n", lErrReport.ReporterId);
     printf("Software Errror ErrorCode : 0x%04X\n", lErrReport.ErrorCode);
     printf("Software Error Attribute   : 0x%08X\n", lErrReport.Error_Attribute);
   }
   else
   {
     printf("Error Reporting failed \n");
   }
}


/**
 * @brief Function to suspend execution for cfg millisec
 *
 * - <b>Description</b>\n
 *   suspends the execution of calling thread
 * @param msec time in millisec
 *
 * @return on successful sleep returns 0 otherwise -1
 *
 */
static inline int32_t lmsSleep(uint32_t msec)
{
    struct timespec ts;
    int32_t lRet;

    if (msec < 0)
    {
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

   do
   {
      lRet = nanosleep(&ts, &ts);
   } while (lRet && errno == EINTR);

    return lRet;
}

/**
 * @brief Function to check Report Error
 *
 * - <b>Description</b>\n
 *   checks whether send the error in current iteration.
 * @param file config file
 *
 * @return None
 *
 */

static inline bool lErrorReportShouldSend(sehInputError_t input, uint32_t counter, bool* runflag )
{
  /* set run flag if endTime is not reached */
  if(!(*runflag))
  {
    if (counter * REPORT_INTERVAL <= input.endTime)
    {
      *runflag  = true;
    }
  }

  // check counter * REPORT_INTERVAL is within (startTime, endTime + REPORT_INTERVAL)
  if (counter * REPORT_INTERVAL < input.startTime)
  {
      return false;
  }
  if (counter * REPORT_INTERVAL >= input.endTime + REPORT_INTERVAL)
  {
      return false;
  }
  if ((counter * REPORT_INTERVAL - input.startTime) % input.interval < REPORT_INTERVAL)
  {
      return true;
  }
  return false;
}
/**
 * @brief Function to Report Software error
 *
 * - <b>Description</b>\n
 *   Report error passed by a config file
 *   Config file should have below details configured for each error
 *   reportId(hex, starting with '0x')
 *   errorId(hex, starting with '0x')
 *   interval(dec, unit: ms)
 *   startTime(dec, unit: ms)
 *   endTime(dec, unit: ms)
 *   eg:
 *   # error 1
 *   0x1000 0x12345678 60 10000 20000
 *   # error 2
 *   0x2000 0x22222222 70 20000 30000
 *   # error 3
 *   0x3000 0x33333333 33 15000 25000
 *
 *  @param file config filepath
 *
 * @return None
 *
 */

static void lProcessErrorReportCfgfle(char *file)
{
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    uint32_t total_read;
    sehInputError_t error = {0};
    int32_t lRet = -1;
    uint32_t lCounter =0;
    bool lRunFlag = true;

    fp = fopen(file, "r");
    if (fp == NULL)
    {
      printf("DemoAppErr: Error cfg file not found\n");
      return;
    }

  while(lRunFlag)
  {
    lRunFlag = false;
    while ((read = getline(&line, &len, fp)) != -1)
    {
      total_read = 0;
      /* skip if line is commented */
      if( '#' == line[0])
      {
        continue;
      }

        total_read = sscanf(line,"%hx %x %d %d %d",
                          &error.lErrReport.ReporterId,
                          &error.lErrReport.ErrorCode,
                          &error.interval,
                          &error.startTime,
                          &error.endTime);
        if( total_read != 5 )
        {
          printf( "DemoAppErr: Error in configuration file: missing parameter \n");
          printf("%s\n", line);
          return;
        }
        if (lErrorReportShouldSend(error, lCounter, &lRunFlag))
        {
          error.lErrReport.timestamp = lGetTimestamp();
          lRet = NvEpl_ReportError(error.lErrReport);

          if( SS_E_OK == lRet )
          {
            printf("Reported SW Error \r\n");
            printf("Software Error ReporterId : 0x%04X\n", error.lErrReport.ReporterId);
            printf("Software Errror ErrorCode : 0x%04X\n", error.lErrReport.ErrorCode);
          }
          else
          {
            printf("DemoAppErr: Error Reporting failed \n");
          }
        }
    }

    lCounter ++;
    if(lmsSleep(REPORT_INTERVAL) !=0)
    {
      printf("DemoAppErr: Error while waiting for next iternation \n");
      return;
    }
    if ( fseek(fp, 0L, SEEK_SET) != 0 )
    {
       printf("DemoAppErr: Failed to reset file ptr\n");
       return;
    }

  }
    fclose(fp);
    if (line)
        free(line);


}

int32_t main(int argc, char *argv[])
{
  /*Init Epl*/
  if(SS_E_OK != NvEplInit())
  {
    printf(" EPL init failed \n");
    return -1;
  }

  if(argc == 4)
  {
     lProcessCmdLineErrorReport(argv);
  }
  else if(argc > 1)
  {
    if((strcmp("-c",argv[1]) == 0) && (argv[2] != NULL))
    {
        lProcessErrorReportCfgfle(argv[2]);
    }
    else
    {
      printf("DemoAppErr: Invalid arguments \n");
    }
  }
  else
  {
     lDemoApp_ErrPropagation();
  }
  return 0;
}
