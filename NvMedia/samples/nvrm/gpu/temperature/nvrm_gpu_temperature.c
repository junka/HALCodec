/*
 * Copyright (c) 2020 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include <stdbool.h>
#include <libgen.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <inttypes.h>

#include "nvrm_gpu.h"
#include "nvrm_init.h"

typedef enum
{
    InputFlags_Celsius,
    InputFlags_Fahrenheit,
    InputFlags_Kelvin,
    InputFlags_Interval,
    InputFlags_Repeat,
    InputFlags_Count
} InputFlags;

typedef enum
{
    DeviceState_DeviceFound,
    DeviceState_DeviceNotFound,
    DeviceState_Closed
} DeviceState;

static struct option longOptions[] =
{
    {"list",        no_argument,       NULL, 'l'},
    {"celsius",     no_argument,       NULL, 'c'},
    {"fahrenheit",  no_argument,       NULL, 'f'},
    {"kelvin",      no_argument,       NULL, 'k'},
    {"interval",    optional_argument, NULL, 'i'},
    {"iterate",     optional_argument, NULL, 'r'},
    {NULL,          0,                 NULL,  0}
};

static const char *deviceStateToStr(NvRmGpuLibDeviceState state)
{
    switch (state)
    {
    case NvRmGpuLibDeviceState_Attached:
        return "attached";
    case NvRmGpuLibDeviceState_InsufficientPrivileges:
        return "access-denied";
    case NvRmGpuLibDeviceState_Unknown:
        return "unknown";
    default:
        return "???";
    }
}

static const char *getTemperatureSensorName(NvRmGpuDeviceTemperature sensor)
{
    switch (sensor)
    {
    case NvRmGpuDeviceTemperature_InternalSensor:
        return "Internal";
    default:
        return "???";
    }
}

static void gpuTempInfo(const NvRmGpuDeviceTemperature *pTemperatureSensors, size_t numSensors,
                NvRmGpuDevice *device, bool *temperatureFlag)
{
    int32_t temperature;
    size_t i;

    for (i = 0; i < numSensors; i++)
    {
        NvError err = NvRmGpuDeviceGetTemperature(device, pTemperatureSensors[i], &temperature);
        if (err)
        {
            printf("*** NvRmGpuDeviceGetTemperature(%u) failed, error %d\n ",
                   pTemperatureSensors[i], err);
        }
        else
        {
            if (temperatureFlag[InputFlags_Celsius] == true)
                printf("- %5s temperature : %" PRId32 " mC (%.3f C)\n",
                       getTemperatureSensorName(pTemperatureSensors[i]),
                       temperature, (float)temperature / 1000);
            if (temperatureFlag[InputFlags_Fahrenheit] == true)
                printf("- %5s temperature : %.0f mF (%.3f F)\n",
                       getTemperatureSensorName(pTemperatureSensors[i]),
                       (float)((temperature * 1.80) + 32000),
                       (float)((temperature * 1.80) + 32000) / 1000);
            if (temperatureFlag[InputFlags_Kelvin] == true)
                printf("- %5s temperature : %" PRId32 " mK (%.3f K)\n",
                       getTemperatureSensorName(pTemperatureSensors[i]),
                       temperature + 273150, (float)(temperature + 273150) / 1000);
        }
    }
}

static void listGpu(void)
{
    const NvRmGpuLibDeviceListEntry *gpuList;
    size_t gpuListSize = 0;
    NvRmGpuLib *lib = NvRmGpuLibOpen(NULL);
    printf("GPU list:\n");
    gpuList = NvRmGpuLibListDevices(lib, &gpuListSize);
    if (!gpuListSize)
    {
        printf("<no devices>\n");
    }
    else
    {
        size_t i;
        printf("\n");
        printf("gpu-index   state           probe name\n");
        printf("======================================================\n");

        for (i = 0; i < gpuListSize; ++i)
        {
            printf("%9d   %-13s   %s\n",
                   gpuList[i].deviceIndex,
                   deviceStateToStr(gpuList[i].deviceState),
                   gpuList[i].name);
        }
    }
    NvRmGpuLibClose(lib);
}

static int gpuTempSensorInfo(int deviceIndex, uint32_t repetition,
            uint32_t intervalMs, bool *temperatureFlag)
{
    size_t i, gpuListSize = 0;
    bool deviceFlag = false;
    struct timespec intervalSleep;
    intervalSleep.tv_sec = intervalMs / 1000;
    intervalSleep.tv_nsec = (intervalMs % 1000) * 1000000L;
    const NvRmGpuLibDeviceListEntry *gpuList;
    NvRmGpuLib *lib = NvRmGpuLibOpen(NULL);
    NvRmGpuDevice *device = NULL;

    if (NULL == lib)
    {
        printf("error opening NvRmGpuLibOpen\n");
        return DeviceState_Closed;
    }
    gpuList = NvRmGpuLibListDevices(lib, &gpuListSize);

    if (!gpuListSize)
    {
        printf("<no GPU detected>\n");
        goto fail;
    }
    for (i = 0; i < gpuListSize; i++)
    {
        if (gpuList[i].deviceIndex != deviceIndex)
        continue;
        deviceFlag = true;
        NvError err = NvRmGpuDeviceOpen(lib, deviceIndex, NULL, &device);
        if (err)
        {
            printf("error opening device\n");
            goto fail;
        }
        const NvRmGpuDeviceTemperature *pTemperatureSensors;
        size_t numSensors;
        err = NvRmGpuDeviceListTemperatureSensors(device,
                        &pTemperatureSensors, &numSensors);
        if (err)
        {
            printf("*** NvRmGpuDeviceListTemperatureSensors failed: %d\n", err);
            goto fail;
        }
        if (numSensors == 0)
        {
            printf("- no temperature sensor available\n");
            goto fail;
        }

        for (uint32_t k = 1; k <= repetition; k++)
        {
            gpuTempInfo(pTemperatureSensors, numSensors, device, temperatureFlag);
            if (k != repetition)
            {
                printf("\n");
                nanosleep(&intervalSleep, NULL);
            }
        }
        NvRmGpuDeviceClose(device);
        break;
    }
    if (deviceFlag == true)
    {
        return DeviceState_DeviceFound;
    }
    else
    {
        printf("incorrect gpu index : '%d'\n", deviceIndex);
        return DeviceState_DeviceNotFound;
    }
    NvRmGpuLibClose(lib);
    return DeviceState_Closed;

fail:
    if(device)
    {
        NvRmGpuDeviceClose(device);
    }
    if(lib)
    {
        NvRmGpuLibClose(lib);
    }
    return DeviceState_Closed;
}

static void listUsage(char *cmd)
{
    printf("Usage: %s <gpu-index> [-list] [-cfk] [-i=interval] [-r=iterations]\n", cmd);
    printf("        check GPU temperature\n");
    printf("options\n");
    printf("   -l  --list                list available devices\n");
    printf("   -c  --celsius             check temperature in Celsius(default)\n");
    printf("   -f  --fahrenheit          check temperature in Fahrenheit\n");
    printf("   -k  --kelvin              check temperature in Kelvin\n");
    printf("   -i  --interval   <msecs>  interval between output in ms, default is 500ms\n");
    printf("   -r  --iterate    <count>  number of iterations, -1 for infinite\n");
    printf("                             default is set to 1\n");
    printf("Note: mandatory arguments to long arguments are mandatory for short\n");
    printf("arguments as well.\n");
    printf("examples\n");
    printf("%s --list                         list all available GPUs and\n", cmd);
    printf("                                                    get corresponding <gpu-index>\n");
    printf("%s <gpu-index> -c -f              display the temperature in\n", cmd);
    printf("                                                    Celsius and Fahrenheit\n");
    printf("%s <gpu-index> -ckf               display the temperature in\n", cmd);
    printf("                                                    Celsius, Fahrenheit, Kelvin\n");
    printf("%s <gpu-index> -c -i=1000 -r=20   display the temperature in\n", cmd);
    printf("                                                    Celsius for 20 times with\n");
    printf("                                                    1000 ms interval\n");
    printf("%s <gpu-index> -ck -i=5000 -r=-1  display the temperature in\n", cmd);
    printf("                                                    Celsius and Kelvin for\n");
    printf("                                                    infinite times with\n");
    printf("                                                    5000 ms interval\n");
    printf("%s <gpu-index> -c -f -i -r=1000   display the temperature in\n", cmd);
    printf("                                                    Celsius and Fahrenheit for\n");
    printf("                                                    1000 times with\n");
    printf("                                                    500(default) ms interval\n");
}

int main(int argc, char **argv)
{
    int c;
    char *cmd = basename(argv[0]);
    bool temperatureFlag[InputFlags_Count] = {false, false, false, false, false};
    uint32_t intervalMs = 500;
    int32_t userRepeat, userInterval;
    int ret;
    int deviceIndex = 0, optionIndex = 0;
    uint32_t repetition = 1;
    DeviceState deviceState;

    opterr = 0;
    if (argc < 2)
    {
        listUsage(cmd);
        exit(1);
    }
    if (!(atoi(argv[1]) <= 0))
    {
        deviceIndex = atoi(argv[1]);
    }
    while (1)
    {
        c = getopt_long (argc, argv, "lcfki::r::",
                       longOptions, &optionIndex);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
        case 'l':
            listGpu();
            exit(0);
        case 'c':
            temperatureFlag[InputFlags_Celsius] = 1;
            break;
        case 'f':
            temperatureFlag[InputFlags_Fahrenheit] = 1;
            break;
        case 'k':
            temperatureFlag[InputFlags_Kelvin] = 1;
            break;
        case 'i':
            if(optarg == NULL)
            {
                break;
            }
            if (optarg[0] == '=')
            {
                ret = sscanf(optarg+1, "%" SCNd32, &userInterval);
            }
            else
            {
                ret = sscanf(optarg, "%" SCNd32, &userInterval);
            }
            if (ret == 0)
            {
                printf("incorrect interval value : '%s'\n", optarg);
                goto error;
            }
            if (userInterval <= 0)
            {
                printf("interval cannot be less than 1\n");
                goto error;
            }
            intervalMs = userInterval;
            temperatureFlag[InputFlags_Interval] = 1;
            break;
        case 'r':
            if(optarg == NULL)
            {
                break;
            }
            if (optarg[0] == '=')
            {
                ret = sscanf(optarg+1, "%" SCNd32, &userRepeat);
            }
            else
            {
                ret = sscanf(optarg, "%" SCNd32, &userRepeat);
            }
            if (ret == 0)
            {
                printf("incorrect number of iterations : '%s'\n", optarg);
                goto error;
            }
            if (userRepeat < 0)
                repetition = NV_WAIT_INFINITE;
            else
                repetition = userRepeat;
            temperatureFlag[InputFlags_Repeat] = 1;
            break;
        default:
            if (optopt == 0)
            {
                printf("incorrect option: %s\n", argv[optind - 1]);
            }
            else
            {
                printf("incorrect option: %c\n", optopt);
            }
            goto error;
        }
    }

    for(; optind < argc; optind++)
    {
        if (atoi(argv[optind]) != deviceIndex)
        {
            printf("incorrect option: %s\n", argv[optind]);
            goto error;
        }
    }

    if (deviceIndex == 0)
    {
        printf("gpu index not set\n");
        goto error;
    }

    if ((temperatureFlag[InputFlags_Celsius] == 0) && (temperatureFlag[InputFlags_Fahrenheit] == 0)
                                            && (temperatureFlag[InputFlags_Kelvin] == 0))
    {
        temperatureFlag[InputFlags_Celsius] = 1;
    }

    deviceState = gpuTempSensorInfo(deviceIndex, repetition, intervalMs, temperatureFlag);

    switch (deviceState)
    {
    case DeviceState_DeviceFound:
        exit(0);
    case DeviceState_DeviceNotFound:
        goto error;
    case DeviceState_Closed:
        exit(1);
    default:
        listUsage(cmd);
        exit(1);
    }

    exit(0);

error:
    printf("try '%s' for more information\n", cmd);
    exit(1);
}