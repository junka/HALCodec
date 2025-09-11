/* ***************************************************************************** *
 * Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 ******************************************************************************* */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "nvpkcs11.h"
#include "nvpkcs11_public_defs.h"
#include "efs_key_debug.h"

/* Uisng AES-256 bit key as Volume encryption key */
#define VEK_KEY_LEN 32UL

#define CK_SP800_108_COUNTER_FORMAT_WIDTH_IN_BITS 32UL
#define CK_SP800_REQUIRED_LENGTH_FORMAT_WIDTH 32U
#define CK_SP800_REQUIRED_ENTRIES 5U
#define NVPKCS11_MAX_OBJECTS 1U

enum VEK_OP {
    VEK_OP_ENCRYPT,
    VEK_OP_DECRYPT,
    VEK_OP_NONE,
};

static int verbose = 0;

static char VekEncKeyFile[PATH_MAX] = "/etc/nvidia/efs/vek_enc.key";

static int initVekFileName(const char *vekName)
{
    int len, i;

    if (!vekName) return -1;

    /* Sanitize vek name */
    len = strlen(vekName);
    for (i=0; i<len; i++)
    {
        if (!isalnum(vekName[i]) && vekName[i] != '_')
        {
            printf("***ERROR: only alphanumeric and underscore characters allowed in VEK name.\n");
            return -1;
        }
    }

    if (snprintf(VekEncKeyFile, sizeof(VekEncKeyFile), "/etc/nvidia/efs/vek_%s_enc.key", vekName) >= (int)sizeof(VekEncKeyFile))
    {
            printf("***ERROR: VEK name is too long.\n");
            return -1;
    }

    return 0;
}

static int readVEKDataFromFile(const char *file, CK_BYTE_PTR pData, CK_ULONG ulSize)
{
    int ret = -1;
    FILE *fp = NULL;
    long filesize = 0;
    size_t bytes_read = 0;

    if (pData == NULL || ulSize == 0)
        return -1;

    fp = fopen(file, "rb");
    if (fp == NULL)
    {
        printf("***ERROR: cannot open file %s to read\n", file);
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        printf("***ERROR: fseek failed! %s\n", strerror(errno));
        goto lbl_err;
    }
    filesize = ftell(fp);
    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        printf("***ERROR: fseek failed! %s\n", strerror(errno));
        goto lbl_err;
    }

    if ((CK_ULONG)filesize != ulSize)
    {
        printf("***ERROR: file size %lu != expected size %lu\n", (CK_ULONG)filesize, ulSize);
        goto lbl_err;
    }

    bytes_read = fread(pData, sizeof(*pData), ulSize, fp);
    if ((CK_ULONG)bytes_read != ulSize)
    {
        printf("***ERROR: read returned %zd != expected size %lu\n", bytes_read, ulSize);
        goto lbl_err;
    }

    ret = 0;

lbl_err:

    if (fp)
        fclose(fp);

    return ret;
}

static int writeVEKDataToFile(const char *file, CK_BYTE_PTR pData, CK_ULONG ulSize)
{
    int ret = -1, fd = -1;
    FILE *fp = NULL;

    if (pData == NULL || ulSize == 0)
        return -1;

    fd = open(file, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0)
    {
        printf("***ERROR: cannot create file %s, %s\n", file, strerror(errno));
        return -1;
    }

    fp = fdopen(fd, "wb");
    if (fp != NULL)
    {
        if (fwrite(pData, sizeof(*pData), ulSize, fp) == ulSize)
            ret = 0;

        fclose(fp);
    }
    else
        close(fd);

    return ret;
}

