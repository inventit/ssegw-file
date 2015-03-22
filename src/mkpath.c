/*
 * LEGAL NOTICE
 *
 * Copyright (C) 2012-2013 InventIt Inc. All rights reserved.
 *
 * This source code, product and/or document is protected under licenses 
 * restricting its use, copying, distribution, and decompilation.
 * No part of this source code, product or document may be reproduced in
 * any form by any means without prior written authorization of InventIt Inc.
 * and its licensors, if any.
 *
 * InventIt Inc.
 * 9F KOJIMACHI CP BUILDING
 * 4-4-7 Kojimachi, Chiyoda-ku, Tokyo 102-0083
 * JAPAN
 * http://www.yourinventit.com/
 */

/*!
 * @file    mkpath.c
 * @brief   ディレクトリ作成ユーティリティ
 *
 *          ディレクトリを再帰的に作成するためのユーティリティ
 */

#include <servicesync/moat.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include "mkpath.h"

enum FileMkpathMakeStat_ {
	FILE_MKPATH_MAKESTAT_MADE_DIR,
	FILE_MKPATH_MAKESTAT_DIR_EXIST,
	FILE_MKPATH_MAKESTAT_ERR_ROFS,
	FILE_MKPATH_MAKESTAT_ERR_OTHERS
};
typedef enum FileMkpathMakeStat_ FileMkpathMakeStat;

enum FileMkpathLastErr_ {
	FILE_MKPATH_LASTERR_NOERROR = 0,
	FILE_MKPATH_LASTERR_EROFS,
	FILE_MKPATH_LASTERR_OTHERS
};
typedef enum FileMkpathLastErr_ FileMkpathLastErr;

struct FileMkpath_ {
	FileMkpathLastErr LastError;
};

#define MKPATH_TAG						"mkpath"

#if defined(SSE_LOG_USE_SYSLOG)
#define LOG_MKPATH_ERR(format, ...)			SSE_SYSLOG_ERROR(MKPATH_TAG, format, ##__VA_ARGS__)
#define LOG_MKPATH_DBG(format, ...)			SSE_SYSLOG_DEBUG(MKPATH_TAG, format, ##__VA_ARGS__)
#else
#define LOG_MKPATH_ERR(format, ...)
#define LOG_MKPATH_DBG(format, ...)
#endif

/**
 * @brief	ステータス文字列取得
 *　			ステータスコードに該当する文字列を返却する。
 *
 * @param	stat ステータスコード
 * @return	ステータス文字列
 */
static sse_char *
mkpath_get_status_string(FileMkpathMakeStat stat)
{
	sse_char *p = NULL;

	switch (stat) {
	case FILE_MKPATH_MAKESTAT_MADE_DIR:
		p = "ok: made dir.";
		break;
	case FILE_MKPATH_MAKESTAT_DIR_EXIST:
		p = "ok: dir is already exist.";
		break;
	case FILE_MKPATH_MAKESTAT_ERR_ROFS:
		p = "error: read only file system.";
		break;
	case FILE_MKPATH_MAKESTAT_ERR_OTHERS:
		p = "error: happens other issue.";
		break;
	default:
		p = "error: unknown args.";
		break;
	}
	return p;
}

/**
 * @brief	ディレクトリ作成処理
 *　			ディレクトリを作成する。
 *			既にディレクトリが存在する場合はエラーとしない。
 *			既に同名のファイルが存在する場合はエラーとする。
 *
 * @param	in_path ディレクトリのパス
 * @param	mode mkdirのモード
 * @param	out_status ステータスコード
 * @return	処理結果
 */
static sse_int
mkpath_do_mkdir(const char *in_path, mode_t mode, FileMkpathMakeStat *out_status)
{
	struct stat st;
	sse_int err = SSE_E_OK;
	int rtn = -1;
	int err_no = 0;

	if (in_path == NULL || out_status == NULL) {
		LOG_MKPATH_ERR("invalid args.");
		return SSE_E_INVAL;
	}

	rtn = stat(in_path, &st);
	LOG_MKPATH_DBG("stat() args: %s, result %d", in_path, rtn);

	*out_status = FILE_MKPATH_MAKESTAT_DIR_EXIST;
	if (rtn != 0) {
		errno = 0; //init
		rtn = mkdir(in_path, mode);
		err_no = errno;
		LOG_MKPATH_DBG("mkdir() args: %s, result %d", in_path, rtn);

		if (rtn != 0 && err_no != EEXIST) {
			LOG_MKPATH_ERR("midir() failed. errno =  %d", err_no);
			if (err_no == EROFS) {
				*out_status = FILE_MKPATH_MAKESTAT_ERR_ROFS;
			} else {
				*out_status = FILE_MKPATH_MAKESTAT_ERR_OTHERS;
			}
			err = SSE_E_GENERIC;
		} else {
			*out_status = FILE_MKPATH_MAKESTAT_MADE_DIR;
		}
	} else if (!S_ISDIR(st.st_mode)) {
		LOG_MKPATH_ERR("found same name file as target dir. err");
		*out_status = FILE_MKPATH_MAKESTAT_ERR_OTHERS;
		err = SSE_E_GENERIC;
	}

	LOG_MKPATH_DBG("*** result: %s***", mkpath_get_status_string(*out_status));
	return err;
}

