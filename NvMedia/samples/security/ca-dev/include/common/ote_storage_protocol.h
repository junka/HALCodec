/*
 * Copyright (c) 2013-2018, NVIDIA CORPORATION. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __OTE_STORAGE_PROTOCOL_H
#define __OTE_STORAGE_PROTOCOL_H

/*
 * Internal secure storage protocol declaration.
 */
enum {
	OTE_STORAGE_NS_CONFIG		= 0x1,
	OTE_STORAGE_GET_STATUS		= 0x90,
	OTE_STORAGE_FILE_CREATE		= 0x100,
	OTE_STORAGE_FILE_DELETE		= 0x101,
	OTE_STORAGE_FILE_OPEN		= 0x102,
	OTE_STORAGE_FILE_CLOSE		= 0x103,
	OTE_STORAGE_FILE_READ		= 0x104,
	OTE_STORAGE_FILE_WRITE		= 0x105,
	OTE_STORAGE_FILE_GET_SIZE	= 0x106,
	OTE_STORAGE_FILE_SEEK		= 0x107,
	OTE_STORAGE_FILE_TRUNC		= 0x108,
	OTE_STORAGE_FILE_TEST_EXIST	= 0x109,
	OTE_STORAGE_FILE_GET_NAME	= 0x10a,
	OTE_STORAGE_FILE_SYNC		= 0x10b,
	OTE_STORAGE_FILE_FLAGS		= 0x10c,
	OTE_STORAGE_RPMB_READ		= 0x1001,
	OTE_STORAGE_RPMB_WRITE		= 0x1002,
	OTE_STORAGE_CPC_IO		= 0x1011,
	OTE_STORAGE_RESET_FS		= 0x2000,
};

/* ss directory name */
#define SS_DIR_NAME "ss"

/* configuration flags */
#define OTE_STORAGE_NS_CONFIG_FLAGS_RPMB_AVAILABLE	(1 << 0)
#define OTE_STORAGE_NS_CONFIG_FLAGS_CPC_AVAILABLE	(1 << 1)
#define OTE_STORAGE_NS_CONFIG_FLAGS_USE_1K_BLOCKS	(1 << 2)

#define OTE_MAX_DIR_NAME_LEN	(64)
#define OTE_MAX_FILE_NAME_LEN	(128)
#define OTE_MAX_DATA_SIZE	(8192)

#define DAEMON_FLAG_OP_GET_GLOBAL	0
#define DAEMON_FLAG_OP_SET_GLOBAL	1
#define DAEMON_FLAG_OP_SET_FILE  	2
#define DAEMON_FLAG_OP_SYNC		3

/* global daemon flags (apply to all files) */
#define DAEMON_GFLAG_NONE		0x0000
#define DAEMON_GFLAG_FSYNC		0x0001
#define DAEMON_GFLAG_FDATASYNC		0x0002
/* file and global flags */
#define DAEMON_GFLAG_FILE_O_DIRECT	0x0010
#define DAEMON_GFLAG_FILE_O_SYNC	0x0020
#define DAEMON_GFLAG_FILE_O_DSYNC	0x0040
#define DAEMON_GFLAG_FILE_O_APPEND	0x0080
#define DAEMON_GFLAG_FILE_O_ASYNC	0x0100
#define DAEMON_GFLAG_FILE_O_NOATIME	0x0200
#define DAEMON_GFLAG_FILE_O_NONBLOCK	0x0400

typedef struct {
	char 		dname[OTE_MAX_DIR_NAME_LEN];
	char 		fname[OTE_MAX_FILE_NAME_LEN];
	uint32_t	flags;
} ote_file_create_params_t;

typedef struct {
	char 		dname[OTE_MAX_DIR_NAME_LEN];
	char 		fname[OTE_MAX_FILE_NAME_LEN];
} ote_file_delete_params_t;

typedef struct {
	char 		dname[OTE_MAX_DIR_NAME_LEN];
	char 		fname[OTE_MAX_FILE_NAME_LEN];
	uint32_t	flags;
	uint32_t	handle;
} ote_file_open_params_t;