static int writeVEKToFile(const char *file, CK_BYTE_PTR pData, CK_ULONG ulSize)
{
    int ret = -1, fd = -1;
    FILE *fp = NULL;
    struct stat st;
    char tmp[PATH_MAX];

    if (pData == NULL || ulSize == 0)
        return -1;

    fd = open(file, O_RDWR);
    if (fd < 0)
    {
        printf("***ERROR: cannot open file %s, %s\n", file, strerror(errno));
        return -1;
    }

    if (realpath(file, tmp) == NULL)
    {
        printf("***ERROR: realpath(%s) failed. %s\n", file, strerror(errno));
        goto lbl_err;
    }
    if (strncmp(tmp, "/tmp/", 5) != 0)
    {
        printf("***ERROR: VEK decryped file (%s) must be be under /tmp/.\n", tmp);
        goto lbl_err;
    }

    if (fstat(fd, &st) != 0)
    {
        printf("***ERROR: stat(%s) failed. %s\n", file, strerror(errno));
        goto lbl_err;
    }

    /* sanity checks */
    if ((!S_ISREG(st.st_mode)) ||
        ((st.st_mode & 0777) != (S_IRUSR | S_IWUSR)) ||
        (st.st_size != 0))
    {
        printf("***ERROR: %s should be a regualar file, with 0600 permissions and of zero size.\n", file);
        goto lbl_err;
    }

    fp = fdopen(fd, "w");
    if (fp != NULL)
    {
        for (CK_ULONG i = 0; i < ulSize; i++)
            fprintf(fp, "%02x", pData[i]);

        ret = 0;
    }

lbl_err:
    if (fp)
        fclose(fp);
    else if (fd >= 0)
        close(fd);

    return ret;
}

#define EFS_MAX_DERV_LBL_LEN            32U
#define EFS_MAX_DERV_CTX_LEN            32U
#define EFS_DEFAULT_CONTEXT_STR         "context"

