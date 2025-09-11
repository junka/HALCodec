/*
 * Copyright (c) 2013-2022 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include "nvmnand.h"
#include "nvmnand_part_ops.h"

/*
 * List of available mnand_ops, each one binds to different kind
 * of mNAND chip. The last one is the "catch-all" type.
 */

extern mnand_operations mnand_f20_ops;  /* Hynix F20 */
extern mnand_operations mnand_f26_ops;  /* Hynix F26 */
extern mnand_operations mnand_micron_ops;  /* Micron */
extern mnand_operations mnand_samsung_ops;  /* Samsung */
extern mnand_operations mnand_toshiba_ops;  /* Toshiba */
extern mnand_operations mnand_ufs_toshiba_ops;  /* Toshiba UFS */
extern mnand_operations mnand_ufs_samsung_ops;  /* Samsung UFS */
extern mnand_operations mnand_ufs_micron_ops;  /* Micron UFS */
extern mnand_operations mnand_ca_ops;   /* Generic, catch-all type */

/**
 *  the list of available operation blocks (one per mNAND)
 */
mnand_operations *mnand_ops_list[] = {
    &mnand_micron_ops,
    &mnand_toshiba_ops,
    &mnand_samsung_ops,
    &mnand_f20_ops,
    &mnand_f26_ops,
    &mnand_ufs_toshiba_ops,
    &mnand_ufs_samsung_ops,
    &mnand_ufs_micron_ops,
    &mnand_ca_ops, /* should be the last one */
    NULL,          /* as sentinel */
};
