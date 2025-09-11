/*
 * SPDX-FileCopyrightText: Copyright (c) 2010-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <nvmnand.h>
#include <inttypes.h>

#define HS_INFO_TYPE_BADBLOCKS          (1 << 0)
#define HS_INFO_TYPE_AGE                (1 << 1)
#define HS_INFO_TYPE_AGE_TOTALS         (1 << 2)
#define HS_INFO_TYPE_SUMMARY_AGE        (1 << 3)
#define HS_INFO_TYPE_SPARE_SUMMARY      (1 << 4)
#define HS_INFO_TYPE_REFRESHED_BLKS     (1 << 5)
#define HS_INFO_TYPE_REFRESH_PROGRESS   (1 << 6)
#define HS_INFO_TYPE_CARD_STATUS        (1 << 7)
#define HS_INFO_TYPE_EXT_CSD            (1 << 8)
#define HS_INFO_TYPE_GET_BKOPS_EN       (1 << 9)
#define HS_INFO_TYPE_BKOPS_STATUS       (1 << 10)
#define HS_INFO_TYPE_BKOPS_START        (1 << 11)
#define HS_INFO_TYPE_CSD                (1 << 12)
#define HS_INFO_TYPE_CID                (1 << 13)
#define HS_INFO_TYPE_TRIM_ALL           (1 << 14)
#define HS_INFO_TYPE_EN_POWER_OFF       (1 << 15)
#define HS_INFO_TYPE_SEND_POWER_OFF     (1 << 16)
#define HS_INFO_TYPE_SET_BKOPS_EN       (1 << 17)
#define HS_INFO_TYPE_EOL_STATUS         (1 << 18)
#define HS_INFO_TYPE_EN_CACHE           (1 << 19)
#define HS_INFO_TYPE_FLUSH_CACHE        (1 << 20)
#define HS_INFO_TYPE_DATA_AREA          (1 << 21)
#define HS_INFO_TYPE_DISABLE_CACHE      (1 << 22)
#define HS_INFO_TYPE_CACHE_STATUS       (1 << 23)
#define HS_INFO_TYPE_READ_FLAG          (1 << 24)
#define HS_INFO_TYPE_WRITE_FLAG         (1 << 25)
#define HS_INFO_TYPE_READ_ATTR          (1 << 26)
#define HS_INFO_TYPE_WRITE_ATTR         (1 << 27)
#define HS_INFO_TYPE_READ_DESC          (1 << 28)
#define HS_INFO_TYPE_FFU                (1 << 29)
#define HS_INFO_TYPE_FFU_STATUS         (1 << 30)
#define HS_INFO_TYPE_FW_VERSION         (1 << 31)

/* index to fetch cache control byte */
#define EXT_CSD_CACHE_CTRL_BYTE_INDX    33

#define MAX_DESC_BUF_SIZE               144

typedef enum { adf_hex_compact, adf_dec_csv } age_dump_format;

typedef struct _ufs_health_status_info {
    uint8_t flag_idn;
    uint8_t flag_val;
    uint8_t attr_idn;
    uint32_t attr_val;
    uint8_t desc_idn;
    uint8_t desc_buf[MAX_DESC_BUF_SIZE];
} ufs_health_status_info;

typedef struct _health_status_info {
    uint32_t included_info; /* combination of HS_INFO constants */
    uint32_t blocks_to_refresh;
    uint32_t rfsh_unit_time;
    uint8_t bkops_en;
    uint8_t bkops_status;
    MNAND_FFU_STATUS ffu_status;
    char fw_version[32];
    uint8_t cache_en;
    uint8_t cache_disable;
    mnand_refresh_progress rfsh_result;
    double cur_rfsh_progress;
    age_dump_format age_format;
    MNAND_EOL_STATUS eol_status;
    ufs_health_status_info ufs;
    mnand_chip chip;
    char *ffu_file;
} health_status_info;

/* print buffer in hex mode */
static void hexdump(unsigned char *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        if ((i & 0x0f) == 0)
            printf("%04d: ", i);
        printf("%02X%c", buf[i], (i & 0x0f) == 7 ? '-' : ' ');
        if (((i + 1) % 16) == 0)
            printf("\n");
    }
}

/* print age information (total) */
static void dump_age_totals(health_status_info *info, int block_type)
{
    int i;
    uint64_t age_total = 0;
    int blocks = 0;
    int blks_with_age = 0;
    int min_no = info->chip.num_blocks + 1, max_no = -1;
    mnand_block_info *bptr;

    for (i = 0, bptr = info->chip.block_info; i < info->chip.num_blocks;
        i++, bptr++) {
        if (mnand_is_usable_block_with_type(bptr, block_type)) {
            age_total += bptr->block_age;
            blocks++;
            if (bptr->block_age)
                blks_with_age++;
            /* look for block range */
            if (min_no > (int)bptr->block_number)
                min_no = (int)bptr->block_number;
            if (max_no < (int)bptr->block_number)
                max_no = (int)bptr->block_number;
        }
    }

    if (max_no >= min_no)
        printf("\tTotal number of blocks (%d~%d): %d\n", min_no, max_no,
            max_no - min_no + 1);
    printf("\tTotal p/e cycles: %llu\n", (unsigned long long)age_total);
    printf("\tNumber of usable blocks: %d\n", blocks);
    printf("\tNumber of usable blocks: %d (only blocks with age > 0)\n",
        blks_with_age);
    if (blocks)
        printf("\tAvg p/e cycles:   %.3f\n", (double)age_total / blocks);
}