static CK_RV efsKeyDerive(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObj, CK_OBJECT_HANDLE_PTR phKey, CK_BYTE_PTR pDervLbl, CK_BYTE_PTR pContextStr)
{
    CK_RV rv = CKR_FUNCTION_NOT_SUPPORTED;
    CK_ULONG ulDervLblLen, ulContextLen;

    CK_SP800_108_COUNTER_FORMAT firstEntry =
    {
        .bLittleEndian = (CK_BBOOL)CK_FALSE,
        .ulWidthInBits = CK_SP800_108_COUNTER_FORMAT_WIDTH_IN_BITS
    };

    /* Set up for 2nd entry: label */
    ulDervLblLen = strnlen((const char *)pDervLbl, EFS_MAX_DERV_LBL_LEN + 1);
    if (ulDervLblLen > EFS_MAX_DERV_LBL_LEN)
    {
        printf("***ERROR: Derivation string length should be <=%d, got %lu.\n",
                EFS_MAX_DERV_LBL_LEN, ulDervLblLen);
        return -1;
    }

    DEBUG_PRINT("Derivation label length = %d.\n", (int)ulDervLblLen);

    /* Set required value for the 3rd entry. */
    CK_BYTE thirdEntry[] = {0x00U};

    /* Set up for 4th entry: context */
    if (pContextStr == NULL)
        pContextStr = (CK_BYTE_PTR) EFS_DEFAULT_CONTEXT_STR;

    ulContextLen = strnlen((const char *)pContextStr, EFS_MAX_DERV_CTX_LEN + 1);
    if (ulContextLen > EFS_MAX_DERV_CTX_LEN)
    {
        printf("***ERROR: Context length should be <=%d, got %lu.\n",
                EFS_MAX_DERV_CTX_LEN, ulContextLen);
        return -1;
    }

    DEBUG_PRINT("Context length = %d.\n", (int)ulContextLen);

    CK_SP800_108_DKM_LENGTH_FORMAT fifthEntry =
    {
        .bLittleEndian = (CK_BBOOL)CK_FALSE,
        .ulWidthInBits = CK_SP800_REQUIRED_LENGTH_FORMAT_WIDTH,
        .dkmLengthMethod = CK_SP800_108_DKM_LENGTH_SUM_OF_KEYS
    };

    /* Mandatory settings, 2nd & 4th entry set to user supplied byte array. */
    CK_PRF_DATA_PARAM mechanismArray[CK_SP800_REQUIRED_ENTRIES] =
    {
        [0] =
        {
            .type = CK_SP800_108_ITERATION_VARIABLE,
            .pValue = &firstEntry,
            .ulValueLen = (CK_ULONG)sizeof(CK_SP800_108_COUNTER_FORMAT),
        },
        [1] =
        {
            .type = CK_SP800_108_BYTE_ARRAY,
            .pValue = pDervLbl,
            .ulValueLen = ulDervLblLen,
        },
        [2] =
        {
            .type = CK_SP800_108_BYTE_ARRAY,
            .pValue = thirdEntry,
            .ulValueLen = (CK_ULONG)sizeof(thirdEntry)
        },
        [3] =
        {
            .type = CK_SP800_108_BYTE_ARRAY,
            .pValue = pContextStr,
            .ulValueLen = ulContextLen,
        },
        [4] =
        {
            .type = CK_SP800_108_DKM_LENGTH,
            .pValue = &fifthEntry,
            .ulValueLen = (CK_ULONG)sizeof(CK_SP800_108_DKM_LENGTH_FORMAT),
        }
    };

    CK_SP800_108_KDF_PARAMS ck_sp800_108_kdf_params =
    {
        .prfType = (CK_SP800_108_PRF_TYPE)CKM_SHA256_HMAC,
        .ulNumberOfDataParams = CK_SP800_REQUIRED_ENTRIES,
        .pDataParams = mechanismArray,
        .ulAdditionalDerivedKeys = 0U,
        .pAdditionalDerivedKeys = NULL,
    };

    /* Ensure padding with space to make 32 bytes as per CK_ID requirement. */
    CK_BYTE cka_string[NVPKCS11_MAX_KEY_ID_SIZE] = "DERIVED_KEY                     ";

    CK_MECHANISM mechanism_derivekey = { CKM_SP800_108_COUNTER_KDF, &ck_sp800_108_kdf_params, sizeof(ck_sp800_108_kdf_params)};
    CK_MECHANISM_TYPE mechanismList[] = {CKM_AES_CBC, CKM_AES_CBC_PAD};

    CK_BBOOL attribute_true = TRUE;
    CK_ULONG bytesLen = 16U;
    CK_KEY_TYPE keyType = CKK_AES;

    /*
     * We set only the attributes which are mandatory or need update from
     * default.
     */
    CK_ATTRIBUTE template[] =
    {
        {CKA_ID, &cka_string, sizeof(cka_string)},
        {CKA_ENCRYPT, &attribute_true, sizeof(CK_BBOOL)},
        {CKA_DECRYPT, &attribute_true, sizeof(CK_BBOOL)},
        {CKA_VALUE_LEN, &bytesLen, sizeof(bytesLen)},
        {CKA_ALLOWED_MECHANISMS, mechanismList, sizeof(mechanismList)},
        {CKA_KEY_TYPE, &keyType, sizeof(CK_KEY_TYPE)},
    };

    CK_ULONG ulAttributeCount = sizeof(template)/sizeof(CK_ATTRIBUTE);

    rv = C_DeriveKey(hSession,
            &mechanism_derivekey,
            hObj,
            template,
            ulAttributeCount,
            phKey);

    return rv;
}