/**
 * @brief	ReadOnlyエラー判定
 *　			最後に発生したエラーがReadOnlyによるものか判定する。
 *
 * @param	in_mkpath ディレクトリ作成オブジェクト
 * @param	result 判定結果
 * @return	処理結果
 */
sse_int
mkpath_is_lasterr_rofs(FileMkpath *in_mkpath, sse_bool *result)
{

	if (in_mkpath == NULL || result == NULL) {
		LOG_MKPATH_ERR("invalid.args");
		return SSE_E_INVAL;
	}

	if (in_mkpath->LastError == FILE_MKPATH_LASTERR_EROFS) {
		*result = sse_true;
	} else {
		*result = sse_false;
	}
	return SSE_E_OK;
}

/**
 * @brief	再帰ディレクトリ作成処理
 *　			再帰的にディレクトリを作成する。
 *
 * @param	in_mkpath ディレクトリ作成オブジェクト
 * @param	in_path 作成するディレクトリのパス
 * @param	mode mkdirのモード
 * @return	処理結果
 */
sse_int
mkpath_make(FileMkpath *in_mkpath, const sse_char *in_path, mode_t mode)
{
	FileMkpath *p = in_mkpath;
	sse_char *pp = NULL;
	sse_char *sp = NULL;
	sse_int err = SSE_E_OK;
	FileMkpathMakeStat mkdir_status;
	sse_char *copypath = NULL;

	if (p == NULL || in_path == NULL) {
		LOG_MKPATH_ERR("invalid args");
		return SSE_E_INVAL;
	}

	in_mkpath->LastError = FILE_MKPATH_LASTERR_NOERROR;
	LOG_MKPATH_DBG("args: target path = %s, mode = %04o", in_path, mode);

	copypath = strdup(in_path);
	if (copypath == NULL) {
		LOG_MKPATH_ERR("nomem");
		in_mkpath->LastError = FILE_MKPATH_LASTERR_OTHERS;
		return SSE_E_GENERIC;
	}

	pp = copypath;
	while (err == SSE_E_OK && (sp = strchr(pp, '/')) != 0) {
		if (sp != pp) {
			*sp = '\0';
			err = mkpath_do_mkdir(copypath, mode, &mkdir_status);
			*sp = '/';
		}
		pp = sp + 1;
	}
	if (!err) {
		err = mkpath_do_mkdir(in_path, mode, &mkdir_status);
	}

	if (err) {
		if (mkdir_status == FILE_MKPATH_MAKESTAT_ERR_ROFS) {
			LOG_MKPATH_ERR("error reason: Read only file system");
			in_mkpath->LastError = FILE_MKPATH_LASTERR_EROFS;
		} else {
			LOG_MKPATH_ERR("error resason: unknown");
			in_mkpath->LastError = FILE_MKPATH_LASTERR_OTHERS;
		}
	}

	sse_free(copypath);
	LOG_MKPATH_DBG("@@@ result @@@");
	LOG_MKPATH_DBG("  return value: %d", err);
	LOG_MKPATH_DBG("  last error: %d", in_mkpath->LastError);
	return err;

}

/**
 * @brief	ディレクトリ作成オブジェクト生成
 *　			ディレクトリ作成オブジェクトを生成する。
 *
 * @return	ディレクトリ作成オブジェクト
 */
FileMkpath *
mkpath_new(void)
{
	FileMkpath *mkpath = NULL;

	mkpath = sse_malloc(sizeof(FileMkpath));
	if (mkpath != NULL) {
		mkpath->LastError = FILE_MKPATH_LASTERR_NOERROR;
	}
	return mkpath;
}

/**
 * @brief	ディレクトリ作成オブジェクト解放
 *　			ディレクトリ作成オブジェクトを解放する。
 *
 * @param	ディレクトリ作成オブジェクト
 */
void
mkpath_fini(FileMkpath *in_mkpath)
{
	if (in_mkpath != NULL) {
		sse_free(in_mkpath);
	}
}