/* dumping block information */
static void dump_per_block_info(health_status_info *info, int block_type)
{
    int cnt;
    int i;
    mnand_block_info *bptr;
    uint16_t age_count[MAX_ERASE_CNT + 1];

    memset(age_count, 0, sizeof(age_count));

    bptr = info->chip.block_info;
    for (i = 0, cnt = 0; i < info->chip.num_blocks; i++, bptr++) {
        if (mnand_is_usable_block_with_type(bptr, block_type)) {
            if (bptr->block_age != 0) {
                if (info->age_format == adf_hex_compact) {
                    printf("%04X:%04d ", i, bptr->block_age);
                    cnt++;
                    if ((cnt % 8) == 0)
                        printf("\n");
                } else
                    printf("%d,%d\n", i, bptr->block_age);
                age_count[bptr->block_age]++;
            }
        }
    }

    printf("\nSummary:\n");
    for (i = 0; i <= MAX_ERASE_CNT; i++) {
        if (age_count[i]) {
            printf("%d %s block(s) with %d erase cycles.\n",
                (int)age_count[i],
                block_type & MNAND_MLC_BLOCK ? "mlc" : "slc",
                (int)i);
        }
    }
}

/* dumping all information */
static void dump_info(health_status_info *info)
{
    int i;

    if (info->included_info & HS_INFO_TYPE_BADBLOCKS) {
        printf("BAD BLOCK DATA:\n");
        for (i = 0; i < info->chip.num_blocks; i++) {
            if (mnand_is_bad_block(&info->chip.block_info[i]))
                printf("Bad Block on block %#x\n", i);
        }
    }

    if (info->included_info & HS_INFO_TYPE_AGE) {
        printf("PER-BLOCK DATA [MLC blocks, erase count > 0 only]:\n");
        printf("Format: <Block Nbr>: <erase count>\n");
        dump_per_block_info(info, MNAND_MLC_BLOCK);

        printf("\nPER-BLOCK DATA [SLC blocks, erase count > 0 only]:\n");
        printf("Format: <Block Nbr>: <erase count>\n");
        dump_per_block_info(info, MNAND_SLC_BLOCK);
    }

    if (info->included_info & HS_INFO_TYPE_AGE_TOTALS) {
        printf("MLC Age Totals:\n");
        dump_age_totals(info, MNAND_MLC_BLOCK);

        printf("SLC Age Totals:\n");
        dump_age_totals(info, MNAND_SLC_BLOCK);
    }

    if (info->included_info & HS_INFO_TYPE_SUMMARY_AGE) {
        if (info->chip.summary->mlc.valid && info->chip.summary->mlc.ages.valid) {
            printf("MLC Minimum erase count: %d\n",
                info->chip.summary->mlc.ages.min_age);
            printf("MLC Maximum erase count: %d\n",
                info->chip.summary->mlc.ages.max_age);
            printf("MLC Average erase count: %.3f\n",
                info->chip.summary->mlc.ages.avg_age);
            if (info->chip.summary->mlc.ages.total_age != 0)
                printf("MLC Total erase count: %d\n",
                    info->chip.summary->mlc.ages.total_age);
        }
        if (info->chip.summary->slc.valid && info->chip.summary->slc.ages.valid) {
            printf("SLC Minimum erase count: %d\n",
                info->chip.summary->slc.ages.min_age);
            printf("SLC Maximum erase count: %d\n",
                info->chip.summary->slc.ages.max_age);
            printf("SLC Average erase count: %.3f\n",
                info->chip.summary->slc.ages.avg_age);
            if (info->chip.summary->slc.ages.total_age != 0)
                printf("SLC Total erase count: %d\n",
                    info->chip.summary->slc.ages.total_age);
        }
        if (info->chip.summary->total.valid && info->chip.summary->total.ages.valid) {
            printf("Minimum erase count: %d\n",
                info->chip.summary->total.ages.min_age);
            printf("Maximum erase count: %d\n",
                info->chip.summary->total.ages.max_age);
            printf("Average erase count: %.3f\n",
                info->chip.summary->total.ages.avg_age);
            if (info->chip.summary->total.ages.total_age != 0)
                printf("Total erase count: %d\n",
                    info->chip.summary->total.ages.total_age);
        }
        if (info->chip.summary->factory_bb_cnt >= 0) {
            printf("Factory bad block count: %d\n",
                info->chip.summary->factory_bb_cnt);
        }
        if (info->chip.summary->runtime_bb_cnt >= 0) {
            printf("Runtime bad block count: %d\n",
                info->chip.summary->runtime_bb_cnt);
        }
    }

    if (info->included_info & HS_INFO_TYPE_SPARE_SUMMARY) {
        if (info->chip.summary->mlc.valid) {
            if (info->chip.summary->mlc.life.life_time_type == MNAND_LIFETIME_IN_BLOCK) {
                printf("MLC Spare block count: %d\n",
                    info->chip.summary->mlc.life.life_time.spare_blocks);
            } else if (info->chip.summary->mlc.life.life_time_type == MNAND_LIFETIME_IN_PERCENT) {
                printf("MLC life time percentage: %.3f%%\n",
                    info->chip.summary->mlc.life.life_time.percentage);
            }
        }
        if (info->chip.summary->slc.valid) {
            if (info->chip.summary->slc.life.life_time_type == MNAND_LIFETIME_IN_BLOCK) {
                printf("SLC Spare block count: %d\n",
                    info->chip.summary->slc.life.life_time.spare_blocks);
            } else if (info->chip.summary->slc.life.life_time_type == MNAND_LIFETIME_IN_PERCENT) {
                printf("SLC life time percentage: %.3f%%\n",
                    info->chip.summary->slc.life.life_time.percentage);
            }
        }
        if (info->chip.summary->total.valid) {
            if (info->chip.summary->total.life.life_time_type == MNAND_LIFETIME_IN_BLOCK) {
                printf("Spare block count: %d\n",
                    info->chip.summary->total.life.life_time.spare_blocks);
            } else if (info->chip.summary->total.life.life_time_type == MNAND_LIFETIME_IN_PERCENT) {
                printf("Life time percentage: %.3f%%\n",
                    info->chip.summary->total.life.life_time.percentage);
            }
        }
    }

    if (info->included_info & HS_INFO_TYPE_REFRESH_PROGRESS) {
        printf("Current refresh progress: %.3f%%\n", info->cur_rfsh_progress);
    }

    if (info->included_info & HS_INFO_TYPE_REFRESHED_BLKS) {
        if (info->blocks_to_refresh > 0) {
            printf("Refreshed %d block(s)\n", info->blocks_to_refresh);
            if (info->rfsh_result.progress_type == MNAND_RFSH_PROG_IN_BLOCK)
                printf("Last refreshed block: %d\n", info->rfsh_result.progress.block);
            else if (info->rfsh_result.progress_type == MNAND_RFSH_PROG_IN_PERCENT)
                printf("Refresh percentage: %.3f%%\n",
                    info->rfsh_result.progress.percentage);
        }
    }

    if (info->included_info & HS_INFO_TYPE_CARD_STATUS)
        printf("Card status: %08x\n", info->chip.card_status);

    if (info->included_info & HS_INFO_TYPE_EXT_CSD) {
        printf("Ext Csd:\n");
        hexdump(info->chip.xcsd.data, sizeof(info->chip.xcsd.data));
    }

    if ((info->included_info & HS_INFO_TYPE_DATA_AREA) &&
        (mnand_get_storage_type(&info->chip) == MNAND_EMMC)) {
        uint32_t sec_count;
        uint32_t enh_start_sec;
        uint32_t cur_enh_size_mult;
        uint64_t euda_sz = 0;
        uint64_t uda_sz = 0;
        mnand_geo_info geo_info;

        if (mnand_get_geo_info(&info->chip, &geo_info) != MNAND_OK) {
            printf("Failed to retrieve mnand geometry info \n");
        } else {
            sec_count = geo_info.mmc_geo_info.sec_count;
            enh_start_sec = geo_info.mmc_geo_info.enh_start_addr;
            cur_enh_size_mult = geo_info.mmc_geo_info.enh_size_mult;
            euda_sz = ((uint64_t)cur_enh_size_mult *
                geo_info.mmc_geo_info.hc_wp_grp_size *
                geo_info.mmc_geo_info.hc_erase_grp_size) << 19;

           /* Total user capacity = (sec_count * 512) / (1024 * 1024) */
            printf("Total user capacity %dMB\n", sec_count >> 11);

            if (euda_sz != 0) {
                printf("EUDA size: %"PRIu64"\n", euda_sz >> 20);
                printf("EUDA start sector: %d (offset %dMB)\n", enh_start_sec,
                    enh_start_sec >> 11);
                if ((euda_sz >> 9) == sec_count)
                    printf("No UDA defined\n");
                else {
                    uda_sz = ((uint64_t)sec_count << 9) - euda_sz;
                    printf("UDA size: %"PRIu64"MB\n", uda_sz >> 20);
                    if (enh_start_sec == 0)
                        printf("UDA start sector: %"PRIu64" (offset %"PRIu64"MB)\n",
                            euda_sz >> 9, euda_sz >> 20);
                    else {
                        if ((uda_sz >> 9) > enh_start_sec) {
                            /*
                             * This should not happen as mnand_repartition will
                             * not partition this way, but it may still happen
                             *  with direct access to EXT_CSD by other programs
                             */
                            printf("UDA 1st start sector: 0 (offset 0MB)\n");
                            printf("UDA 2nd start sector: %d (offset %dMB)\n",
                                enh_start_sec + (uint32_t)(euda_sz >> 9),
                                (enh_start_sec + (uint32_t)(euda_sz >> 9))
                                >> 11);
                        } else
                            printf("UDA start sector: 0 (offset 0MB)\n");
                    }
                }
            } else {
                printf("No EUDA defined\n");
                printf("UDA size: %dMB\n", sec_count >> 11);
                printf("UDA start sector: 0 (offset 0MB)\n");
            }
        }
    } else if ((info->included_info & HS_INFO_TYPE_DATA_AREA) &&
        (mnand_get_storage_type(&info->chip) == MNAND_UFS)) {
        mnand_geo_info geo_info;
        mnand_lu_info lu_info;
        uint32_t lun;
        uint64_t lu_size = 0;

        if (mnand_get_geo_info(&info->chip, &geo_info) != MNAND_OK) {
            printf("Failed to retrieve mnand geometry info \n");
        } else {
            printf("Total user capacity %lluMB\n",
               (long long unsigned int)geo_info.ufs_geo_info.total_raw_capacity >> 11);
            printf("max_lu = %d segment_size = %d alloc_unit_size = %d supported memory types = 0x%x\n",
                geo_info.ufs_geo_info.max_lu, geo_info.ufs_geo_info.segment_size,
                geo_info.ufs_geo_info.alloc_unit_size,
                geo_info.ufs_geo_info.supported_memory_types);
            printf(" Lun    Enabled    Num Alloc Units     Log Blk Size    LU Size     Memory Type\n");

            for (i = 0 ; i < geo_info.ufs_geo_info.max_lu; i++) {
                if (mnand_get_lu_info(&info->chip, i, &lu_info) != MNAND_OK) {
                   printf("Failed to retrieve logical unit info \n");
                   break;
                }

                lu_size = (uint64_t)lu_info.num_alloc_units *
                    (uint64_t)(geo_info.ufs_geo_info.segment_size * 512) *
                    (uint64_t)geo_info.ufs_geo_info.alloc_unit_size;
                printf(" %2d  %10u     %10u      %10d   %15llu     %4x\n", i,
                    lu_info.lu_enabled, lu_info.num_alloc_units,
                    512 << lu_info.logical_blk_sz, (unsigned long long)lu_size,
                    lu_info.memory_type);
            }

            if (mnand_get_current_lun(&info->chip, &lun) == MNAND_OK)
                printf("\n Current LUN = %u\n", lun);
            else
                printf("Failed to retrieve curent lun\n");
        }
    }

    if (info->included_info & HS_INFO_TYPE_CSD) {
        printf("Csd:\n");
        hexdump(info->chip.csd.data, sizeof(info->chip.csd.data));
    }

    if (info->included_info & HS_INFO_TYPE_CID) {
        printf("Cid:\n");
        hexdump(info->chip.cid.data, sizeof(info->chip.cid.data));
    }

    if (info->included_info & HS_INFO_TYPE_EOL_STATUS) {
        printf("EOL: %s\n", info->eol_status == MNAND_EOL_DETECTED ?
            "Detected" : "None");
    }

    if (info->included_info & HS_INFO_TYPE_GET_BKOPS_EN)
        printf("BKOPS_EN: %s\n", info->bkops_en ? "Set" : "Cleared");

    if (info->included_info & HS_INFO_TYPE_BKOPS_STATUS)
        printf("BKOPS_STATUS: 0x%02x\n", info->bkops_status);

    if (info->included_info & HS_INFO_TYPE_FFU_STATUS)
        printf("FFU_STATUS: 0x%02x\n", info->ffu_status);

    if ((info->included_info & HS_INFO_TYPE_FW_VERSION) && (info->fw_version != NULL))
        printf("FW_VERSION: %s\n", info->fw_version);

    if (info->included_info & HS_INFO_TYPE_READ_FLAG)
        printf("FLAG(idn = %u) : %s\n", info->ufs.flag_idn,
            info->ufs.flag_val ? "Set" : "Cleared");

    if (info->included_info & HS_INFO_TYPE_READ_ATTR)
        printf("ATTR(idn = %u) : 0x%02x\n", info->ufs.attr_idn,
            info->ufs.attr_val);

    if (info->included_info & HS_INFO_TYPE_READ_DESC) {
        printf("DESC(idn = %u) :\n", info->ufs.desc_idn);
        hexdump(info->ufs.desc_buf, MAX_DESC_BUF_SIZE);
    }
}