static CK_RV efsKeySessionSetup(CK_SLOT_ID slotID, CK_SESSION_HANDLE_PTR phSession, CK_OBJECT_HANDLE_PTR phKey, CK_BYTE_PTR pDervLbl, CK_BYTE_PTR pContextStr)
{
    CK_RV rv = CKR_OK;
    CK_OBJECT_HANDLE hObj[NVPKCS11_MAX_OBJECTS] = { 0U };
    CK_ULONG ulObjectCount = 0;

    rv = C_OpenSession(slotID, (CK_FLAGS)CKF_SERIAL_SESSION, NULL, NULL, phSession);
    C_FUNCTION_RESULT(rv, C_OpenSession, lbl_err, CKR_OK);

    rv = C_Login(*phSession, CKU_USER, NULL, 0);
    C_FUNCTION_RESULT(rv, C_Login, lbl_err, CKR_OK);

    CK_BYTE cka_string[NVPKCS11_MAX_KEY_ID_SIZE] = "NV_OEM_KEY1                     ";
    CK_OBJECT_CLASS searchObjectClass = CKO_SECRET_KEY;
    CK_ATTRIBUTE template[] = {
                                {CKA_ID, &cka_string, sizeof(cka_string)},
                                {CKA_CLASS, &searchObjectClass, sizeof(CK_OBJECT_CLASS)}};

    rv = C_FindObjectsInit(*phSession, template, sizeof(template)/sizeof(CK_ATTRIBUTE));
    C_FUNCTION_RESULT(rv, C_FindObjectsInit, lbl_err, CKR_OK);

    rv = C_FindObjects(*phSession, &hObj[0], (CK_ULONG)1, &ulObjectCount);
    C_FUNCTION_RESULT(rv, C_FindObjects,  lbl_err, CKR_OK);

    rv = C_FindObjectsFinal(*phSession);
    C_FUNCTION_RESULT(rv, C_FindObjectsFinal, lbl_err, CKR_OK);

    rv = efsKeyDerive(*phSession, hObj[0], phKey, pDervLbl, pContextStr);
    C_FUNCTION_RESULT(rv, C_DeriveKey, lbl_err, CKR_OK);

lbl_err:

    return rv;
}

static CK_RV nvpkcs11Init(CK_SLOT_ID_PTR pSlotID)
{
    CK_RV rv = CKR_OK;
    CK_BBOOL isTokenPresent = (CK_BBOOL)CK_TRUE;
    CK_BBOOL isTokenFound = (CK_BBOOL)CK_FALSE;
    CK_SLOT_ID slotIds[NVPKCS11_TOKEN_COUNT];
    CK_ULONG slotCount = NVPKCS11_TOKEN_COUNT;
    CK_ULONG i;

    rv = C_Initialize(NULL);
    C_FUNCTION_RESULT(rv, C_Initialize, lbl_err, CKR_OK);

    rv = C_GetSlotList(isTokenPresent, slotIds, &slotCount);
    C_FUNCTION_RESULT(rv, C_GetSlotList, lbl_err, CKR_OK);

    for (i = (CK_ULONG)0; i < slotCount; i++)
    {
	    CK_TOKEN_INFO tokenInfo;
	    rv = C_GetTokenInfo(slotIds[i], &tokenInfo);
	    if (rv == CKR_OK)
	    {
		    if (strncmp((char *)tokenInfo.model, NVPKCS11_CCPLEX_DYNAMIC_2_MODEL_NAME, sizeof(tokenInfo.model)) == 0)
		    {
			    *pSlotID = slotIds[i];
			    isTokenFound = (CK_BBOOL)CK_TRUE;
			    break;
		    }
	    }
    }

    if(isTokenFound == (CK_BBOOL)CK_FALSE)
    {
	    printf("Could not find requested token.\n");
	    rv = CKR_TOKEN_NOT_PRESENT;
	    goto lbl_err;
    }

lbl_err:

    return rv;
}

static CK_RV efsKeySessionTerminate(CK_SLOT_ID slotID, CK_SESSION_HANDLE_PTR phSession, CK_OBJECT_HANDLE_PTR phKey)
{
    CK_RV rv = CKR_OK;

    if (phKey != NULL)
    {
        rv = C_DestroyObject(*phSession, *phKey);
        C_FUNCTION_RESULT(rv, C_DestroyObject, lbl_err, CKR_OK);
    }

    rv = C_CloseSession(*phSession);
    C_FUNCTION_RESULT(rv, C_CloseSession, lbl_err, CKR_OK);

    rv = C_Finalize(NULL);
    C_FUNCTION_RESULT(rv, C_Finalize, lbl_err, CKR_OK);

lbl_err:

    return rv;
}