typedef struct {
	uint32_t	handle;
} ote_file_close_params_t;

typedef struct {
	uint32_t	handle;
	uint32_t	data_size;
	char		data[OTE_MAX_DATA_SIZE];
} ote_file_write_params_t;

typedef struct {
	uint32_t	handle;
	uint32_t	data_size;
	char		data[OTE_MAX_DATA_SIZE];
} ote_file_read_params_t;

typedef struct {
	uint32_t	handle;
	uint32_t	size;
} ote_file_get_size_params_t;

typedef struct {
	uint32_t	handle;
	int32_t		offset;
} ote_file_seek_params_t;

typedef struct {
	uint32_t	handle;
	uint32_t	length;
} ote_file_trunc_params_t;

typedef struct {
	char 		dname[OTE_MAX_DIR_NAME_LEN];
	char 		fname[OTE_MAX_FILE_NAME_LEN];
} ote_file_test_exist_params_t;

typedef struct {
	uint32_t	handle;
} ote_file_sync_params_t;

typedef struct {
	uint32_t	handle;
	uint32_t	opcode;
	uint32_t	flags;
} ote_file_flag_params_t;

/* Lookup Nth non-secure side file name from given file system type.
 * The order of names does not change, but they may not be sorted in any
 * expected way (i.e. they are in directory order, not sorted alphabetically).
 *
 * If the given file system contains non-secure side directories, they will
 * get traversed (according to file system specification). This applies currenlty
 * only to old format directories.
 *
 * If filesystem does not contain non-secure side directories: dname[0] = '\000'
 */
#define FS_TYPE_0	0
#define FS_TYPE_1	1
#define FS_TYPE_OLD     2

typedef struct {
	uint32_t	fs_type;	// type of filesystem
	uint32_t	index;		// get Nth name
	char 		dname[OTE_MAX_DIR_NAME_LEN];
	char 		fname[OTE_MAX_FILE_NAME_LEN];
} ote_file_get_name_params_t;

/* size in bytes of RPMB frame */
#define OTE_RPMB_FRAME_SIZE	512

typedef struct {
	uint8_t		req_frame[OTE_RPMB_FRAME_SIZE];
	uint8_t		req_resp_frame[OTE_RPMB_FRAME_SIZE];
	uint8_t		resp_frame[OTE_RPMB_FRAME_SIZE];
} ote_rpmb_write_params_t;

typedef struct {
	uint8_t		req_frame[OTE_RPMB_FRAME_SIZE];
	uint8_t		resp_frame[OTE_RPMB_FRAME_SIZE];
} ote_rpmb_read_params_t;

/* size (with some padding) in bytes of CPC frame */
#define OTE_CPC_FRAME_SIZE	128

typedef struct {
	uint8_t		frame[OTE_CPC_FRAME_SIZE];
} ote_cpc_io_params_t;

typedef union {
	ote_file_create_params_t	f_create;
	ote_file_delete_params_t	f_delete;
	ote_file_open_params_t		f_open;
	ote_file_close_params_t		f_close;
	ote_file_read_params_t		f_read;
	ote_file_write_params_t		f_write;
	ote_file_get_size_params_t	f_getsize;
	ote_file_seek_params_t		f_seek;
	ote_file_trunc_params_t		f_trunc;
	ote_file_test_exist_params_t	f_test_exist;
	ote_file_get_name_params_t	f_get_name;
	ote_file_sync_params_t		f_sync;
	ote_file_flag_params_t		f_flags;
	ote_rpmb_read_params_t		f_rpmb_read;
	ote_rpmb_write_params_t		f_rpmb_write;
	ote_cpc_io_params_t		f_cpc_io;
} ote_file_req_params_t;

#define OTE_STORAGE_PROTOCOL_MAGIC	(0xfeedbeefcafebabe)
/*
 * Holds a parameter block that is exchanged on each file system operation
 * request.
 */
typedef struct {
	uint64_t		magic;
	uint32_t		req_size;
	uint32_t		type;
	te_error_t		result;
	uint32_t		params_size;
	ote_file_req_params_t	params;
} ote_ss_req_t;

#endif