static int trim_all(health_status_info *info)
{
    int res = -1;
    char c;
    struct timeval t0, t1;
    double elapsed;

    printf("\nIMPORTANT: The change applied by this tool is *not reversible* and\n"
            "           will erase all contents in mNAND.\n"
            "           Are you sure you want to proceed? (y/n) ");
    c = getchar();
    printf("\n");
    if (c == 'y' || c == 'Y') {
        printf("\nRECONFIRM: The change applied by this tool is *not reversible* and\n"
                "           will erase all contents in mNAND.\n"
                "           Abandon change? (y/n) ");
        do {
            c = getchar();
        } while (c == '\n');
        printf("\n");
        if (c == 'n' || c == 'N') {
            printf("Trimming the device ... ");
            fflush(stdout);
            gettimeofday(&t0, 0);
            res = mnand_trim_all(&info->chip);
            gettimeofday(&t1, 0);
            elapsed = (t1.tv_sec-t0.tv_sec)*1000000 + t1.tv_usec-t0.tv_usec;
            if (res == 0)
                printf("\nTrim Success : Time taken to complete: %.3f secs\n", elapsed/1000000);
            else
                printf("Failed\n");
        } else
            printf("Cancelled device trimming.\n");
    } else
        printf("Cancelled device trimming.\n");

    return res;
}