static CK_RV efsKeyCreateEncryptedVEK(CK_BYTE_PTR pDervLbl, CK_BYTE_PTR pContextStr)
{
    CK_RV rv = CKR_FUNCTION_FAILED;
    CK_SLOT_ID slotID = 0;
    CK_SESSION_HANDLE hSession = 0;
    CK_OBJECT_HANDLE hKey[NVPKCS11_MAX_OBJECTS] = { 0U };
    CK_BYTE vek[VEK_KEY_LEN];
    CK_BYTE encryptedData[VEK_KEY_LEN] = {0x00};
    CK_ULONG ulEncryptedDataLen = sizeof(encryptedData);

    CK_BYTE iv[NVPKCS11_AES_CBC_IV_LEN] = {0x00};
    CK_BYTE ivLen = NVPKCS11_AES_CBC_IV_LEN;

    CK_BYTE data[VEK_KEY_LEN + NVPKCS11_AES_CBC_IV_LEN];

    rv = nvpkcs11Init(&slotID);
    if (rv != CKR_OK)
        goto lbl_err;


    rv = efsKeySessionSetup(slotID, &hSession, hKey, pDervLbl, pContextStr);
    if (rv != CKR_OK)
        goto lbl_err;

    rv = C_GenerateRandom(hSession, vek, VEK_KEY_LEN);
    C_FUNCTION_RESULT(rv, C_GenerateRandom, lbl_err, CKR_OK);
    DEBUG_HEXDUMP("VEK", vek, sizeof(vek));

    CK_MECHANISM mechanism_encrypt = {CKM_AES_CBC, NULL, 0};
    rv = C_EncryptInit(hSession, &mechanism_encrypt, hKey[0]);
    C_FUNCTION_RESULT(rv, C_EncryptInit, lbl_err, CKR_OK);

    rv = C_Encrypt(hSession,
            (CK_BYTE_PTR)vek,
            sizeof(vek),
            encryptedData,
            &ulEncryptedDataLen);
    C_FUNCTION_RESULT(rv, C_Encrypt, lbl_err, CKR_OK);

    (void)memset(vek, 0, sizeof(vek));

    /* Sanity check encrypted data len */
    if (ulEncryptedDataLen != VEK_KEY_LEN)
    {
        printf("***ERROR: VEK encrypted data len %lu != %lu\n",
                ulEncryptedDataLen, (CK_ULONG)VEK_KEY_LEN);
        rv = -1;
        goto lbl_err;
    }

    DEBUG_PRINT("Encrypted VEK length %lu \n", ulEncryptedDataLen);
    DEBUG_HEXDUMP("Encrypted VEK", encryptedData, ulEncryptedDataLen);

    /* This IV is needed for decrypt. */
    rv = C_NVIDIA_EncryptGetIV(hSession, iv, &ivLen);
    C_FUNCTION_RESULT(rv, C_NVIDIA_EncryptGetIV, lbl_err, CKR_OK);
    DEBUG_HEXDUMP("IV data", iv, ivLen);

    /* Sanity check iv len */
    if (ivLen != NVPKCS11_AES_CBC_IV_LEN)
    {
        printf("***ERROR: IV len %lu != %lu\n", (CK_ULONG)ivLen,
                (CK_ULONG)NVPKCS11_AES_CBC_IV_LEN);
        rv = -1;
        goto lbl_err;
    }

    rv = efsKeySessionTerminate(slotID, &hSession, hKey);

    if (rv == CKR_OK)
    {
        memcpy(data, encryptedData, VEK_KEY_LEN);
        memcpy(data + VEK_KEY_LEN, iv, NVPKCS11_AES_CBC_IV_LEN);

        if (writeVEKDataToFile(VekEncKeyFile, data, sizeof(data)) == 0)
        {
            printf("Encrypt operation successful, following file is created:\n"
                    "   - %s\n", VekEncKeyFile);
        }
        else
        {
            printf("***ERROR: Output file(s) not created, check filesystem access.\n");
            rv = -1;
        }
    }

lbl_err:

    return rv;
}