/* usage */
static int usage(void)
{
    printf("usage: mnand_hs -d <path to mnand device> [options]\n"
        "       -h      Show usage (this message)\n"
        "       -b      Include bad block table\n"
        "       -ah     Include age per block, hex mode\n"
        "       -ac     Include age per block, decimal mode, comma-separated output\n"
        "       -at     Include age totals (calculated by app)\n"
        "       -as     Include age summary (calculated by part)\n"
        "       -s      Include spare block summary\n"
        "       -st     Include card status\n"
        "       -ext    Include ext_csd register (raw)\n"
        "       -csd    Include csd register (raw)\n"
        "       -cid    Include cid register (raw)\n"
        "       -da     Include data area information (UDA/EUDA)\n"
        "       -eol    Include EOL status\n"
        "       -rp     Include reported/estimated refresh progress (if available)\n"
        "       -bkops  Include BKOPS_EN status\n"
        "       -bsts   Include BKOPS_STATUS\n"
        "       -bsta   Execute BKOPS_START\n"
        "       -bkops_set <0/1> Set BKOPS_EN\n"
        "       -ponen  Enable POWER_OFF notification\n"
        "       -ponsta Send POWER_OFF notification\n"
        "       -cache_en <0/1/2>  Enable mNAND cache.\n"
        "               0 - READ CACHE\n"
        "               1 - WRITE CACHE\n"
        "               2 - READ & WRITE CACHE\n"
        "               Default - 2(READ & WRITE CACHE)\n"
        "       -cache_disable <0/1/2> Disable mNAND cache\n"
        "               0 - READ CACHE\n"
        "               1 - WRITE CACHE\n"
        "               2 - READ & WRITE CACHE\n"
        "               Default - 2(READ & WRITE CACHE)\n"
        "       -cache_flush    Flush mNAND cache\n"
        "       -cache_status   mNAND cache status\n"
        "       -flag_rd <flag_idn> Read UFS Flag.\n"
        "               flag_idn - Flag identifier\n"
        "       -flag_wr <flag_idn> <flag_val> Write UFS Flag\n"
        "               flag_idn - Flag identifier\n"
        "               flag_val - Value to be set in the Flag\n"
        "       -attr_rd <attr_idn> Read UFS Attribute\n"
        "               attr_idn - Attribute identifier\n"
        "       -attr_wr <attr_idn> <attr_val> Write UFS Attribute\n"
        "               attr_idn - Attribute identifier\n"
        "               attr_val - Value to be set in the Attribute\n"
        "       -desc_rd <desc_idn> Read UFS Descriptor\n"
        "               desc_idn - Descriptor identifier\n"
        "       -ffu <fw_file> Field Firmware Update using this firmware file\n"
        "            The firmware file should follow below naming convention\n"
        "            <firmware image>_PRN_<product id/revision>_FWVER_<hex version string>.bin\n"
        "       -ffusts Get Field Firmware Update status\n"
        "       -fwver Get Firmware version of the UFS/EMMC device\n"
        "       -t      Trim the device\n"
        "       -r cnt  Refresh cnt blocks\n"
        "       -rut <time_us> Refresh unit time - time for single refresh \n"
    );

    return 1;
}

int main(int argc, char **argv)
{
    int skip = 0;
    char *devnode = NULL;
    health_status_info stat_info;
    health_status_info *info = &stat_info;

    printf("mNAND Health and Status Info\n");

    memset(info, 0, sizeof(health_status_info));

    /* Extract command line parameters */
    argc--;
    argv++;
    if (!argc)
        return usage();
    while(argc > 0) {
        skip = 1;
        if (!strcmp(argv[0], "-h"))
            return usage();
        else if (!strcmp(argv[0], "-d")) {
            if(argc < 2)
                return usage();
            devnode = argv[1];
            skip = 2;
        }
        else if (!strcmp(argv[0], "-b"))
            info->included_info |= HS_INFO_TYPE_BADBLOCKS;
        else if (!strcmp(argv[0], "-ah")) {
            info->included_info |= HS_INFO_TYPE_AGE;
            info->age_format = adf_hex_compact;
        }
        else if (!strcmp(argv[0], "-ac")) {
            info->included_info |= HS_INFO_TYPE_AGE;
            info->age_format = adf_dec_csv;
        }
        else if (!strcmp(argv[0], "-at"))
            info->included_info |= HS_INFO_TYPE_AGE_TOTALS;
        else if (!strcmp(argv[0], "-as"))
            info->included_info |= HS_INFO_TYPE_SUMMARY_AGE;
        else if (!strcmp(argv[0], "-s"))
            info->included_info |= HS_INFO_TYPE_SPARE_SUMMARY;
        else if (!strcmp(argv[0], "-st"))
            info->included_info |= HS_INFO_TYPE_CARD_STATUS;
        else if (!strcmp(argv[0], "-ext"))
            info->included_info |= HS_INFO_TYPE_EXT_CSD;
        else if (!strcmp(argv[0], "-csd"))
            info->included_info |= HS_INFO_TYPE_CSD;
        else if (!strcmp(argv[0], "-cid"))
            info->included_info |= HS_INFO_TYPE_CID;
        else if (!strcmp(argv[0], "-da"))
            info->included_info |= HS_INFO_TYPE_DATA_AREA;
        else if (!strcmp(argv[0], "-eol"))
            info->included_info |= HS_INFO_TYPE_EOL_STATUS;
        else if (!strcmp(argv[0], "-bsts"))
            info->included_info |= HS_INFO_TYPE_BKOPS_STATUS;
        else if (!strcmp(argv[0], "-bkops"))
                info->included_info |= HS_INFO_TYPE_GET_BKOPS_EN;
        else if (!strcmp(argv[0], "-bkops_set")) {
            if (argc < 2)
                return usage();
            info->bkops_en = strtoul(argv[1], NULL, 0);
            if (info->bkops_en == 0 || info->bkops_en == 1)
                info->included_info |= HS_INFO_TYPE_SET_BKOPS_EN;
            else {
                printf("Invalid argument to set BKOPS\n");
                return usage();
            }
            skip = 2;
        }
        else if (!strcmp(argv[0], "-bsta"))
            info->included_info |= HS_INFO_TYPE_BKOPS_START;
        else if (!strcmp(argv[0], "-t"))
            info->included_info |= HS_INFO_TYPE_TRIM_ALL;
        else if (!strcmp(argv[0], "-ponen"))
            info->included_info |= HS_INFO_TYPE_EN_POWER_OFF;
        else if (!strcmp(argv[0], "-ponsta"))
            info->included_info |= HS_INFO_TYPE_SEND_POWER_OFF;
        else if (!strcmp(argv[0], "-cache_en")) {
            if (argc > 1 && argv[1][0] != '-') {
                info->cache_en = strtoul(argv[1], NULL, 0);
                if (info->cache_en > 2)
                    return usage();
                skip = 2;
            } else
                info->cache_en = 2; /* Default enable Read and Write cache */

            info->included_info |= HS_INFO_TYPE_EN_CACHE;
        }
        else if (!strcmp(argv[0], "-cache_disable")) {
            if (argc > 1 && argv[1][0] != '-') {
                info->cache_disable = strtoul(argv[1], NULL, 0);
                 if (info->cache_disable > 2)
                    return usage();
                skip = 2;
            } else
                info->cache_disable = 2; /* Default : disable Rd and Wr cache */

            info->included_info |= HS_INFO_TYPE_DISABLE_CACHE;
        }
        else if (!strcmp(argv[0], "-cache_flush"))
            info->included_info |= HS_INFO_TYPE_FLUSH_CACHE;
        else if (!strcmp(argv[0], "-cache_status"))
            info->included_info |= HS_INFO_TYPE_CACHE_STATUS;
        else if (!strcmp(argv[0], "-rp"))
            info->included_info |= HS_INFO_TYPE_REFRESH_PROGRESS;
        else if (!strcmp(argv[0], "-r")) {
            if (argc < 2)
                return usage();
            info->blocks_to_refresh = strtoul(argv[1], NULL, 0);
            if (info->blocks_to_refresh)
                info->included_info |= HS_INFO_TYPE_REFRESHED_BLKS;
            skip = 2;
        }
        else if (!strcmp(argv[0], "-rut")) {
            if (argc < 2)
                return usage();
            info->rfsh_unit_time = strtoul(argv[1], NULL, 0);
            skip = 2;
        }
        else if (!strcmp(argv[0], "-flag_rd")) {
            if (argc < 2)
                return usage();
            info->included_info |= HS_INFO_TYPE_READ_FLAG;
            info->ufs.flag_idn = strtoul(argv[1], NULL, 0);
            skip = 2;
        }
        else if (!strcmp(argv[0], "-flag_wr")) {
            if (argc < 3)
                return usage();
            info->included_info |= HS_INFO_TYPE_WRITE_FLAG;
            info->ufs.flag_idn = strtoul(argv[1], NULL, 0);
            info->ufs.flag_val = strtoul(argv[2], NULL, 0);
            skip = 3;
        }
        else if (!strcmp(argv[0], "-attr_rd")) {
            if (argc < 2)
                return usage();
            info->included_info |= HS_INFO_TYPE_READ_ATTR;
            info->ufs.attr_idn = strtoul(argv[1], NULL, 0);
            skip = 2;
        }
        else if (!strcmp(argv[0], "-attr_wr")) {
            if (argc < 3)
                return usage();
            info->included_info |= HS_INFO_TYPE_WRITE_ATTR;
            info->ufs.attr_idn = strtoul(argv[1], NULL, 0);
            info->ufs.attr_val = strtoul(argv[2], NULL, 0);
            skip = 3;
        }
        else if (!strcmp(argv[0], "-desc_rd")) {
            if (argc < 2)
                return usage();
            info->included_info |= HS_INFO_TYPE_READ_DESC;
            info->ufs.desc_idn = strtoul(argv[1], NULL, 0);
            skip = 2;
        }
        else if (!strcmp(argv[0], "-ffu")){
            if(argc < 2)
                return usage();
            info->included_info |= HS_INFO_TYPE_FFU;
            info->ffu_file = argv[1];
            skip = 2;
        }
        else if (!strcmp(argv[0], "-ffusts"))
            info->included_info |= HS_INFO_TYPE_FFU_STATUS;
        else if (!strcmp(argv[0], "-fwver"))
            info->included_info |= HS_INFO_TYPE_FW_VERSION;
        else
            return usage();
        argc -= skip;
        argv += skip;
    }

    if (!info->included_info) {
        printf("No information was selected to display.\n");
        return 1;
    }

    if (mnand_open(devnode, &info->chip) != MNAND_OK) {
        printf("Failed to access/identify mNAND.\n");
        return 1;
    }

    if (mnand_get_storage_type(&info->chip) == MNAND_EMMC)
        printf("mNAND identified: \"%s\"\n", info->chip.desc);
    else
        printf("UFS identified: \"%s\"\n", info->chip.desc);

    /* Retrieve block data */
    if (info->included_info & (HS_INFO_TYPE_AGE | HS_INFO_TYPE_AGE_TOTALS |
        HS_INFO_TYPE_BADBLOCKS)) {
        if (mnand_is_block_info_available(&info->chip)) {
            if (mnand_update_block_info(&info->chip) != MNAND_OK) {
                printf("Error getting block data. Skipped.\n");
                info->included_info &= ~(HS_INFO_TYPE_AGE | HS_INFO_TYPE_AGE_TOTALS |
                    HS_INFO_TYPE_BADBLOCKS);
            }
        } else {
            printf("Block data not available.\n");
            info->included_info &= ~(HS_INFO_TYPE_AGE | HS_INFO_TYPE_AGE_TOTALS |
                HS_INFO_TYPE_BADBLOCKS);
        }
    }

    /* Retrieve summary information */
    if (info->included_info & (HS_INFO_TYPE_SUMMARY_AGE |
        HS_INFO_TYPE_SPARE_SUMMARY)) {
        if (mnand_is_summary_available(&info->chip)) {
            if (mnand_update_summary_info(&info->chip) != MNAND_OK) {
                printf("Error getting summary info-> Skipped.\n");
                info->included_info &= ~(HS_INFO_TYPE_SUMMARY_AGE |
                    HS_INFO_TYPE_SPARE_SUMMARY);
            }
        } else {
            printf("Summary not available.\n");
            info->included_info &= ~(HS_INFO_TYPE_SUMMARY_AGE |
                HS_INFO_TYPE_SPARE_SUMMARY);
        }
    }

    /* Get CID */
    if (info->included_info & HS_INFO_TYPE_CID) {
        if (mnand_update_cid(&info->chip) != MNAND_OK) {
            printf("Error updating CID. Skipped.\n");
            info->included_info &= ~HS_INFO_TYPE_CID;
        }
    }

    /* Get CSD */
    if (info->included_info & HS_INFO_TYPE_CSD) {
        if (mnand_update_csd(&info->chip) != MNAND_OK) {
            printf("Error updating CSD. Skipped.\n");
            info->included_info &= ~HS_INFO_TYPE_CSD;
        }
    }

    /* Get EXT CSD */
    if (info->included_info & (HS_INFO_TYPE_EXT_CSD | HS_INFO_TYPE_DATA_AREA)) {
        if (mnand_get_storage_type(&info->chip) == MNAND_EMMC)
            if (mnand_update_xcsd(&info->chip) != MNAND_OK) {
                printf("Error updating EXT CSD. Skipped.\n");
                info->included_info &= ~(HS_INFO_TYPE_EXT_CSD | HS_INFO_TYPE_DATA_AREA);
            }
    }

    /* Get Card status */
    if (info->included_info & HS_INFO_TYPE_CARD_STATUS) {
        if (mnand_update_card_status(&info->chip) != MNAND_OK) {
            printf("Error updating card status. Skipped.\n");
            info->included_info &= ~HS_INFO_TYPE_CARD_STATUS;
        }
    }

    /* Get EOL status */
    if (info->included_info & HS_INFO_TYPE_EOL_STATUS) {
        if (mnand_check_eol_status(&info->chip, &info->eol_status) != MNAND_OK) {
            printf("Error getting EOL status. Skipped.\n");
            info->included_info &= ~HS_INFO_TYPE_EOL_STATUS;
        }
    }

    /* Get current refresh progress */
    if (info->included_info & HS_INFO_TYPE_REFRESH_PROGRESS) {
        if (mnand_get_refresh_progress(&info->chip, &info->cur_rfsh_progress) != MNAND_OK) {
            printf("Refresh progress not available, Skipped.\n");
            info->included_info &= ~HS_INFO_TYPE_REFRESH_PROGRESS;
        }
    }

    /* Refresh block(s) */
    if (info->blocks_to_refresh) {
        if (mnand_is_refresh_available(&info->chip)) {
            if (mnand_send_refresh(&info->chip, MNAND_MLC_BLOCK,
                info->blocks_to_refresh, info->rfsh_unit_time, &info->rfsh_result) != MNAND_OK) {
                printf("Error refreshing block(s). Skipped.\n");
                info->included_info &= ~HS_INFO_TYPE_BKOPS_STATUS;
                info->blocks_to_refresh = 0;
                info->rfsh_unit_time = 0;

            }
        } else {
            printf("Refresh not available.\n");
            info->included_info &= ~HS_INFO_TYPE_BKOPS_STATUS;
            info->blocks_to_refresh = 0;
            info->rfsh_unit_time = 0;
        }
    }

    /* Get BKOPS status */
    if (info->included_info & HS_INFO_TYPE_BKOPS_STATUS) {
        MNAND_STATUS status;

        status = mnand_get_bkops_status(&info->chip, &info->bkops_status);
        if (status != MNAND_OK) {
            info->included_info &= ~HS_INFO_TYPE_BKOPS_STATUS;
            if (status == MNAND_EINVAL)
                printf("BKOPS_EN not set. BKOPS status skipped.\n");
            else
                printf("Error getting BKOPS status. Skipped.\n");
        }
    }

    if (info->included_info & HS_INFO_TYPE_SET_BKOPS_EN) {
        MNAND_STATUS status;

        status = mnand_set_bkops_en(&info->chip, info->bkops_en);
        if (status != MNAND_OK) {
            info->included_info &= ~HS_INFO_TYPE_SET_BKOPS_EN;
            printf("Error setting BKOPS_EN status. Skipped.\n");
        }
    }

    if (info->included_info & HS_INFO_TYPE_GET_BKOPS_EN) {
        MNAND_STATUS status;
        uint8_t enabled;

        status = mnand_get_bkops_en(&info->chip, &enabled);
        if (status != MNAND_OK) {
            info->included_info &= ~HS_INFO_TYPE_GET_BKOPS_EN;
            printf("Error getting BKOPS_EN status. Skipped.\n");
        } else
            info->bkops_en = enabled & 0x1;
    }

    /* BKOPS_START */
    if (info->included_info & HS_INFO_TYPE_BKOPS_START) {
        MNAND_STATUS status;

        status = mnand_set_bkops_start(&info->chip);
        if (status != MNAND_OK) {
            info->included_info &= ~HS_INFO_TYPE_BKOPS_START;
            if (status == MNAND_EINVAL)
                printf("BKOPS_EN not set. BKOPS_START skipped.\n");
            else
                printf("Error sending BKOPS_START.\n");
        }
    }

    if (info->included_info & HS_INFO_TYPE_READ_FLAG) {
        MNAND_STATUS status;
        mnand_ufs_query_req query_req = {0};

        query_req.opcode = 5; /* Read Flag */
        query_req.idn = info->ufs.flag_idn;
        query_req.index = 0;
        query_req.selector = 0;
        query_req.buf_size = sizeof(uint8_t);
        query_req.buffer = &info->ufs.flag_val;

        status = mnand_send_combo_query_req(&info->chip, &query_req, 1, 0, 0);
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_WRITE_FLAG) {
        MNAND_STATUS status;
        mnand_ufs_query_req query_req = {0};

        if (info->ufs.flag_val == 0)
            query_req.opcode = 7; /* Clear Flag */
        else
            query_req.opcode = 6; /* Set Flag */
        query_req.idn = info->ufs.flag_idn;
        query_req.index = 0;
        query_req.selector = 0;
        query_req.buf_size = 0;
        query_req.buffer = NULL;

        status = mnand_send_combo_query_req(&info->chip, &query_req, 1, 0, 0);
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_READ_ATTR) {
        MNAND_STATUS status;
        mnand_ufs_query_req query_req = {0};

        query_req.opcode = 3; /* Read Attr */
        query_req.idn = info->ufs.attr_idn;
        query_req.index = 0;
        query_req.selector = 0;
        query_req.buf_size = 4;
        query_req.buffer = (uint8_t *)&info->ufs.attr_val;

        status = mnand_send_combo_query_req(&info->chip, &query_req, 1, 0, 0);
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_WRITE_ATTR) {
        MNAND_STATUS status;
        mnand_ufs_query_req query_req = {0};

        query_req.opcode = 4; /* Write Attr */
        query_req.idn = info->ufs.attr_idn;
        query_req.index = 0;
        query_req.selector = 0;
        query_req.buf_size = 4;
        query_req.buffer = (uint8_t *)&info->ufs.attr_val;

        status = mnand_send_combo_query_req(&info->chip, &query_req, 1, 0, 0);
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_READ_DESC) {
        MNAND_STATUS status;
        mnand_ufs_query_req query_req = {0};

        query_req.opcode = 1; /* Read Desc */
        query_req.idn = info->ufs.desc_idn;
        query_req.index = 0;
        query_req.selector = 0;
        query_req.buf_size = MAX_DESC_BUF_SIZE;
        query_req.buffer = (uint8_t *)&info->ufs.desc_buf;

        status = mnand_send_combo_query_req(&info->chip, &query_req, 1, 0, 0);
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_FFU) {
        MNAND_STATUS status;
	MNAND_FW_COMPARE res;

        status = mnand_compare_fw_version(&info->chip, info->ffu_file, &res);
        if (status != MNAND_OK) {
            if (status == MNAND_EINVAL)
                printf("FW version comparison is skipped.\n");
            else
                printf("Error comparing FW version. Skipped.\n");
        } else {
		printf("\nThe Firmware version comparison result is: %d\n", res);
	}

        status = mnand_ffu(&info->chip, info->ffu_file);
        if (status != MNAND_OK)
            printf("Not able to update firmware: %d\n", status);
        else
            printf("Updated firmware Successfully\n");
    }

    /* Get FFU status */
    if (info->included_info & HS_INFO_TYPE_FFU_STATUS) {
        MNAND_STATUS status;

        status = mnand_get_ffu_status(&info->chip, &info->ffu_status);
        if (status != MNAND_OK) {
            info->included_info &= ~HS_INFO_TYPE_FFU_STATUS;
            if (status == MNAND_EINVAL)
                printf("FFU status skipped.\n");
            else
                printf("Error getting FFU status. Skipped.\n");
        }
	printf("The FFU status is %u\n", info->ffu_status);
    }

    /* Get Firmware version */
    if (info->included_info & HS_INFO_TYPE_FW_VERSION) {
        MNAND_STATUS status;

        status = mnand_get_fw_version(&info->chip, info->fw_version);
        if (status != MNAND_OK) {
            info->included_info &= ~HS_INFO_TYPE_FW_VERSION;
            if (status == MNAND_EINVAL)
                printf("FW version is skipped.\n");
            else
                printf("Error getting FW version. Skipped.\n");
        }
	printf("\nThe Firmware version is: ");
	printf("(%s)", info->fw_version);
	printf("\n");
    }

    /* Dump it all */
    dump_info(info);

    /* Device trim */
    if (info->included_info & HS_INFO_TYPE_TRIM_ALL) {
        trim_all(info);
     }


    if (info->included_info & HS_INFO_TYPE_EN_POWER_OFF) {
        MNAND_STATUS status;

        printf("Enabling device power off notification... ");
        fflush(stdout);
        status = mnand_enable_power_off_notification(&info->chip);
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_SEND_POWER_OFF) {
        MNAND_STATUS status;

        printf("Sending device power off notification... ");
        fflush(stdout);
        status = mnand_send_power_off_notification(&info->chip);
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_EN_CACHE) {
        MNAND_STATUS status;
        uint8_t cache_enabled;

        printf("Enabling mNAND cache... ");
        fflush(stdout);
        status = mnand_get_cache_en(&info->chip, &cache_enabled);

        if (status == MNAND_OK) {
            if (info->cache_en == 0)
                cache_enabled |= MNAND_READ_CACHE_ENABLE; /* Enable Rd Cache */
            else if (info->cache_en == 1)
                cache_enabled |= MNAND_WRITE_CACHE_ENABLE; /* Enable Wr Cache */
            else if (info->cache_en == 2)
                cache_enabled = MNAND_READ_CACHE_ENABLE |
                    MNAND_WRITE_CACHE_ENABLE; /* Enable Rd & Wr Cache */

            status = mnand_set_cache_en(&info->chip, cache_enabled);
        }

        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_DISABLE_CACHE) {
        MNAND_STATUS status;
        uint8_t cache_enabled;

        printf("Disabling mNAND cache... ");
        fflush(stdout);

        status = mnand_get_cache_en(&info->chip, &cache_enabled);

        if (status == MNAND_OK) {
            if (info->cache_disable == 0)
                cache_enabled &=
                    (~MNAND_READ_CACHE_ENABLE); /* Disable Rd Cache */
            else if (info->cache_disable == 1)
                cache_enabled &=
                    (~MNAND_WRITE_CACHE_ENABLE); /* Disable Wr Cache */
            else if (info->cache_disable == 2)
                cache_enabled &= ~(MNAND_READ_CACHE_ENABLE |
                    MNAND_WRITE_CACHE_ENABLE); /* Disable Rd & Wr Cache */

            status = mnand_set_cache_en(&info->chip, cache_enabled);

        }

        status =
            mnand_set_cache_en(&info->chip, cache_enabled); /* Disable CACHE */

        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    if (info->included_info & HS_INFO_TYPE_CACHE_STATUS) {
        uint8_t cache_enabled;
        MNAND_STATUS status;

        printf("Getting mNAND cache status... \n");
        fflush(stdout);

        status = mnand_get_cache_en(&info->chip, &cache_enabled);
        if (status == MNAND_OK) {
            if (cache_enabled & MNAND_READ_CACHE_ENABLE)
                printf("Read Cache Enabled\n");
            else
                printf("Read Cache Disabled\n");

            if (cache_enabled & MNAND_WRITE_CACHE_ENABLE)
                printf("Write Cache Enabled\n");
            else
                printf("Write Cache Disabled\n");
        }  else
            printf("Error getting cache enabled state.\n");
    }

    if (info->included_info & HS_INFO_TYPE_FLUSH_CACHE) {
        MNAND_STATUS status;

        printf("Flushing mNAND cache... ");
        fflush(stdout);
        status = mnand_flush_cache(&info->chip); /* CACHE_CTRL */
        if (status != MNAND_OK)
            printf("Failed\n");
        else
            printf("Success\n");
    }

    /* Free things up */
    mnand_close(&info->chip);

    return 0;
}