static CK_RV efsKeyDecryptVEK(const char *file, CK_BYTE_PTR pDervLbl, CK_BYTE_PTR pContextStr)
{
    CK_RV rv = CKR_FUNCTION_FAILED;
    CK_SLOT_ID slotID = 0;
    CK_SESSION_HANDLE hSession = 0;
    CK_OBJECT_HANDLE hKey[NVPKCS11_MAX_OBJECTS] = { 0U };
    CK_BYTE data[VEK_KEY_LEN + NVPKCS11_AES_CBC_IV_LEN];
    CK_BYTE decryptedData[VEK_KEY_LEN] = {0x00};
    CK_ULONG ulDecryptedDataLen = sizeof(decryptedData);
    CK_AES_CBC_ENCRYPT_DATA_PARAMS mechanism_decrypt_params = { 0 };

    rv = nvpkcs11Init(&slotID);
    if (rv != CKR_OK)
        goto lbl_err;

    /* Set up derived symmetric key using KEK as base. */
    rv = efsKeySessionSetup(slotID, &hSession, hKey, pDervLbl, pContextStr);
    if (rv != CKR_OK)
        goto lbl_err;


    if (readVEKDataFromFile(VekEncKeyFile, data, sizeof(data)) != 0)
    {
        printf("***ERROR: Failed to read ENC_KEY/IV data from %s.\n", VekEncKeyFile);
        goto lbl_err;
    }

    DEBUG_HEXDUMP("Encrypted VEK from file:", data, VEK_KEY_LEN);
    DEBUG_HEXDUMP("IV from file:", data + VEK_KEY_LEN, NVPKCS11_AES_CBC_IV_LEN);

    (void)memcpy(mechanism_decrypt_params.iv, data + VEK_KEY_LEN, NVPKCS11_AES_CBC_IV_LEN);

    mechanism_decrypt_params.length = NVPKCS11_AES_CBC_IV_LEN;
    mechanism_decrypt_params.pData = NULL;
    CK_MECHANISM mechanism_decrypt = {CKM_AES_CBC, &mechanism_decrypt_params, NVPKCS11_AES_CBC_IV_LEN};
    rv = C_DecryptInit(hSession, &mechanism_decrypt, hKey[0]);
    C_FUNCTION_RESULT(rv, C_DecryptInit, lbl_err, CKR_OK);

    rv = C_Decrypt(hSession,
            data,
            VEK_KEY_LEN,
            decryptedData,
            &ulDecryptedDataLen);

    C_FUNCTION_RESULT(rv, C_Decrypt, lbl_err, CKR_OK);

    DEBUG_PRINT("Decrypted data length %lu \n", ulDecryptedDataLen);
    DEBUG_HEXDUMP("Decrypted data", decryptedData, ulDecryptedDataLen);

    rv = efsKeySessionTerminate(slotID, &hSession, hKey);

    if (rv == CKR_OK)
    {
        if (writeVEKToFile(file, decryptedData, ulDecryptedDataLen) == 0)
        {
            printf("Decrypt operation successful, key written to %s\n", file);
        }
        else
        {
            printf("***ERROR: %s file not written, check filesystem access.\n", file);
            rv = -1;
        }
    }
    (void)memset(decryptedData, 0, ulDecryptedDataLen);

lbl_err:

    return rv;

}

static void printUsage(int error)
{
    printf("Usage:\n"
           "efs_key --encrypt|--decrypt <other options>\n\n"
           " -e            --encrypt             Select encrypt operation OR\n"
           " -d <filename> --decrypt=<filename>  Select decrypt operation, writing the output to <filename>\n"
           "                                     <filename> must be a regular file under /tmp/ with mode 0600 and size 0.\n"
           " -p <string>   --derivation-string=<string>  Specify key derivation string (required, max 32 bytes).\n"
           " -c <string>   --context-string=<string>     Specify context string (optional, max 32 bytes. Default: context).\n"
           " -f            --vek-name=<string>   Name suffix for key/iv filenames (optional)\n"
           " --verbose                           Print verbose information.\n"
           " -h            --help                Show usage.\n"
           "\n>> Decrypted key is written as a Hexadecimal string, one character per 4-bits.\n"
           ">> Decrypt operation requires following file to be present:\n"
           "    - /etc/nvidia/efs/vek_<VEK_NAME>_enc.key\n"
           ">> Above file is generated as part of the encrypt operation.\n");

    exit(error);
}

int main(int32_t argc, char *argv[])
{
    enum VEK_OP operation = VEK_OP_NONE;
    CK_BYTE_PTR pDerivationString = NULL;
    CK_BYTE_PTR pContextStr = NULL;
    const char *pVekName = NULL;
    const char *pDecryptFilename = NULL;
    static struct option longOptions[] = {
        {"encrypt", no_argument, NULL, 'e'},
        {"decrypt", required_argument, NULL, 'd'},
        {"derivation-string", required_argument, NULL, 'p'},
        {"context-string", required_argument, NULL, 'c'},
        {"vek-name", required_argument, NULL, 'f'},
        {"verbose", no_argument, &verbose, 1},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0}
    };
    int c;

    while(1)
    {
        c = getopt_long(argc, argv, "p:c:f:ed:h", longOptions, NULL);
        if (c == -1)
            break;

        switch (c)
        {
            case 0:
                break;
            case 'p':
                pDerivationString = (CK_BYTE_PTR)optarg;
                break;
            case 'c':
                pContextStr = (CK_BYTE_PTR)optarg;
                break;
            case 'f':
                pVekName = optarg;
                break;
            case 'e':
                if (operation == VEK_OP_DECRYPT)
                {
                    printf("***ERROR: Specify only one of --encrypt or --decrypt operations.\n");
                    return -1;
                }
                operation = VEK_OP_ENCRYPT;
                break;
            case 'd':
                if (operation == VEK_OP_ENCRYPT)
                {
                    printf("***ERROR: Specify only one of --encrypt or --decrypt operations.\n");
                    return -1;
                }
                operation = VEK_OP_DECRYPT;
                pDecryptFilename = optarg;
                break;

            case 'h':
                /* Doesn't return */
                printUsage(0);
                break;

            default:
                /* Doesn't return */
                printUsage(1);
                break;
        }
    }

    if (operation == VEK_OP_NONE)
    {
        printf("***ERROR: Either of --encrypt or --decrypt must be specified.\n");
        return -1;
    }

    if (pDerivationString == NULL)
    {
        printf("***ERROR: Derivation string not provided.\n");
        return -1;
    }

    if (pVekName != NULL)
    {
        if (initVekFileName(pVekName) != 0) return -1;
    }

    DEBUG_PRINT("Selected operation: %s\n", operation == VEK_OP_ENCRYPT ? "Create & Encrypt VEK" : "Decrypt VEK");
    DEBUG_PRINT("Derivation string: %s\n", pDerivationString);
    DEBUG_PRINT("Encrypted VEK filename: %s\n", VekEncKeyFile);


    if (operation == VEK_OP_ENCRYPT)
    {
        return (int)efsKeyCreateEncryptedVEK(pDerivationString, pContextStr);
    }
    else
    {
        if (pDecryptFilename == NULL)
        {
            printf("***ERROR: filename must be specified for decrypt operation.\n");
            return -1;
        }

        DEBUG_PRINT("Decrypted VEK filename: %s\n", pDecryptFilename);

        return (int)efsKeyDecryptVEK(pDecryptFilename, pDerivationString, pContextStr);
    }
}
