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
 */

/*!
 * @file	file.c
 * @brief	ファイル操作 MOATアプリケーション
 *
 *			以下の機能を実現するMOATアプリケーション。
 *			- ファイル取得/配信機能
 *			  サーバからの要求を受け、通常ファイルの取得(アップロード)、配信(ダウンロード)を行う機能
 *			- 設定ファイル取得/配信機能
 *			  サーバからの要求を受け、設定ファイルの取得(アップロード)、配信(ダウンロード)/反映を行う機能
 *			- syslog取得機能
 *			  サーバからの要求を受け、指定された/var/log/messages.*の内容を結合し、取得(アップロード)する機能
 */


#include <servicesync/moat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <syslog.h>
#include <libgen.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <ev.h>
#include "mkpath.h"

/* model name */
#define MDL_NAME_CONTENTINF			"ContentInfo"
#define MDL_NAME_CONFIGINF				"ConfigurationInfo"
#define MDL_NAME_SYSLOGINF				"SyslogInfo"

/* keys for datastore */
#define MDL_KEY_CONTENTINF			MDL_NAME_CONTENTINF
#define MDL_KEY_CONFIGINF			MDL_NAME_CONFIGINF
#define MDL_KEY_SYSLOGINF			MDL_NAME_SYSLOGINF

/** Common **/
#define MDL_FILEDL_TEMPFILE			"/tmp/pkg_file_tmpfile"
/* shell command*/
#define MDL_FILEDL_SH_REMOUNT_RW		"mount -o remount,rw /"
#define MDL_FILEDL_SH_REMOUNT_RO		"mount -o remount,ro /"

/** ContentInfo **/
/* used filed */
#define MDL_CONTENT_DELIVERY			"deliveryUrl"
#define MDL_CONTENT_UPLOAD				"uploadUrl"
#define MDL_CONTENT_NAME				"name"
#define MDL_CONTENT_DESTINATION		"destinationPath"
#define MDL_CONTENT_SOURCE				"sourcePath"
/* service id */
#define MDL_CONTENT_SERVICEID_DELIVERY	"deliver-file-result"
#define MDL_CONTENT_SERVICEID_FETCH	"fetch-file-result"

/** ConfigrationInfo **/
/* used filed */
#define MDL_CONFIG_DELIVERY			"deliveryUrl"
#define MDL_CONFIG_UPLOAD				"uploadUrl"
/* service id */
#define MDL_CONFIG_SERVICEID_DELIVERY	"deliver-config-result"
#define MDL_CONFIG_SERVICEID_FETCH		"fetch-config-result"
/* fixed info */
#define MDL_CONFIG_FIXED_CONFNAME		"/etc/config.xml"
/* shell command*/
#define MDL_CONFIG_SH_APPLY			"config save"
#define MDL_CONFIG_SH_REBOOT			"reboot"
/* leave file */
#define MDL_KEY_CONFIG_LEAVE_INF		"ConfigResult"
/* other macro*/
#define ADD_INC(idx, inc) 				(idx+=inc)
#define MDL_FILE_MAXTOKENS				64

/** SyslogInfo **/
/* used filed */
#define MDL_SYSLOG_UPLOAD				"uploadUrl"
#define MDL_SYSLOG_MAXLOG				"maxLogs"
/* service id */
#define MDL_SYSLOG_SERVICEID_FETCH		"fetch-syslogs-result"
/* fixed uploading file info */
#define MDL_SYSLOG_FIXED_LOGPATH		"/var/log/messages"
#define MDL_SYSLOG_FIXED_UPFILE		"/tmp/logtemp"		/* all syslog files are combined into one file as this name */
/* syslog arguments */
#define MDL_SYSLOG_FACIRITY			LOG_USER
#define MDL_SYSLOG_PRIORITY			LOG_NOTICE
/* searching restriction */
#define AVAILABLE_SEARCH_GENERATION	128
#define MAX_SIZE_OF_READ_BUFFER		1024*4
/* target marker */
#define MDL_SYSLOG_TARGETMARKER		"SERVICESYNC_PKG_FILE_SYSLOGMARKER"

/*! HTTPの方向 */
enum HttpUseDirection_ {
	HTTP_USE_DIRECTION_DOWNLOAD,
	HTTP_USE_DIRECTION_UPLOAD
};
typedef enum HttpUseDirection_ HttpUseDirection;

/*! HTTPダウンロードコンテンツの保存タイプ */
enum HttpDlSaveType_ {
	HTTPDL_SAVE_TEMPORARY,
	HTTPDL_SAVE_DIRECTORY
};
typedef enum HttpDlSaveType_ HttpDlSaveType;

/*! syslogのソート方向 */
enum SlogSortType_ {
	SYSLOG_SORT_ASCENDING,
	SYSLOG_SORT_DESCENDING
};
typedef enum SlogSortType_ SlogSortType;

/*! syslogファイル情報型 */
struct SlogFileInf_ {
	sse_char *fname; /* null terminate, and needs release heap after 							*/
	sse_uint size;
	sse_uint position; /* file position																*/
/* value:																	*/
/* 		 0: whole file (file pointer should set top)						*/
/* 		 other positive number: start position of file pointer 				*/
};
typedef struct SlogFileInf_ SlogFileInf;

/*! コンテンツデータ転送情報型 */
struct FileAppTransfer_ {
//	sse_char *uid;
	sse_char *AsyncKey; /* save pointer to key string which is copied from receiving key at command case */
	MoatHttpClient *Http; /* instance of http client. its create when access s3 */
	sse_char *Path_from; /* this means application get target file from this URL or local path */
	sse_char *Path_to; /* this means application put target file to this URL or local path */
	sse_char *tmpfile; /* this means application download it as temporary, then copy any path */
	HttpDlSaveType save_type; /* indicate downloaded file is placed distination path or temporary */
	HttpUseDirection direction; /* use type (download or upload) */
};
typedef struct FileAppTransfer_ FileAppTransfer;

/*! ファイル操作アプリのコンテキストデータ型 */
struct FileAppContext_ {
	Moat Moat; /* [need release] moat object. instanse should free by moat_fini() 			*/
	sse_char *AppID; /* [need release] set addres to app id is same as string of argv[0] 		*/
	sse_char *ModelName; /* [need release] set const address to model name. 							*/
	sse_char *SaveKey; /* [need release] set fine name using object collection saving/loadding 	*/
	sse_char *ServiceID; /* [need release] set address to service id string.							*/
	MoatObject *ModelObj; /* [need release] save attribute field and value as Model Object structure 	*/
	FileAppTransfer *transfer; /* [need release] transfer object */
};
typedef struct FileAppContext_ FileAppContext;

/*! サーバへの通知引数リスト */
static const sse_char *notfy_arglist[] = { "urn", "key", "modelname", NULL };

#define FLTRANS_TAG						"file"

#if defined(SSE_LOG_USE_SYSLOG)
#define LOG_FL_ERR(format, ...)			SSE_SYSLOG_ERROR(FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_FL_DBG(format, ...)			SSE_SYSLOG_DEBUG(FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_FL_INF(format, ...)			SSE_SYSLOG_INFO(FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_FL_TRC(format, ...)			SSE_SYSLOG_TRACE(FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_IN()							SSE_SYSLOG_TRACE(FLTRANS_TAG, "enter")
#define LOG_OUT()							SSE_SYSLOG_TRACE(FLTRANS_TAG, "exit")
#else
#define LOG_PRINT(type, tag, format, ...)	printf("[" type "]" tag " %s():L%d " format "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_FL_ERR(format, ...)				LOG_PRINT("**ERROR**", FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_FL_DBG(format, ...)				LOG_PRINT("DEBUG", FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_FL_INF(format, ...)				LOG_PRINT("INFO", FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_FL_TRC(format, ...)				LOG_PRINT("TRACE", FLTRANS_TAG, format, ##__VA_ARGS__)
#define LOG_IN()							LOG_PRINT("TRACE", FLTRANS_TAG, "enter")
#define LOG_OUT()							LOG_PRINT("TRACE", FLTRANS_TAG, "exit")
#endif

/* static関数宣言 */
static MoatObject *
fileapp_create_fileresult_obj(sse_int status_code);
static sse_char *
fileapp_str_cat(const sse_char *s1, const sse_char *s2);

static void
fileapp_context_reset(FileAppContext *self);
static sse_int
handle_transfer_io_ready_cb(sse_int in_event_id, sse_pointer in_data,
		sse_uint in_data_length, sse_pointer in_user_data);

/////////////////////// Debug API ///////////////////////

static void
print_object(sse_char *in_method_name, MoatObject *in_object);
#define LOG_OBJ(method, obj)						print_object(method, obj);


/**
 * @brief	MoatValueの内容表示
 *　			入力されたMoatValueのインスタンスの内容を表示する。
 *
 * @param	in_method_name メソッド名
 * @param   in_field_name フィールド名
 * @param	in_value 表示するMoatValueのインスタンス
 */
static void
print_value(sse_char *in_method_name, sse_char *in_field_name,
		MoatValue *in_value)
{
	moat_value_type type;

	type = moat_value_get_type(in_value);
	switch (type) {
	case MOAT_VALUE_TYPE_BOOLEAN: {
		sse_bool value;
		moat_value_get_boolean(in_value, &value);
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], boolean=[%s]",
				in_method_name, in_field_name, type, value ? "true" : "false");
	}
		break;
	case MOAT_VALUE_TYPE_INT16: {
		sse_int16 value;
		moat_value_get_int16(in_value, &value);
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], int16=[%d]",
				in_method_name, in_field_name, type, value);
	}
		break;
	case MOAT_VALUE_TYPE_INT32: {
		sse_int32 value;
		moat_value_get_int32(in_value, &value);
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], int32=[%d]",
				in_method_name, in_field_name, type, value);
	}
		break;
	case MOAT_VALUE_TYPE_INT64: {
		sse_int64 value;
		moat_value_get_int64(in_value, &value);
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], int64=[%lld]",
				in_method_name, in_field_name, type, value);
	}
		break;
	case MOAT_VALUE_TYPE_FLOAT: {
		sse_float value;
		moat_value_get_float(in_value, &value);
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], float=[%f]",
				in_method_name, in_field_name, type, value);
	}
		break;
	case MOAT_VALUE_TYPE_DOUBLE: {
		sse_double value;
		moat_value_get_double(in_value, &value);
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], double=[%f]",
				in_method_name, in_field_name, type, value);
	}
		break;
	case MOAT_VALUE_TYPE_STRING: {
		sse_char *value;
		sse_uint length;
		moat_value_get_string(in_value, &value, &length);
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], string=[%s]",
				in_method_name, in_field_name, type, value);
	}
		break;
	case MOAT_VALUE_TYPE_BINARY: {
		sse_byte *value;
		sse_uint length;
		sse_int i;
		moat_value_get_binary(in_value, &value, &length);
		LOG_FL_DBG("Method=[%s] in object: field=[%s], value_type=[%d], bin=[",
				in_method_name, in_field_name, type);
		for (i = 0; i < length; i++) {
			LOG_FL_DBG("%02x", value[i]);
		}
		LOG_FL_DBG("]");
	}
		break;
	case MOAT_VALUE_TYPE_RESOURCE:
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], resource type to be supported.",
				in_method_name, in_field_name, type);
		break;
	case MOAT_VALUE_TYPE_OBJECT:
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], object??? ",
				in_method_name, in_field_name, type);
		break;
	default:
		LOG_FL_DBG(
				"Method=[%s] in object: field=[%s], value_type=[%d], unhandled type.",
				in_method_name, in_field_name, type);
		break;
	}
}

/**
 * @brief	MoatObjectの内容表示
 *　			入力されたMoatObjectのインスタンスの内容を表示する。
 *
 * @param	in_method_name メソッド名
 * @param	in_object 表示するMoatObjectのインスタンス
 */
static void
print_object(sse_char *in_method_name, MoatObject *in_object)
{
	MoatObjectIterator *ite;
	sse_char *field_name;
	MoatValue *value;

	ite = moat_object_create_iterator(in_object);
	if (ite == NULL) {
		LOG_FL_DBG("Method=[%s] failed to create iterator.", in_method_name);
		return;
	}
	while (moat_object_iterator_has_next(ite)) {
		field_name = moat_object_iterator_get_next_key(ite);
		value = moat_object_get_value(in_object, field_name);
		print_value(in_method_name, field_name, value);
	}
	moat_object_iterator_free(ite);
}

/////////////////////// Utility API ///////////////////////

/**
 * @brief	改行削除
 *　			文字列から改行を削除('\0'に置換)する。
 *
 * @param	str 文字列
 * @param	len 文字列長
 */
static void
remove_new_line(sse_char *str, sse_uint len)
{
	sse_uint i = 0;

	if (str == NULL || len <= 0) {
		return;
	}

	for (i = 0; i < len; i++) {
		if (str[i] == 0x0A) {
			str[i] = 0x0;
			break;
		}
	}
	return;
}

/**
 * @brief	shellコマンドの実行
 *　			system()を使用して、コマンドを実行する。
 *
 * @param	command 実行するコマンド
 * @return	実行結果
 */
static sse_int
exec_shell_command(const sse_char *command)
{
	sse_int r = SSE_E_OK;
	sse_int status = -1;

	if (command == NULL) {
		return SSE_E_INVAL;
	}

	status = system(command);

	if (status == -1) {
		LOG_FL_DBG("shell returns error value: %d", status);
		return SSE_E_GENERIC;
	} else {
		if ((WIFEXITED(status) && WEXITSTATUS(status)) == 127) {
			LOG_FL_DBG("shell returns error value: %d", status);
			return SSE_E_GENERIC;
		} else {
			if (status == 0x7F00) {
				LOG_FL_DBG("maybe shell can't find command: %d", status);
				return SSE_E_GENERIC;
			}
		}
	}
	return r;
}

/**
 * @brief	ディレクトリの生成
 *　			指定されたディレクトリを生成する。
 *
 * @param	in_path 作成するディレクトリへのパス
 * @param   mode 実行モード(mkdir()に渡すモード)
 * @return	実行結果
 */
static sse_int
make_path(const sse_char *in_path, mode_t mode)
{
	FileMkpath *mp = NULL;
	sse_int err = SSE_E_GENERIC;

	if (in_path == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	mp = mkpath_new();
	if (mp == NULL) {
		LOG_FL_ERR("nomem");
		return SSE_E_NOMEM;
	}

	err = mkpath_make(mp, in_path, mode);
	if (err) {
		LOG_FL_ERR("failed to make path.");
	}

	mkpath_fini(mp);
	return err;
}

/**
 * @brief	ファイルコピー
 *　			指定されたファイルをコピーする。
 *
 * @param	in_copy_from コピー元ファイルへのパス
 * @param   in_copy_to コピー先ファイルへのパス
 * @return	実行結果
 */
static sse_int
copy_file(const sse_char *in_copy_from, const sse_char *in_copy_to)
{
	sse_int fd_copy_from = -1;
	sse_int fd_copy_to = -1;
	sse_byte *readbuf = NULL;
	sse_int read_bytes = 0;
	sse_int write_bytes = 0;
	sse_int err = SSE_E_GENERIC;
	sse_char *cp_path = NULL;
	sse_char *target_dir = NULL;
	sse_int err_no = 0;

	if (in_copy_from == NULL || in_copy_from == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	errno = 0;
	fd_copy_from = open(in_copy_from, O_RDONLY);
	err_no = errno;
	if (fd_copy_from < 0) {
		LOG_FL_ERR("failed to open temporary file:: %s", strerror(err_no));
		goto error_exit;
	}

	errno = 0;
	fd_copy_to = open(in_copy_to, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
	err_no = errno;
	if (fd_copy_to < 0) {
		LOG_FL_DBG("open() failed. errno = %d: %s", errno, strerror(err_no));
		if (errno == ENOENT) {
			/* No such file or directory: needs new dir*/
			cp_path = sse_strdup(in_copy_to);
			if (cp_path == NULL) {
				LOG_FL_ERR("nomem");
				goto error_exit;
			}
			target_dir = dirname(cp_path);
			err = make_path(target_dir, S_IRUSR | S_IWUSR);
			if (err) {
				LOG_FL_ERR("make_path() failed");
				goto error_exit;
			}

			/* open target file again..*/
			fd_copy_to = open(in_copy_to, O_CREAT | O_WRONLY | O_TRUNC,
					S_IRUSR | S_IWUSR);
			if (fd_copy_to < 0) {
				LOG_FL_DBG("open() failed again. errno = %d: %s",
						errno, strerror(errno));
				goto error_exit;
			}
		}
	}

	readbuf = sse_malloc(MAX_SIZE_OF_READ_BUFFER);
	if (readbuf == NULL) {
		LOG_FL_ERR("nomem. err");
		goto error_exit;
	}

    /* ファイルコピー処理 */
	while (1) {
		errno = 0;
		read_bytes = read(fd_copy_from, readbuf, MAX_SIZE_OF_READ_BUFFER);
		err_no = errno;
		if (read_bytes < 0) {
			LOG_FL_ERR("failed to read file :: %s", strerror(err_no));
			goto error_exit;
		}

		errno = 0;
		write_bytes = write(fd_copy_to, readbuf, read_bytes);
		err_no = errno;
		if (write_bytes < 0) {
			LOG_FL_ERR("failed to file writing :: %s", strerror(err_no));
			goto error_exit;
		}

		if (read_bytes < MAX_SIZE_OF_READ_BUFFER) {
			break;
		}

	}

	sse_free(readbuf);
	close(fd_copy_to);
	close(fd_copy_from);

	return SSE_E_OK;

error_exit:
	if (readbuf != NULL) {
		sse_free(readbuf);
	}
	if (fd_copy_to > 0) {
		close(fd_copy_to);
	}
	if (fd_copy_from > 0) {
		close(fd_copy_from);
	}
	if (cp_path != NULL) {
		sse_free(cp_path);
	}

	return SSE_E_GENERIC;
}

/**
 * @brief	ファイル末尾の改行判定
 *　			指定されたファイルの末尾が改行であるか判定し、結果を出力する。
 *
 * @param	in_file 判定対象のファイル
 * @param   out_result 判定結果
 * @return	処理結果
 */
static sse_int
is_file_last_char_lf(const sse_char *in_file, sse_bool *out_result)
{
	sse_int fd_file = -1;
	sse_char last_char;
	sse_int err_no = 0;

	if (in_file == NULL || out_result == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	errno = 0;
	fd_file = open(in_file, O_RDONLY);
	err_no = errno;
	if (fd_file < 0) {
		LOG_FL_ERR("failed to open temporary file:: %s", strerror(err_no));
		return SSE_E_GENERIC;
	}

	lseek(fd_file, -1, SEEK_END);

	if (read(fd_file, &last_char, 1) != 1) {
		LOG_FL_ERR("failed to read file");
		close(fd_file);
		return SSE_E_GENERIC;
	}

	if ((last_char == 0x05) || (last_char == '\n')) {
		*out_result = sse_true;
	} else {
		*out_result = sse_false;
	}
	close(fd_file);
	return SSE_E_OK;
}

/**
 * @brief	ファイルへの文字列追加
 *　			指定されたファイルの末尾に、文字列を追加する。
 *
 * @param	in_file 文字列追加対象のファイル
 * @param   in_str 追加する文字列
 * @param   len 追加する文字列長
 * @return	処理結果
 */
static sse_int
append_string_to_file(const sse_char *in_file, const sse_char *in_str,
		sse_int len)
{
	sse_int fd_file = -1;
	sse_int write_bytes = 0;
	sse_int err = SSE_E_GENERIC;
	sse_int err_no = 0;

	if (in_file == NULL || in_str == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	errno = 0;
	fd_file = open(in_file, O_WRONLY | O_APPEND);
	err_no = errno;
	if (fd_file < 0) {
		LOG_FL_ERR("failed to open destination file:: %s", strerror(err_no));
		return SSE_E_GENERIC;
	}

	errno = 0;
	write_bytes = write(fd_file, in_str, len);
	err_no = errno;
	if (write_bytes != len) {
		LOG_FL_ERR("failed to file writing :: %s", strerror(err_no));
		err = SSE_E_GENERIC;
	} else {
		err = SSE_E_OK;
	}

	close(fd_file);
	return err;
}

#if 0 //no use
static sse_char *
get_convert_absolute_path_to_relative_path(const sse_char *in_absolute)
{
	sse_char *resolved_path = NULL;
	sse_char *relative_path = NULL;

	sse_char *cp_path1 = NULL;
	sse_char *cp_path2 = NULL;
	sse_char *dir_p = NULL;
	sse_char *base_p = NULL;
	sse_char *realdir = NULL;
	sse_int len = 0;

	if(in_absolute == NULL) {
		LOG_FL_ERR("invalid args");
		return NULL;
	}

	resolved_path = sse_malloc(PATH_MAX + 2); // '/' + null
	if(resolved_path == NULL) {
		LOG_FL_ERR("nomem");
		return NULL;
	}

	sse_memset(resolved_path, 0, PATH_MAX + 2);

	cp_path1 = sse_strdup(in_absolute);
	if(cp_path1 == NULL) {
		LOG_FL_TRC("nomem");
		goto error_exit;
	}
	cp_path2 = sse_strdup(in_absolute);
	if(cp_path2 == NULL) {
		LOG_FL_TRC("nomem");
		goto error_exit;
	}

	dir_p = dirname(cp_path1);
	LOG_FL_TRC("original dir info: %s",dir_p);
	base_p = basename(cp_path2);
	LOG_FL_TRC("original base info: %s",base_p);

	realdir = realpath(dir_p, resolved_path);
	if(realdir == NULL) {
		LOG_FL_ERR("failed to realpath()");
		goto error_exit;
	}
	LOG_FL_TRC("real dir info: %s", realdir);

	len = sse_strlen(realdir);
	realdir[len] = '/';
	realdir[len + 1] = 0;

	relative_path = fileapp_str_cat(realdir, base_p);
	if(relative_path == NULL) {
		LOG_FL_ERR("failed to create realpath");
		goto error_exit;
	}

	sse_free(cp_path1);
	sse_free(cp_path2);
	sse_free(resolved_path);
	return relative_path;

error_exit:
	if(resolved_path != NULL) {
		sse_free(resolved_path);
	}
	if(cp_path1 != NULL) {
		sse_free(cp_path1);
	}
	if(cp_path2 != NULL) {
		sse_free(cp_path2);
	}
	return NULL;
}
#endif

#if 0
static sse_int
is_available_write_file(const sse_char *in_file, sse_bool *out_result)
{
	sse_int rc = 0;
	struct statvfs buf = {0};
	sse_char *cp_path = NULL;
	sse_char *dir_p = NULL;
	if((in_file == NULL) || (out_result == NULL)) {
		LOG_FL_ERR("invalid args.");
		return SSE_E_INVAL;
	}

	cp_path = sse_strdup(in_file);
	if(cp_path == NULL) {
		LOG_FL_TRC("nomem");
		return SSE_E_INVAL;
	}

	dir_p = dirname(cp_path);
	LOG_FL_TRC("dir info: %s",dir_p);

	rc = statvfs(dir_p, &buf);
	LOG_FL_TRC("statvfs result: %d", rc);
	if(rc < 0) {
		LOG_FL_ERR("failed to statvfs() %s: %s\n", optarg, strerror(errno));
		sse_free(cp_path);
		return SSE_E_GENERIC;
	}

	if(buf.f_flag & ST_RDONLY) {
		/* read ony*/
		*out_result = sse_false;
		LOG_FL_TRC("READ ONLY AREA");
	} else {
		/* */
		*out_result = sse_true;
		LOG_FL_TRC("READ WRITE AREA");
	}
	sse_free(cp_path);
	return SSE_E_OK;
}
#endif

/**
 * @brief	ReadOnly判定
 *　			指定されたファイルあるいは、そのファイルの置かれているディレクトリが
 *          ReadOnlyかどうか判定する。
 *
 * @param	in_file 判定対象のファイル
 * @param   out_result 判定結果
 * @return	処理結果
 */
static sse_int
is_targetdir_readonly(const sse_char *in_file, sse_bool *out_result)
{
	sse_int fd = -1;
	sse_char *cp_path = NULL;
	sse_char *dir_p = NULL;
	sse_char *test_file = NULL;
	sse_int err_no = 0;
	sse_int len = 0;
	sse_char *dir_append_slash = NULL;
	FileMkpath *mp = NULL;
	sse_int err = SSE_E_GENERIC;
	sse_bool result = sse_false;

	if (in_file == NULL || out_result == NULL) {
		LOG_FL_ERR("invalid args.");
		return SSE_E_INVAL;
	}

	cp_path = sse_strdup(in_file);
	if (cp_path == NULL) {
		LOG_FL_ERR("nomem");
		return SSE_E_INVAL;
	}

	/* ファイルの置かれているディレクトリのパス取得 */
	dir_p = dirname(cp_path);
	LOG_FL_TRC("target dir: %s", dir_p);

	/* 判定用テストファイルパス(test_file)の生成 */
	len = sse_strlen(dir_p);
	if (dir_p[len - 1] != '/') {
		dir_append_slash = fileapp_str_cat(dir_p, "/");
		if (dir_append_slash == NULL) {
			LOG_FL_ERR("failed to create testfile path");
			goto error_exit;
		}
		test_file = fileapp_str_cat(dir_append_slash, ".sse_writable_test");
		sse_free(dir_append_slash);
		if (test_file == NULL) {
			LOG_FL_ERR("failed to create testfile path");
			goto error_exit;
		}
	} else {
		test_file = fileapp_str_cat(dir_p, "sse_writable_test");
		if (test_file == NULL) {
			LOG_FL_ERR("failed to create testfile path");
			goto error_exit;
		}
	}

	errno = 0;
	*out_result = sse_false;
	LOG_FL_TRC("try to create file: %s", test_file);
	/* 判定用テストファイルをRWモードでopen */
	fd = open(test_file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	err_no = errno;
	if (fd >= 0) {
		/* 判定用テストファイルopen成功: ReadOnlyではない */
		close(fd);
		unlink(test_file);
	} else {
		/* 判定用テストファイルopen失敗: errnoチェック */
		LOG_FL_DBG("open() failed. errno: %d", err_no);
		if (err_no == EROFS) {
			/* EROFS: Read only file system */
			LOG_FL_TRC("target dir is READ ONLY.");
			*out_result = sse_true;
		} else if (err_no == ENOENT) {
			/*  ENOENT: No such file or directory*/
			LOG_FL_TRC("try to create directory again: %s", dir_p);

			/* ディレクトリが生成できるかチェック */
			mp = mkpath_new();
			if (mp == NULL) {
				LOG_FL_ERR("nomem.");
				goto error_exit;
			}

			err = mkpath_make(mp, dir_p, 0666);
			if (err) {
				/* ディレクトリ生成の結果がReadOnlyかチェック */
				err = mkpath_is_lasterr_rofs(mp, &result);
				if (err) {
					LOG_FL_ERR("mkpath_is_lasterr_rofs() failed.");
					goto error_exit;
				}

				if (result) {
					LOG_FL_TRC("target dir is READ ONLY.");
					*out_result = sse_true;
				}
			}
			mkpath_fini(mp);
		} else {
			LOG_FL_ERR("failed to create testfile path");
			goto error_exit;
		}
	}

	sse_free(test_file);
	sse_free(cp_path);
	return SSE_E_OK;

error_exit:
	if (test_file != NULL) {
		sse_free(test_file);
	}
	if (cp_path != NULL) {
		sse_free(cp_path);
	}
	if (mp != NULL) {
		mkpath_fini(mp);
	}
	return SSE_E_GENERIC;
}

/**
 * @brief	unionfs以外判定
 *　			指定されたパスがunionfs以外であるか判定する。
 *
 * @param	in_file 判定対象のパス
 * @param   out_result 判定結果
 * @return	処理結果
 */
static sse_int
is_path_excluded_unionfs(const sse_char *in_file, sse_bool *out_result)
{
	const sse_char *excludelist[] = { "/var/log", NULL };
	sse_int idx = 0;
	sse_int cmp_result = -1;

	if (in_file == NULL || out_result == NULL) {
		LOG_FL_ERR("invalid args.");
		return SSE_E_INVAL;
	}

	*out_result = sse_false;
	while (excludelist[idx] != 0) {
		cmp_result = sse_strncmp(in_file, excludelist[idx], sse_strlen(excludelist[idx]));
		if (cmp_result == 0) {
			LOG_FL_TRC("Area is RW FS");
			*out_result = sse_true;
			break;
		}
		idx++;
	}
	return SSE_E_OK;
}

/**
 * @brief	unionfs判定
 *　			指定されたパスがunionfsであるか判定する。
 *
 * @param	in_file 判定対象のパス
 * @param   out_result 判定結果
 * @return	処理結果
 */
static sse_int
is_path_unionfs(const sse_char *in_file, sse_bool *out_result)
{
	const sse_char *unionfslist[] = { "/etc", "/var", "/media", "/tmp", NULL };
	sse_int idx = 0;
	sse_int cmp_result = -1;
	sse_bool result = sse_false;
	sse_int err = SSE_E_GENERIC;

	if (in_file == NULL || out_result == NULL) {
		LOG_FL_ERR("invalid args.");
		return SSE_E_INVAL;
	}

	*out_result = sse_false;
	while (unionfslist[idx] != 0) {
		cmp_result = sse_strncmp(in_file, unionfslist[idx], sse_strlen(unionfslist[idx]));
		if (cmp_result == 0) {
			err = is_path_excluded_unionfs(in_file, &result);
			if (err) {
				LOG_FL_ERR("failed to is_path_exc;ide_unionfs");
				return SSE_E_INVAL;
			}

			if (!result) {
				LOG_FL_TRC("Area is UnionFS");
				*out_result = sse_true;
				break;
			}
		}
		idx++;
	}
	return SSE_E_OK;
}

/**
 * @brief	config.list内ファイル存在判定
 *　			指定されたパスが/etc/config.listに含まれるか判定する。
 *
 * @param	in_file 判定対象のパス
 * @param   out_result 判定結果
 * @return	処理結果
 */
static sse_int
is_file_existed_in_configlist(const sse_char *in_file, sse_bool *out_result)
{
	FILE *fp;
	sse_char *p = NULL;
	sse_int buf_len = 1024;
	sse_int target_fpath_len = 0;
	sse_int exist_path_len = 0;

	if (in_file == NULL || out_result == NULL) {
		LOG_FL_ERR("invalid args.");
		return SSE_E_INVAL;
	}

	fp = fopen("/etc/config.list", "r");
	if (fp == NULL) {
		LOG_FL_ERR("failed to open /etc/config.list. err");
		return SSE_E_INVAL;
	}

	p = sse_malloc(buf_len); /* +1 for null terminate by fgets*/
	if (p == NULL) {
		LOG_FL_ERR("nomem. err");
		fclose(fp);
		return SSE_E_NOMEM;
	}

	*out_result = sse_false;
	target_fpath_len = sse_strlen(in_file);
	do {
		if (fgets(p, buf_len, fp) != NULL) {
			remove_new_line(p, (buf_len));
			exist_path_len = sse_strlen(p);
			if (exist_path_len == target_fpath_len) {
				if (sse_strncmp((const sse_char *)p, in_file, target_fpath_len)
						== 0) {
					/* found same path string */
					*out_result = sse_true;
					break;
				}
			}
		}
	} while (feof(fp) == 0);

	free(p);
	fclose(fp);
	return SSE_E_OK;
}

/**
 * @brief	unionfsへのファイル書き込み
 *　			unionfsでマウントされているディレクトリにファイルを書き込み、config saveを実行して保存する。
 *
 * @param	in_copy_from コピー元ファイルのパス
 * @param   in_copy_to コピー先ファイルのパス
 * @return	処理結果
 */
static sse_int
write_file_to_unionfs_dir(const sse_char *in_copy_from,
		const sse_char *in_copy_to)
{
	sse_int err = SSE_E_OK;
	sse_bool result = sse_false;
	sse_char char_lf = 0x0A; //LF

	if (in_copy_from == NULL || in_copy_to == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	err = copy_file(in_copy_from, in_copy_to);
	if (err) {
		LOG_FL_ERR("failed writing to RW fs");
		goto error_exit;
	}

	/* コピー対象のファイルがconfig.listに存在するかチェック */
	err = is_file_existed_in_configlist(in_copy_to, &result);
	if (err) {
		LOG_FL_ERR("failed to search /etc/config.list");
		goto error_exit;
	}

	if (!result) {
		/* need append file path if not*/
		err = is_file_last_char_lf("/etc/config.list", &result);
		if (err) {
			LOG_FL_ERR("failed to read /etc/config.list");
			goto error_exit;
		}

		if (!result) {
			/* need to add LF */
			err = append_string_to_file("/etc/config.list", &char_lf,
					sizeof(char_lf));
			if (err) {
				LOG_FL_ERR("failed to append LF to config.list");
				goto error_exit;
			}
		}

		err = append_string_to_file("/etc/config.list", in_copy_to,
				sse_strlen(in_copy_to));
		if (err) {
			LOG_FL_ERR("failed to append file path to config.list");
			goto error_exit;
		}
	}
	/* execute "config save" */
	err = exec_shell_command(MDL_CONFIG_SH_APPLY);
	if (err) {
		LOG_FL_ERR("failed to append file path to config.list");
		goto error_exit;
	}

	return SSE_E_OK;

error_exit:
	return SSE_E_GENERIC;
}

/**
 * @brief	ReadOnly領域へのファイル書き込み
 *　			一旦R/Wでリマウントしてからファイルを書き込み書き込み後、ReadOnlyに戻す。
 *
 * @param	in_copy_from コピー元ファイルのパス
 * @param   in_copy_to コピー先ファイルのパス
 * @return	処理結果
 */
static sse_int
write_file_to_readonly_dir(const sse_char *in_copy_from,
		const sse_char *in_copy_to)
{
	sse_int err = SSE_E_OK;

	if (in_copy_from == NULL || in_copy_to == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	/* re-mount root dir as Read+Write */
	err = exec_shell_command(MDL_FILEDL_SH_REMOUNT_RW);
	if (err) {
		LOG_FL_ERR("failed to re-mount as rw");
		return SSE_E_INVAL;
	}

	err = copy_file(in_copy_from, in_copy_to);
	if (err) {
		LOG_FL_ERR("failed writing to RW fs");
		goto error_exit;
	}

	/* re-mount root dir as ReadOnly */
	err = exec_shell_command(MDL_FILEDL_SH_REMOUNT_RO);
	if (err) {
		/* error but continue operation. */
		LOG_FL_ERR("failed to re-mount as ro");
	}

	return SSE_E_OK;

error_exit:
	exec_shell_command(MDL_FILEDL_SH_REMOUNT_RO);
	return SSE_E_GENERIC;
}

/**
 * @brief	ファイル移動
 *　			移動先の状況(RO/unionfsなど)を判定して、適切なファイル移動処理を行う。
 *
 * @param	in_copy_from コピー元ファイルのパス
 * @param   in_copy_to コピー先ファイルのパス
 * @return	処理結果
 */
static sse_int
move_file(const sse_char *in_copy_from, const sse_char *in_copy_to)
{
	sse_bool result = sse_false;
	sse_int err = SSE_E_GENERIC;

	if (in_copy_from == NULL || in_copy_to == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	LOG_FL_DBG("@@@@@ move_file debug - start @@@@@");
	LOG_FL_DBG("in_copy_from: %s", in_copy_from);
	LOG_FL_DBG("in_copy_to: %s", in_copy_to);

	/* get status of target dir is writable */
	err = is_targetdir_readonly(in_copy_to, &result);
	if (err) {
		LOG_FL_ERR("failed to get the status of specified dir is rwfs or not");
		goto error_exit;
	}

	if (result) {
		/* target dir: RO */
		LOG_FL_DBG("--- target dir: RO fs");
		err = write_file_to_readonly_dir(in_copy_from, in_copy_to);
		if (err) {
			LOG_FL_ERR("failed writing to RO fs.");
			goto error_exit;
		}
	} else {
		err = is_path_unionfs(in_copy_to, &result);
		if (err) {
			LOG_FL_ERR(
					"failed to get status of specified dir is unionfs or not");
			goto error_exit;
		}

		if (result) {
			/* target dir: unionfs (need 'config save' command) */
			LOG_FL_DBG("--- target dir: unionfs");
			err = write_file_to_unionfs_dir(in_copy_from, in_copy_to);
			if (err) {
				LOG_FL_ERR("failed writing to unionfs");
				goto error_exit;
			}
		} else {
			/* target dir: RW dir */
			LOG_FL_DBG("--- target dir: RW fs");
			err = copy_file(in_copy_from, in_copy_to);
			if (err) {
				LOG_FL_ERR("failed writing to RW fs");
				goto error_exit;
			}
		}
	}

	remove(in_copy_from);
	LOG_FL_DBG("@@@@@ move_file debug - end @@@@@");
	return SSE_E_OK;

error_exit:
	remove(in_copy_from);
	return SSE_E_GENERIC;
}

#if 0 // no use
/**
 * 未使用
 */
static sse_int
obj_collection_serialize(MoatObject *in_obj, const sse_char *fname)
{
	sse_byte *data = NULL;
	sse_uint len;
	sse_int fd = -1;
	sse_int wb;
	sse_int err = SSE_E_GENERIC;

	if (in_obj == NULL || fname == NULL) {
		LOG_FL_ERR("invalid args. error");
		return SSE_E_INVAL;
	}

	err = moat_object_to_stream(in_obj, NULL, &len);
	if (err) {
		LOG_FL_ERR("failed to get size of serialize object: %s", fname);
		return err;
	}

	data = sse_malloc(len);
	err = moat_object_to_stream(in_obj, data, &len);
	if (err) {
		LOG_FL_ERR("failed to get serialize object: %s", fname);
		return err;
	}
	fd = open(fname, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd < 0) {
		LOG_FL_ERR("failed to open file: %s", fname);
		goto error_exit;
	}
	wb = write(fd, data, len);
	if (wb < len) {
		LOG_FL_ERR("failed to write to file: %s", fname);
		goto error_exit;
	}
	close(fd);
	sse_free(data);
	return SSE_E_OK;

error_exit:
	if (fd > 0) {
		close(fd);
	}
	if (data != NULL) {
		sse_free(data);
	}
	return SSE_E_GENERIC;
}

/**
 * 未使用
 */
MoatObject *
obj_collection_deserialize(const sse_char *fname)
{
	sse_byte buffer[1024 * 2];
	sse_int rb = 0;
	sse_int fd = -1;
	MoatObject *obj = 0;
	sse_uint len = 0;

	if (fname == NULL) {
		LOG_FL_ERR("invalid args. error");
		return NULL;
	}

	fd = open(fname, O_RDONLY);
	if (fd >= 0) {
		rb = read(fd, buffer, sizeof(buffer));
		if (rb <= 0) {
			LOG_FL_ERR("failed to open file: %s", fname);
			goto error_exit;
		}
		obj = moat_object_new_from_stream(buffer, &len);
		if (obj == NULL) {
			LOG_FL_ERR("failed to desirializing: %s", fname);
			goto error_exit;
		}
	} else {
		obj = moat_object_new();
		if (obj == NULL) {
			LOG_FL_ERR("failed to create new object: %s", fname);
			goto error_exit;
		}
	}
	LOG_FL_DBG("target file: %s", fname);
	LOG_OBJ("perfomed desirialization - colleciton: ", obj);
	close(fd);

	return obj;

error_exit:
	if (fd < 0) {
		close(fd);
	}
	return NULL;
}
#endif

/**
 * @brief	文字列連結
 *　			2つの文字列を連結した文字列を生成する。
 *
 * @param	s1 連結対象の文字列(前方)
 * @param   s2 連結対象の文字列(後方)
 * @return	連結した文字列(新たに確保されたバッファ)
 */
static sse_char *
fileapp_str_cat(const sse_char *s1, const sse_char *s2)
{
	sse_int s1_len;
	sse_int s2_len;
	sse_char *str = NULL;

	if (s1 == NULL || s2 == NULL) {
		return NULL;
	}

	s1_len = sse_strlen(s1);
	s2_len = sse_strlen(s2);

	str = sse_malloc(s1_len + s2_len + 1);
	if (str == NULL) {
		return NULL;
	}

	sse_memset(str, 0, s1_len + s2_len + 1);
	sse_memcpy(str, s1, s1_len);
	sse_memcpy(&str[s1_len], s2, s2_len);
	return str;
}

/**
 * @brief	通知IDの生成
 *			URNとサービスIDから通知IDを生成する。
 *			通知IDのフォーマット：
 *			"urn:moat:" + ${APP_ID} + ":" + ${PACKAGE_ID} + ":" + ${SERVICE_NAME} + ":" + ${VERSION}
 *
 * @param	in_urn URN
 * @param	in_service_name サービス名
 * @return	通知ID（呼び出し側で解放すること）
 */
static sse_char *
create_notification_id(sse_char *in_urn, sse_char *in_service_name)
{
	sse_char *prefix = "urn:moat:";
	sse_uint prefix_len;
	sse_char *suffix = ":1.0";
	sse_uint suffix_len;
	sse_uint urn_len;
	sse_uint service_len;
	sse_char *noti_id = NULL;
	sse_char *p;

	prefix_len = sse_strlen(prefix);
	urn_len = sse_strlen(in_urn);
	service_len = sse_strlen(in_service_name);
	suffix_len = sse_strlen(suffix);
	noti_id = sse_malloc(
			prefix_len + urn_len + 1 + service_len + suffix_len + 1);
	if (noti_id == NULL) {
		return NULL;
	}
	p = noti_id;
	sse_memcpy(p, prefix, prefix_len);
	p += prefix_len;
	sse_memcpy(p, in_urn, urn_len);
	p += urn_len;
	*p = ':';
	p++;
	sse_memcpy(p, in_service_name, service_len);
	p += service_len;
	sse_memcpy(p, suffix, suffix_len);
	p += suffix_len;
	*p = '\0';
	return noti_id;
}

/**
 * @brief	MoatValueの文字列値複製
 *　			MoatValue内の文字列値を取り出し、複製した文字列を返す。
 *
 * @param	value MoatValue
 * @return	複製文字列(呼び出し側で解放すること)
 */
static sse_char *
fileapp_util_clone_string_value(MoatValue *value)
{
	sse_char *r = NULL;
	moat_value_type type;
	sse_uint len;
	sse_char *s = NULL;
	sse_int err;

	if (value == NULL) {
		return NULL;
	}

	type = moat_value_get_type(value);
	if (type == MOAT_VALUE_TYPE_STRING) {
		err = moat_value_get_string(value, &s, &len);
		if (err) {
			return NULL;
		}
		r = sse_malloc(len + 1);
		if (r != NULL) {
			sse_memset(r, 0, len + 1);
			sse_memcpy(r, s, len);
		}
	}
	return r;
}
/////////////////////// Syslog API ///////////////////////

/**
 * @brief	SYSLOGマーカ出力
 *　			SYSLOGにSUNSYNCのマーカを出力する。
 *
 * @param	ident syslogのident
 * @param	facirity syslogのfacirity
 * @param	priority syslogのpriority
 * @param	marker 出力するマーカ
 */
static void
fileapp_trace_leave_marker_to_syslog(const sse_char *ident, sse_int facirity,
		sse_int priority, const sse_char *marker)
{
	if (ident != NULL && marker != NULL) {
		openlog(ident, (LOG_CONS | LOG_PID), facirity);
		syslog(priority, "%s\n", marker);
		closelog();
	}
	return;
}

/*!
 * caller is expected path and marker string are null terminated.
 * regarding *position value
 * 			0      : operation done completely, but couldn't find marker in file
 *  		other  : function found marker, so returns next head position of marker line.
 */
static sse_int
fileapp_trace_rotatefile_find_marker(const char *path, const char *marker,
		sse_uint *size, sse_uint *position)
{
	FILE *fp;
	sse_char *p = NULL;
	sse_int err = SSE_E_OK;

	LOG_IN();
	if (path == NULL || marker == NULL || size == NULL || position == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	fp = fopen(path, "r");
	if (fp == NULL) {
		LOG_FL_DBG("--- failed to open file. but its OK!");
		*position = 0;
		*size = 0;
		return SSE_E_OK;
	}

	p = sse_malloc(MAX_SIZE_OF_READ_BUFFER + 1); /* +1 for null terminate by fgets*/
	if (p == NULL) {
		LOG_FL_ERR("nomem. err");
		fclose(fp);
		return SSE_E_NOMEM;
	}

	/* get filesize */
	err = fseek(fp, 0, SEEK_END);
	if (err < 0) {
		LOG_FL_ERR("seeking file err");
		fclose(fp);
		return SSE_E_GENERIC;
	}

	*size = ftell(fp);

	/* fp returns to top of file */
	rewind(fp);

	LOG_FL_DBG(" current target: file =  %s, size = %u, marker = %s",
			path, *size, marker);

	/* initizalize position to top of file */
	*position = 0;
	do {
		if (fgets(p, MAX_SIZE_OF_READ_BUFFER, fp) != NULL) {
			if (strstr(p, marker) != NULL) {
				/* found marker */
				*position = ftell(fp);
				LOG_FL_DBG(" -> found marker. position: %u",
						(sse_uint)*position);
			}
		} else {
			if (feof(fp) == 0) {
				/* error case */
				LOG_FL_ERR(" -> error occured");
				err = SSE_E_GENERIC;
			} else {
				LOG_FL_DBG(" -> reached EOF. save position: %u",
						(sse_uint)*position);
			}
			break;
		}
	} while (feof(fp) == 0);

	free(p);
	fclose(fp);

	LOG_OUT();
	return err;
}

/**
 * @brief	SYSLOGファイル情報リスト解放
 *　			SYSLOGファイル情報リストを解放する。
 *
 * @param	slist SYSLOGファイル情報リスト
 */
static void
fileapp_trace_rotatefile_release_flist(SlogFileInf **slist)
{
	sse_int i;

	if (slist != NULL) {
		for (i = 0; slist[i] != NULL; i++) {
			if (slist[i]->fname != NULL) {
				sse_free(slist[i]->fname);
			}
			sse_free(slist[i]);
		}
		sse_free(slist);
	}
	return;
}

/**
 * @brief	SYSLOGファイル情報リスト生成
 *　			SYSLOGファイル情報リストを生成する。
 *
 * @param	log_name syslogファイルのパス
 * @param	marker マーカ文字列
 * @param	search_gen 検索対象の世代
 * @param	type ソート種別
 * @return	SYSLOGファイル情報リスト
 */
static SlogFileInf **
fileapp_trace_rotatefile_create_flist(const sse_char *log_name,
		const sse_char *marker, sse_int search_gen, SlogSortType type)
{
	SlogFileInf **slist = NULL;
	sse_char *pre_fname = NULL;
	sse_int fname_length = 0;
	sse_int idx = 0;
	sse_char gen_num_str[5]; /* ".0\n"(length=3) to ".128\n"(length=5) */
	sse_int gen_num_len = 0;
	sse_int err = SSE_E_GENERIC;
	/* */
	SlogFileInf *tmpsinf = NULL;
	sse_int i;
	sse_int last_array;

	if (log_name == NULL || marker == NULL
			|| search_gen <= 0|| search_gen > AVAILABLE_SEARCH_GENERATION) {
		LOG_FL_ERR("invalid arg. err");
		return NULL;
	}

	slist = sse_malloc(sizeof(SlogFileInf *) * (search_gen + 1)); /* +1 for NULL terminate*/
	if (slist == NULL) {
		LOG_FL_ERR("nomem. err");
		return NULL;
	}

	/* initialize list first */
	for (idx = 0; idx < (search_gen + 1); idx++) {
		slist[idx] = NULL;
	}

	/* main loop */
	for (idx = 0, pre_fname = (char *)log_name, gen_num_len = 0;
			idx < search_gen; idx++) {

		LOG_FL_DBG("LOOP >>>> %d", idx);
		/* allocate list (pointer array) of file info */
		slist[idx] = sse_malloc(sizeof(SlogFileInf));
		sse_memset(slist[idx], 0, sizeof(SlogFileInf));
		fname_length = strlen(pre_fname);
		if (idx > 0) {
			/* we assume that rotation filenames are sample, sample.0, sample.1, ample.2 ...*/
			gen_num_len = sprintf(gen_num_str, ".%d", (idx - 1));
			LOG_FL_DBG("rotate info: [%.*s]", gen_num_len, gen_num_str);
		}

		/* create file name */
		slist[idx]->fname = sse_malloc(fname_length + gen_num_len + 1); /* +1 for NULL terminate */
		sse_memcpy(slist[idx]->fname, pre_fname, fname_length);
		if (gen_num_len > 0) {
			sse_memcpy(&slist[idx]->fname[fname_length], gen_num_str,
					gen_num_len);
		}

		/* set terminate and initialize potision */
		slist[idx]->fname[fname_length + gen_num_len] = 0;
		slist[idx]->position = 0;
		LOG_FL_DBG("target file name %s", slist[idx]->fname);
		LOG_FL_DBG("target marker %s", marker);

		/* try to find marker in specific file */
		err = fileapp_trace_rotatefile_find_marker(slist[idx]->fname, marker,
				&slist[idx]->size, &slist[idx]->position);
		if (err != SSE_E_OK) {
			/* position should (value < 0) if found failed */
			fileapp_trace_rotatefile_release_flist(slist);
			slist = NULL;
			break;
		} else if (slist[idx]->position > 0) {
			/* app should exit outside of loop since app already found marker when position value is big than 0*/
			break;
		}
	}

	/* sort */
	if (type == SYSLOG_SORT_ASCENDING) {
		idx = 0;
		while (slist[idx] != NULL) {
			idx++;
		}

		if (idx > 1) {
			for (i = 0, last_array = idx - 1; i < (last_array - i); i++) {
				tmpsinf = slist[i];
				slist[i] = slist[last_array - i];
				slist[last_array - i] = tmpsinf;
			}
		}
	}
	return slist;
}

/**
 * @brief	SYSLOGの単一ファイル化
 *　			複数のsyslogファイルを読み出し、一つのファイルに書き込む。
 *
 * @param	slist SYSLOGファイル情報リスト
 * @param	in_packed_file まとめたファイルのパス
 * @return	処理結果
 */
static sse_int
fileapp_trace_rotatefile_pack_singlefile(SlogFileInf **slist,
		const sse_char *in_packed_file)
{
	sse_int fd_copy_from = -1;
	sse_int fd_copy_to = -1;
	sse_byte *readbuf = NULL;
	sse_int err = SSE_E_GENERIC;
	sse_int read_bytes = 0;
	sse_int write_bytes = 0;
	sse_int idx = 0;
	sse_bool processed = sse_false;

	if (slist == NULL || in_packed_file == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	fd_copy_to = open(in_packed_file, O_CREAT | O_WRONLY | O_TRUNC,
			S_IRUSR | S_IWUSR);
	if (fd_copy_to < 0) {
		LOG_FL_ERR("failed to create whole syslog file:: %s", strerror(errno));
		return SSE_E_GENERIC;
	}

	readbuf = sse_malloc(MAX_SIZE_OF_READ_BUFFER);
	if (readbuf == NULL) {
		LOG_FL_ERR("nomem. err");
		err = SSE_E_GENERIC;
		goto done;
	}

	for (idx = 0; slist[idx] != NULL; idx++) {

		/* open target file*/
		fd_copy_from = -1;
		fd_copy_from = open(slist[idx]->fname, O_RDONLY);
		if (fd_copy_from < 0) {
			/* maybe file not exist but its OK. */
			err = SSE_E_OK;
			continue;
		}

		processed = sse_true;

		/* seek */
		if (slist[idx]->position > 0) {
			lseek(fd_copy_from, slist[idx]->position, SEEK_SET);
		}

		while (1) {
			read_bytes = 0;
			read_bytes = read(fd_copy_from, readbuf, MAX_SIZE_OF_READ_BUFFER);
			if (read_bytes < 0) {
				LOG_FL_ERR("failed to read file :: %s", strerror(errno));
				close(fd_copy_from);
				err = SSE_E_GENERIC;
				goto done;
			}

			write_bytes = write(fd_copy_to, readbuf, read_bytes);
			if (write_bytes < 0) {
				LOG_FL_ERR("failed to file writing :: %s", strerror(errno));
				err = SSE_E_GENERIC;
				goto done;
			}

			if (read_bytes < MAX_SIZE_OF_READ_BUFFER) {
				break;
			}

		}
	}
	if (!processed) {
		/* syslogファイルが一つも見つからなかった */
		LOG_FL_ERR("syslog file not found");
		err = SSE_E_NOENT;
		goto done;
	}
	sse_free(readbuf);
	close(fd_copy_to);
	return SSE_E_OK;

done:
	if (readbuf != NULL) {
		sse_free(readbuf);
	}
	if (fd_copy_to > 0) {
		close(fd_copy_to);
	}
	return err;
}

/* collecting syslog main function*/
/**
 * @brief	SYSLOG収集
 *　			SYSLOG収集処理のエントリポイント。
 *
 * @param	in_target_syslog_name SYSLOGファイルのパス
 * @param	generation 収集対象の世代
 * @param	in_target_marker マーカ文字列
 * @param	in_packed_file 収集結果ファイルのパス
 * @return	処理結果
 */
static sse_int
fileapp_collect_syslog(const sse_char *in_target_syslog_name,
		sse_int generation, const sse_char *in_target_marker,
		const sse_char *in_packed_file)
{
	sse_int err = SSE_E_OK;
	SlogFileInf **syslogflist = NULL;

	if (in_target_syslog_name == NULL
			|| generation
					<= 0|| in_target_marker == NULL || in_packed_file == NULL) {
		LOG_FL_ERR("invalid arg. err");
		return SSE_E_INVAL;
	}

	LOG_FL_DBG("--- collecting syslog start ---");
	LOG_FL_DBG("tartget syslog name: %s", in_target_syslog_name);
	LOG_FL_DBG("target search generation: %d", generation);
	LOG_FL_DBG("tartget marker: %s", in_target_marker);
	LOG_FL_DBG("packed filee: %s", in_packed_file);

	/* create all file list (and pogision info) to be uploaded server*/
	syslogflist = fileapp_trace_rotatefile_create_flist(in_target_syslog_name,
			in_target_marker, generation, SYSLOG_SORT_ASCENDING);
	if (syslogflist == NULL) {
		LOG_FL_ERR(
				"failed to create file list to be upload as syslog file. err");
		/* release instance memory */
		return SSE_E_INVAL;
	}

	err = fileapp_trace_rotatefile_pack_singlefile(syslogflist, in_packed_file);

	/* release instance memory */
	fileapp_trace_rotatefile_release_flist(syslogflist);

	LOG_FL_DBG("--- collecting syslog end ---");
	return err;
}
/////////////////////// Configration API ///////////////////////

/**
 * @brief	設定ファイル反映結果削除
 *　			保存済みの設定ファイル反映結果オブジェクトを削除する。
 *
 * @param	in_ctx コンテキスト情報
 * @param	key 設定ファイル反映結果のキー
 * @return	処理結果
 */
static sse_int
fileapp_remove_configinf(FileAppContext *in_ctx, const sse_char *key)
{
	sse_int err = SSE_E_GENERIC;

	err = moat_datastore_remove_object(in_ctx->Moat, (sse_char *)key);
	if (err) {
		LOG_FL_ERR("failed to remove object :: %d", err);
	}
	return err;
}

/**
 * @brief	設定ファイル反映結果保存
 *　			設定ファイル反映結果オブジェクトを生成し、保存する。
 *
 * @param	in_ctx コンテキスト情報
 * @param	status_code 結果コード
 * @param	key 設定ファイル反映結果のキー
 * @return	処理結果
 */
static sse_int
fileapp_leave_configinf(FileAppContext *in_ctx, sse_int status_code,
		const sse_char *key)
{
	MoatObject *obj = NULL;
	sse_int err = SSE_E_GENERIC;
	const sse_char *str_field;
	sse_char *str_value;
	sse_int idx = 0;

	if (in_ctx == NULL || key == NULL) {
		LOG_FL_ERR("invalid args. err");
		return SSE_E_INVAL;
	}

	obj = fileapp_create_fileresult_obj(status_code);
	if (obj == NULL) {
		LOG_FL_ERR("failed to create moat object. err");
		goto done;
	}

	while (notfy_arglist[idx] != NULL) {
		str_field = notfy_arglist[idx];
		if (sse_strcmp(str_field, "urn") == 0) {
			str_value = in_ctx->ServiceID;

		} else if (sse_strcmp(str_field, "key") == 0) {
			str_value = in_ctx->transfer->AsyncKey;

		} else if (sse_strcmp(str_field, "modelname") == 0) {
			str_value = in_ctx->ModelName;

		} else {
			LOG_FL_ERR("unknown field name. err");
			goto done;
		}

		err = moat_object_add_string_value(obj, (sse_char *)str_field, str_value, 0,
				sse_true, sse_true);
		if (err) {
			LOG_FL_ERR("failed to add field to moat object: %s", str_field);
			goto done;
		}

		idx++;
	}

	LOG_OBJ("create leave information obj", obj);
	/* leave information */
	err = moat_datastore_save_object(in_ctx->Moat, (sse_char *)key, obj);
	if (err) {
		LOG_FL_ERR("failed to leave information. err");
		goto done;
	}

	return SSE_E_OK;

done:
	if (obj != NULL) {
		moat_object_free(obj);
	}
	fileapp_remove_configinf(in_ctx, (const sse_char *)key);
	return SSE_E_GENERIC;
}

/**
 * @brief	設定ファイル反映
 *　			ダウンロードした設定ファイルの内容をconfig saveで反映し、再起動を行う。
 *
 * @param	in_ctx コンテキスト情報
 * @param	status_code 結果コード
 * @return	処理結果
 */
static sse_int
fileapp_transfer_apply_config(FileAppContext *in_ctx, sse_int status_code)
{
	sse_int err = SSE_E_INVAL;

	LOG_IN();
	if (in_ctx == NULL) {
		LOG_FL_ERR("invalid args. err");
		return SSE_E_INVAL;
	}
	/* execute "config save" */
	err = exec_shell_command(MDL_CONFIG_SH_APPLY);
	if (err) {
		return err;
	}
	/* leave job which means app should send result via notify after reboot 			*/
	err = fileapp_leave_configinf(in_ctx, status_code,
			MDL_KEY_CONFIG_LEAVE_INF);

	/* execute "shutdown -r now" */
	err = exec_shell_command(MDL_CONFIG_SH_REBOOT);
	if (err) {
		fileapp_remove_configinf(in_ctx, MDL_KEY_CONFIG_LEAVE_INF);
		return err;
	}

	LOG_OUT();
	return SSE_E_OK;
}

/**
 * @brief	設定ファイル反映結果通知
 *　			保存済みの設定ファイル反映結果を読み出し、サーバへの結果通知を行う。
 *
 * @param	in_ctx コンテキスト情報
 * @param	key 設定ファイル反映結果のキー
 * @return	処理結果
 */
static sse_int
fileapp_transfer_notify_apply_config(FileAppContext *in_ctx,
		const sse_char *key)
{
	sse_int err = SSE_E_GENERIC;
	sse_char *str = NULL;
	sse_char *str_args[3] = { NULL };
	sse_int i = 0;
	sse_uint len = 0;
	MoatObject *obj = NULL;
	sse_int req_id;

	if (in_ctx == NULL || key == NULL) {
		LOG_FL_ERR("invalid args. err");
		return SSE_E_INVAL;
	}

#if 0	/* todo: need to add an interface to check whether an object exists or not */
	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		/* leaving file is not found. its means app doesn't send notify to server */
		return SSE_E_OK;
	}
	close(fd);
#endif

	err = moat_datastore_load_object(in_ctx->Moat, (sse_char *)key, &obj);
	if (err) {
		/* leaving file is not found. its means app doesn't send notify to server */
		LOG_FL_DBG("can't find leave file.");
		return SSE_E_OK;
#if 0	/* todo: need to add an interface to check whether an object exists or not */
		LOG_FL_ERR("failed to load leaving file. err");
		return err;
#endif
	}

	/* create args */
	for (i = 0; i < 3; i++) {
		err = moat_object_get_string_value(obj, (sse_char *)notfy_arglist[i], &str, &len);
		if (err) {
			LOG_FL_ERR("failed to load leaving file. err");
			goto err_done;
		}

		str_args[i] = sse_malloc(len + 1);
		if (str_args[i] == NULL) {

		}
		sse_memset(str_args[i], 0, len + 1);
		sse_memcpy(str_args[i], str, len);

		moat_object_remove_value(obj, (sse_char *)notfy_arglist[i]);
	}

	/* send */
	LOG_OBJ("send notification for configrationinfo", obj);
	req_id = moat_send_notification(in_ctx->Moat, str_args[0], str_args[1],
			str_args[2], obj, NULL, NULL);

	fileapp_remove_configinf(in_ctx, key);
	moat_object_free(obj);
	if (req_id < 0) {
		err = req_id;
	} else {
		err = SSE_E_OK;
	}
	return err;

err_done:

	fileapp_remove_configinf(in_ctx, key);
	return err;
}

/////////////////////// Transfer API (HTTP) ///////////////////////

/**
 * @brief	ファイル転送オブジェクト解放
 *　			ファイル転送オブジェクトを解放する。
 *
 * @param	transfer ファイル転送オブジェクト
 */
static void
fileapp_transfer_free(FileAppTransfer *transfer)
{
	if (transfer != NULL) {
		if (transfer->AsyncKey != NULL) {
			sse_free(transfer->AsyncKey);
			transfer->AsyncKey = NULL;
		}
		if (transfer->Http != NULL) {
			moat_httpc_free(transfer->Http);
			transfer->Http = NULL;
		}
		if (transfer->Path_from != NULL) {
			sse_free(transfer->Path_from);
			transfer->Path_from = NULL;
		}
		if (transfer->Path_to != NULL) {
			sse_free(transfer->Path_to);
			transfer->Path_to = NULL;
		}
		if (transfer->tmpfile != NULL) {
			sse_free(transfer->tmpfile);
			transfer->tmpfile = NULL;
		}
		sse_free(transfer);
		transfer = NULL;
	}
}

/**
 * @brief	ファイル転送オブジェクト生成
 *　			ファイル転送オブジェクトを生成する。
 *
 * @param	AsyncKey 非同期通知キー
 * @param	path_from 取得元へのパス
 * @param	path_to 取得先へのパス
 * @param	direction HTTPの方向(ダウンロード or アップロード)
 * @return	transfer ファイル転送オブジェクト
 */
static FileAppTransfer *
fileapp_transfer_new(const sse_char *AsyncKey, const sse_char *path_from,
		const sse_char *path_to, HttpUseDirection direction)
{
	FileAppTransfer *transfer = NULL;

	if (AsyncKey == NULL || path_from == NULL || path_to == NULL) {
		LOG_FL_ERR("unexpected arguments. err");
		return NULL;
	}

	transfer = sse_malloc(sizeof(FileAppTransfer));
	if (transfer == NULL) {
		LOG_FL_ERR("nomem err");
		return NULL;
	}

	sse_memset(transfer, 0, sizeof(FileAppTransfer));

	transfer->Http = moat_httpc_new();
	if (transfer->Http == NULL) {
		LOG_FL_ERR("failed to create instance of http client. err");
		goto error_exit;
	}

	transfer->AsyncKey = sse_strdup(AsyncKey);
	if (transfer->AsyncKey == NULL) {
		LOG_FL_ERR("nomem. error");
		goto error_exit;
	}

	transfer->Path_from = sse_strdup(path_from);
	if (transfer->Path_from == NULL) {
		LOG_FL_ERR("nomem. error");
		goto error_exit;
	}

	transfer->Path_to = sse_strdup(path_to);
	if (transfer->Path_to == NULL) {
		LOG_FL_ERR("nomem. error");
		goto error_exit;
	}

	/* *** now we use temporary file when http donwload *** */
	transfer->save_type = HTTPDL_SAVE_TEMPORARY;
	transfer->direction = direction;

	if (transfer->save_type == HTTPDL_SAVE_TEMPORARY) {
		transfer->tmpfile = sse_strdup(MDL_FILEDL_TEMPFILE);
		if (transfer->tmpfile == NULL) {
			LOG_FL_ERR("nomem. error");
			goto error_exit;
		}
	} else {
		transfer->tmpfile = NULL;
	}

	LOG_FL_DBG("path from: %s", transfer->Path_from);
	LOG_FL_DBG("path to: %s", transfer->Path_to);
	return transfer;

error_exit:
	if (transfer != NULL) {
		fileapp_transfer_free(transfer);
	}
	return NULL;
}

/**
 * @brief	要リダイレクト判定
 *　			ステータスコードからリダイレクトが必要か判定する
 *
 * @param	self MoatHttpResponseインスタンス
 * @return	リダイレクト要非
 */
sse_bool
fileapp_response_need_redirect(MoatHttpResponse *self)
{
  sse_int status_code = 0;
  moat_httpres_get_status_code(self, &status_code);

	switch (status_code) {
	case 301:
	case 302:
	case 303:
	case 307:
		return sse_true;
	default:
		break;
	}
	return sse_false;
}

/**
 * @brief	リダイレクト実行
 *　			ステータスコードからリダイレクトが必要か判定する
 *
 * @param	self ファイル操作アプリコンテキスト
 * @return	リダイレクト要非
 */
static sse_int
fileapp_do_redirect(FileAppContext *self)
{
	MoatHttpResponse *res = NULL;
	MoatHttpRequest *req = NULL;
	sse_char *location = NULL;
	sse_size loc_len;
	sse_int err = SSE_E_GENERIC;
//todo	sse_bool opt = sse_true;
	sse_char *local_path = NULL;
	sse_uint local_path_len = 0;

	res = moat_httpc_get_response(self->transfer->Http);
	if (res == NULL) {
		LOG_FL_ERR("failed to get http response");
		goto error_exit;
	}
	/* リダイレクト先取得 */
	err = moat_httpres_get_redirect_to(res, &location, &loc_len);
	if (err) {
		LOG_FL_ERR("failed to get redirect to");
		goto error_exit;
	}

	if (location == NULL) {
		LOG_FL_ERR("redirect string is null.");
		goto error_exit;
	}

	//LOG_FL_TRC("@@@ redirect to :%.*s", loc_len, location);

	/* HTTPクライアントリセット */
	moat_httpc_reset(self->transfer->Http);

#if 0 //workaround 2013/03/14
	httpc_set_option(self->transfer->Http, HTTP_OPT_ACCEPT_COMPRESSED, &opt,sizeof(sse_bool));
#endif

	/* set saved place to temporary if setting indicate to use temporary file. */
	if (self->transfer->save_type == HTTPDL_SAVE_TEMPORARY) {
		local_path = self->transfer->tmpfile;
		local_path_len = sse_strlen(self->transfer->tmpfile);
	} else {
		local_path = self->transfer->Path_to;
		local_path_len = sse_strlen(self->transfer->Path_to);
	}
	moat_httpc_set_download_file_path(self->transfer->Http, local_path,
			local_path_len);

	/* リダイレクト先へのHTTPリクエスト生成 */
	req = moat_httpc_create_request(self->transfer->Http, MOAT_HTTP_METHOD_GET,
			location, loc_len);
	if (req == NULL) {
		LOG_FL_ERR("failed to create HTTP request (redirect).");
		goto error_exit;
	}

	/* HTTPリクエスト送信 */
	err = moat_httpc_send_request(self->transfer->Http, req);
	if (err) {
		LOG_FL_ERR("failed to send HTTP request.");
		goto error_exit;
	}
	return SSE_E_OK;

error_exit:
	if (req != NULL) {
		moat_httpreq_free(req);
	}
	return SSE_E_GENERIC;
}

/**
 * @brief	ファイル転送完了処理
 *　			ファイル転送の結果を判定し、後続の処理(サーバへの結果通知、設定ファイルの反映など)を実行する。
 *
 * @param	in_err エラーコード
 * @param	in_ctx ファイル操作アプリコンテキスト
 * @return	処理結果
 */
static sse_int
fileapp_transfer_done(sse_int in_err, FileAppContext *in_ctx)
{
	MoatObject *obj = NULL;
	MoatHttpResponse *res;
	MoatEventService *es = NULL;
	sse_int status_code = -1; /* todo its preliminary code */
	sse_int err = SSE_E_GENERIC;
	sse_bool redirect = sse_false;

	/* un-subscribe */LOG_IN();
	err = in_err;

	/* 購読中のIO_READYイベントのunsubscribe */
	es = moat_event_service_get_instance();
	moat_event_service_unsubscribe(es, MOAT_EVENT_IO_READY, handle_transfer_io_ready_cb);
	if (err) {
		LOG_FL_ERR("unexpected error in http session. err: %d", err);
		goto done_and_notify;
	}

	res = moat_httpc_get_response(in_ctx->transfer->Http);
	if (res == NULL) {
		err = SSE_E_GENERIC;
		LOG_FL_ERR("failed to get response err: %d", err);
		goto done_and_notify;
	}

	err = moat_httpres_get_status_code(res, &status_code);
	if (err) {
		LOG_FL_ERR("failed to http_response_get_status_code() [%d]", err);
		goto done_and_notify;
	}
	LOG_FL_DBG(" @@@@@@@@@@ status code: %d @@@@@@@@@@", status_code);

	if ((in_ctx->transfer->direction == HTTP_USE_DIRECTION_DOWNLOAD)
			&& (status_code == 200)) {
		struct stat fileStat;
		char *fpath = NULL;

		/* ダウンロード時はダウンロードしたファイルのチェック */
		if (in_ctx->transfer->save_type != HTTPDL_SAVE_TEMPORARY) {
			fpath = in_ctx->transfer->Path_to;
		} else {
			fpath = in_ctx->transfer->tmpfile;
		}

		LOG_FL_DBG("Call stat() path: %s\n", fpath);
		if (stat(fpath, &fileStat) < 0) {
			LOG_FL_DBG("Error: stat() %s: %s\n", optarg, strerror(errno));
		} else {
			LOG_FL_DBG("Information for %s\n", optarg);
			LOG_FL_DBG("---------------------------\n");
			LOG_FL_DBG("File Size: \t\t%ld bytes\n", fileStat.st_size);
			LOG_FL_DBG("Number of Links: \t%d\n", fileStat.st_nlink);
			LOG_FL_DBG("File inode: \t\t%ld\n", fileStat.st_ino);
			LOG_FL_DBG("\n\n");
			LOG_FL_DBG("The file %s a symbolic link\n",
					(S_ISLNK(fileStat.st_mode)) ? "is" : "is not");
		}
	}

	if ((status_code != 200) && (status_code != 201)) {
		/* redirect check */
		redirect = moat_httpres_need_redirect(res);
		if ((redirect)
				&& (in_ctx->transfer->direction == HTTP_USE_DIRECTION_DOWNLOAD)) {
			/* リダイレクト実行 */
			err = fileapp_do_redirect(in_ctx);
			if (err) {
				LOG_FL_ERR("failed to start redirect request [%d]", err);
				err = SSE_E_INVAL;
				goto done_and_notify;
			} else {
				/* continue */
				es = moat_event_service_get_instance();
				err = moat_event_service_subscribe(es, MOAT_EVENT_IO_READY, handle_transfer_io_ready_cb, in_ctx);
				if (err) {
					LOG_FL_ERR("failed to subscribing. err");
					err = SSE_E_INVAL;
					goto done_and_notify;
				}
				return SSE_E_OK;
			}
		} else {
			LOG_FL_ERR("unexpected status [%d]", status_code);
			err = SSE_E_INVAL;
			goto done_and_notify;
		}
	}

	/* In download case, application move downloaded file to persistent directory. */
	if ((in_ctx->transfer->save_type == HTTPDL_SAVE_TEMPORARY)
			&& (in_ctx->transfer->direction == HTTP_USE_DIRECTION_DOWNLOAD)) {
		if ((status_code == 200) || (status_code == 201)) {
			err = move_file(in_ctx->transfer->tmpfile,
					in_ctx->transfer->Path_to);
			if (err) {
				status_code = -1;
				LOG_FL_ERR("failed to copy file to persistent directory. ");
				goto done_and_notify;
			}
		}
	}

	if ((sse_strcmp(MDL_NAME_CONFIGINF, in_ctx->ModelName) == 0)
			&& (in_ctx->transfer->direction == HTTP_USE_DIRECTION_DOWNLOAD)) {
		/* 設定ファイル反映 */

		if ((status_code == 200) || (status_code == 201)) {
			/* app got new config file, then apply it to device */
			err = fileapp_transfer_apply_config(in_ctx, status_code);
			if (err) {
				status_code = -1;
				LOG_FL_ERR("failed to apply configration");
				goto done_and_notify;
			}
			/* notify should send after reboot in only apply config case */
			fileapp_context_reset(in_ctx);
			LOG_OUT();
			return SSE_E_OK;
		}
	} else if (sse_strcmp(MDL_NAME_SYSLOGINF, in_ctx->ModelName) == 0) {
		if ((status_code == 200) || (status_code == 201)) {
			/* SYSLOGへマーカ設定 */
			fileapp_trace_leave_marker_to_syslog(in_ctx->ModelName,
					MDL_SYSLOG_FACIRITY, MDL_SYSLOG_PRIORITY,
					MDL_SYSLOG_TARGETMARKER);
		}
	}

	goto done_and_notify;

	/* サーバに結果通知 */
	done_and_notify: obj = fileapp_create_fileresult_obj(status_code);
	if (obj != NULL) {
		moat_send_notification(in_ctx->Moat, in_ctx->ServiceID,
				in_ctx->transfer->AsyncKey, in_ctx->ModelName, obj, NULL, NULL);
		moat_object_free(obj);
	} else {
		LOG_FL_ERR("failed to create fileresult object. error");
	}
	/* release having all fields, values, and service id,*/
	fileapp_context_reset(in_ctx);
	LOG_OUT();
	return SSE_E_OK;
}

/**
 * @brief	IO_READYイベントハンドラ
 *　			IO_READYを受信し、HTTPの送受信処理を継続する。
 *
 * @param	in_event_id イベントID
 * @param	in_data イベントデータ
 * @param	in_data_length イベントデータ長
 * @param	in_user_data ユーザデータ
 * @return	処理結果
 */
static sse_int
handle_transfer_io_ready_cb(sse_int in_event_id, sse_pointer in_data,
		sse_uint in_data_length, sse_pointer in_user_data)
{
	FileAppContext *self = (FileAppContext *)in_user_data;
	MoatHttpClient *http = self->transfer->Http;
	sse_int state;
	sse_bool complete = 0;
	sse_int err = SSE_E_OK;

	LOG_IN();
	state = moat_httpc_get_state(http);
	if (state == MOAT_HTTP_STATE_CONNECTING || state == MOAT_HTTP_STATE_SENDING) {
		LOG_FL_DBG("Connecting or Sending: state = %d, complete = %d",
				state, complete);
		err = moat_httpc_do_send(http, &complete);
		LOG_FL_DBG("Return value = %d, complete = %d", err, complete);
		if (err != SSE_E_OK && err != SSE_E_AGAIN) {
			LOG_FL_ERR("failed to moat_httpc_do_send()");
			goto done;
		}
		if (complete) {
			LOG_FL_DBG("do recv");
			err = moat_httpc_recv_response(http);
			if (err != SSE_E_OK && err != SSE_E_AGAIN) {
				LOG_FL_ERR("failed to moat_httpc_recv_response()");
				goto done;
			}
		}
	} else if (state == MOAT_HTTP_STATE_RECEIVING || state == MOAT_HTTP_STATE_RECEIVED) {
		LOG_FL_DBG("Receiving or Received");
		err = moat_httpc_do_recv(http, &complete);
		LOG_FL_DBG("\nreturn from moat_httpc_do_recv()");
		if (err != SSE_E_OK && err != SSE_E_AGAIN) {
			LOG_FL_ERR("@@@ failed to moat_httpc_do_send() = %d @@@",
					err);
			goto done;
		}
		if (complete) {
			LOG_FL_DBG("\n*** receiving complete ***");
			goto done;
		} else {
			LOG_FL_DBG("\n @@@ continue to receive. reason = %d @@@", err);
		}
	} else {
		LOG_FL_ERR("unhandled http state. state=[%d]", state);
		err = SSE_E_INVAL;
		goto done;
	}
	return SSE_E_OK;

done:
	err = fileapp_transfer_done(err, self);
	LOG_OUT();
	return SSE_E_OK;
}

/**
 * @brief	ファイルアップロード実行
 *　			ファイルのアップロードの準備を行い、HTTPリクエストを送信する。
 *
 * @param	self ファイル操作アプリコンテキスト
 * @param	transfer ファイル転送オブジェクト
 * @return	処理結果
 */
static sse_int
fileapp_transfer_upload_file(FileAppContext *self, FileAppTransfer *transfer)
{
	MoatHttpRequest *req = NULL;
	MoatEventService *es;
	sse_int err = SSE_E_INVAL;
	enum moat_http_req_cont_enc_type_ enctype;
	struct stat st;

	LOG_IN();
	if (self == NULL || transfer == NULL) {
		LOG_FL_ERR("unexpected arguments. err");
		return SSE_E_INVAL;
	}

	if (transfer->AsyncKey == NULL || transfer->Http == NULL
			|| transfer->Path_from == NULL || transfer->Path_to == NULL) {
		LOG_FL_ERR("unexpected arguments. err");
		return SSE_E_INVAL;
	}

	LOG_FL_TRC("path_to=%s", transfer->Path_to);
	LOG_FL_TRC("path_from=%s", transfer->Path_from);

	/* 通常ファイルのみアップロード許可 */
	err = stat(transfer->Path_from, &st);
	if (err < 0) {
		LOG_FL_ERR("failed to stat(%s) :%s", transfer->Path_from, strerror(errno));
		return SSE_E_GENERIC;
	}
	if (!S_ISREG(st.st_mode)) {
		LOG_FL_ERR("file=[%s] is not regular file", transfer->Path_from);
		return SSE_E_INVAL;
	}

	/* アップロードするファイルはGZIP圧縮する */
	enctype = MOAT_HTTP_REQ_CONT_ENC_GZIP;
	err = moat_httpc_set_option(transfer->Http, MOAT_HTTP_OPT_REC_CONT_ENC,
			&enctype, sizeof(enctype));
	if (err != SSE_E_OK) {
		LOG_FL_ERR("failed to set content-encoding option: %d", err);
	}

	req = moat_httpc_create_request(transfer->Http, MOAT_HTTP_METHOD_PUT,
			transfer->Path_to, sse_strlen(transfer->Path_to));
	if (req == NULL) {
		LOG_FL_ERR("failed to create http request err");
		return SSE_E_GENERIC;
	}

	moat_httpreq_set_upload_file_path(req, transfer->Path_from,
			sse_strlen(transfer->Path_from), "application/octet-stream",
			sse_strlen("application/octet-stream"));

	es = moat_event_service_get_instance();
	err = moat_event_service_subscribe(es, MOAT_EVENT_IO_READY, handle_transfer_io_ready_cb, self);
	if (err) {
		LOG_FL_ERR("failed to subscribing. err");
		return SSE_E_GENERIC;
	}

	err = moat_httpc_send_request(transfer->Http, req);
	if (err) {
		LOG_FL_ERR("failed to send request via http. err");
		return SSE_E_GENERIC;
	}

	self->transfer = transfer;
	LOG_OUT();
	//return SSE_E_INPROGRESS;
	return SSE_E_OK;
}

/**
 * @brief	ファイルダウンロード実行
 *　			ファイルのダウンロードの準備を行い、HTTPリクエストを送信する。
 *
 * @param	self ファイル操作アプリコンテキスト
 * @param	transfer ファイル転送オブジェクト
 * @return	処理結果
 */
static sse_int
fileapp_transfer_download_file(FileAppContext *self, FileAppTransfer *transfer)
{
	MoatHttpRequest *req = NULL;
	MoatEventService *es;
	sse_int err = SSE_E_INVAL;
//todo	sse_bool opt;

	if (self == NULL || transfer == NULL) {
		LOG_FL_ERR("unexpected arguments. err");
		return SSE_E_INVAL;
	}

	if (transfer->AsyncKey == NULL || transfer->Http == NULL
			|| transfer->Path_from == NULL || transfer->Path_to == NULL) {
		LOG_FL_ERR("unexpected arguments. err");
		return SSE_E_INVAL;
	}

	if (transfer->save_type == HTTPDL_SAVE_TEMPORARY) {
		if (transfer->tmpfile == NULL) {
			LOG_FL_ERR(
					"unexpected arguments(missing temporary file path). err");
			return SSE_E_INVAL;
		}
	}

#if 0 //workaround 2013/03/14
	opt = sse_true;
	moat_httpc_set_option(transfer->Http, HTTP_OPT_ACCEPT_COMPRESSED, &opt,
			sizeof(sse_bool));
#endif

	/* set saved place to temporary if setting indicate to use temporary file. */
	if (transfer->save_type == HTTPDL_SAVE_TEMPORARY) {
		moat_httpc_set_download_file_path(transfer->Http, transfer->tmpfile,
				sse_strlen(transfer->tmpfile));
	} else {
		moat_httpc_set_download_file_path(transfer->Http, transfer->Path_to,
				sse_strlen(transfer->Path_to));
	}

	req = moat_httpc_create_request(transfer->Http, MOAT_HTTP_METHOD_GET,
			transfer->Path_from, sse_strlen(transfer->Path_from));

	es = moat_event_service_get_instance();
	err = moat_event_service_subscribe(es, MOAT_EVENT_IO_READY, handle_transfer_io_ready_cb, self);
	if (err) {
		LOG_FL_ERR("failed to subscribing. err");
		return SSE_E_GENERIC;
	}

	err = moat_httpc_send_request(transfer->Http, req);
	if (err) {
		LOG_FL_ERR("failed to send request via http. err");
		return SSE_E_GENERIC;
	}

	self->transfer = transfer;
	//return SSE_E_INPROGRESS;
	return SSE_E_OK;
}
/////////////////////// Application API (Common) ///////////////////////

/**
 * @brief	ファイル操作アプリコンテキスト生成
 *　			ファイル操作アプリコンテキストを生成する。
 *
 * @param	in_moat MOATハンドル
 * @return	ファイル操作アプリコンテキスト
 */
static FileAppContext *
fileapp_context_new(Moat in_moat)
{
	FileAppContext *c = NULL;

	c = sse_malloc(sizeof(FileAppContext));
	if (c == NULL) {
		return NULL;
	}
	sse_memset(c, 0, sizeof(FileAppContext));
	c->Moat = in_moat;
	return c;
}

/**
 * @brief	ファイル操作アプリコンテキストリセット
 *　			ファイル操作アプリコンテキストの一部情報をリセットする。
 *
 * @param	self ファイル操作アプリコンテキスト
 */
static void
fileapp_context_reset(FileAppContext *self)
{
	if (self != NULL) {
		if (self->ServiceID != NULL) {
			sse_free(self->ServiceID);
			self->ServiceID = NULL;
		}
		if (self->transfer != NULL) {
			fileapp_transfer_free(self->transfer);
			self->transfer = NULL;
		}
	}
}

/**
 * @brief	ファイル操作アプリコンテキスト解放
 *　			ファイル操作アプリコンテキストを解放する。
 *
 * @param	self ファイル操作アプリコンテキスト
 */
static void
fileapp_context_free(FileAppContext *self)
{
	if (self != NULL) {
		if (self->AppID != NULL) {
			sse_free(self->AppID);
			self->AppID = NULL;
		}
		if (self->ModelName != NULL) {
			sse_free(self->ModelName);
			self->ModelName = NULL;
		}
		if (self->SaveKey != NULL) {
			sse_free(self->SaveKey);
			self->SaveKey = NULL;
		}
		if (self->ModelObj != NULL) {
			moat_object_free(self->ModelObj);
			self->ModelObj = NULL;
		}
		fileapp_context_reset(self);
		free(self);
	}
}

#if 0 //no use
static sse_int
fileapp_collection_save(FileAppContext *self)
{
	if (self == NULL) {
		LOG_FL_ERR("invalid args");
		return SSE_E_INVAL;
	}
	return moat_datastore_save_object(self->Moat, self->SaveKey, self->ModelObj);
}

static sse_int
fileapp_collection_load(FileAppContext *self)
{
	MoatObject *obj = NULL;
	sse_int err = SSE_E_OK;

	if (self == NULL) {
		LOG_FL_ERR("invalid args");
		return SSE_E_INVAL;
	}

	err = moat_datastore_load_object(self->Moat, self->SaveKey, &obj);
	if (err) {
		obj = moat_object_new();
		if (obj == NULL) {
			LOG_FL_ERR("failed to load collection. err");
			return SSE_E_NOMEM;
		}
	}
	self->ModelObj = obj;
	return SSE_E_OK;
}

static sse_int
fileapp_override_obj_collection(FileAppContext *self, MoatObject *in_object)
{
	MoatObject *obj = NULL;
	sse_int err;

	if (in_object == NULL || self == NULL) {
		LOG_FL_ERR("invalid args");
		return SSE_E_INVAL;
	}

	obj = moat_object_clone(in_object);
	if (obj == NULL) {
		err = SSE_E_NOMEM;
		LOG_FL_ERR("failed to clone object");
		goto error_exit;
	}

	if(self->ModelObj != NULL) {
		moat_object_free(self->ModelObj);
	}

	self->ModelObj = obj;
	LOG_OBJ("perfomed override - current colleciton: ",self->ModelObj);
	return SSE_E_OK;

error_exit:
	if (obj != NULL) {
		moat_object_free(obj);
	}
	return err;
}
#endif

/**
 * @brief	モデルデータ更新
 *　			ファイル操作アプリコンテキスト内のモデルデータを更新する。
 *
 * @param	self ファイル操作アプリコンテキスト
 * @param	in_object モデルデータ
 * @return	処理結果
 */
static sse_int
fileapp_update_obj_collection(FileAppContext *self, MoatObject *in_object)
{
	MoatObject *obj = NULL;
	MoatObjectIterator *in_obj_ite = NULL;
	MoatValue *in_field_value = NULL;
	sse_char *in_field_name = NULL;
	sse_int err;

	if (in_object == NULL || self == NULL) {
		LOG_FL_ERR("invalid args");
		return SSE_E_INVAL;
	}

	if (self->ModelObj == NULL) {
		/* まだコンテキスト内にモデルデータがない場合は、入力モデルデータを複製し保持 */
		obj = moat_object_clone(in_object);
		if (obj == NULL) {
			err = SSE_E_NOMEM;
			LOG_FL_ERR("failed to clone object");
			goto error_exit;
		}
		self->ModelObj = obj;
	} else {
		/* 既にコンテキスト内にモデルデータがある場合は、入力モデルデータをフィールド毎に更新 */
		in_obj_ite = moat_object_create_iterator(in_object);
		while (moat_object_iterator_has_next(in_obj_ite)) {
			/* retrieve field name and value */
			in_field_name = moat_object_iterator_get_next_key(in_obj_ite);
			in_field_value = moat_object_get_value(in_object, in_field_name);
			err = moat_object_add_value(self->ModelObj, in_field_name,
					in_field_value, sse_true, sse_true);
			if (err != SSE_E_OK) {
				LOG_FL_ERR("failed to add value.");
				goto error_exit;
			}
		}
	}
	LOG_OBJ("perfomed update - current colleciton: ", self->ModelObj);
	return SSE_E_OK;

error_exit:
	if (obj != NULL) {
		moat_object_free(obj);
	}
	return err;
}

/////////////////////// Send Notitication API (Common) ///////////////////////

/**
 * @brief	FileResultオブジェクト生成
 *　			ファイル操作の結果通知に使用するFileResultオブジェクトを生成する。
 *
 * @param	status_code ステータスコード
 * @return	FileResultオブジェクト、失敗時はNULL
 */
static MoatObject *
fileapp_create_fileresult_obj(sse_int status_code)
{
	MoatObject *obj = NULL;
	MoatValue *value = NULL;
	sse_int err = SSE_E_GENERIC;
	sse_char *str_field = NULL;
	sse_char *str_value = NULL;
	sse_bool success = sse_false;
	sse_int idx = 0;
	const sse_char *fieldlist[] = { "code", "success", "message", NULL };

	obj = moat_object_new();
	if (obj == NULL) {
		LOG_FL_ERR("failed to create moat object. err");
		goto done;
	}

	while (fieldlist[idx] != NULL) {

		value = moat_value_new();
		if (value == NULL) {
			LOG_FL_ERR("failed to create value object. err");
			goto done;
		}
		str_field = (sse_char *)fieldlist[idx];
		if (sse_strcmp(str_field, "code") == 0) {
			moat_value_set_int32(value, status_code);

		} else if (sse_strcmp(str_field, "success") == 0) {
			if ((status_code == 200) || (status_code == 201)) {
				success = sse_true;
			} else {
				success = sse_false;
			}
			moat_value_set_boolean(value, success);
		} else if (sse_strcmp(str_field, "message") == 0) {
			if ((status_code == 200) || (status_code == 201)) {
				str_value = "operation complete.";
			} else {
				/* preliminary message */
				str_value = "operation failed";
			}
			moat_value_set_string(value, str_value, 0, sse_true);

		} else {
			LOG_FL_ERR("unknown field name. err");
			goto done;
		}

		err = moat_object_add_value(obj, str_field, value, sse_false, sse_true);
		if (err) {
			LOG_FL_ERR("failed to add value to moat object: %s", str_field);
			goto done;
		}

		idx++;
	}

	LOG_OBJ("Create FileResult Obj", obj);
	return obj;

done:
	if (value != NULL) {
		moat_value_free(value);
	}
	if (obj != NULL) {
		moat_object_free(obj);
	}
	return NULL;
}

/**
 * @brief	非同期処理実行開始コールバック
 *　			非同期コマンド実行時のコールバック。
 *			実際のコマンド処理の実行を開始する。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @param	処理結果
 */
static sse_int
fileapp_async_command_cb(Moat in_moat, sse_char *in_uid, sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
	FileAppContext *self = (FileAppContext *)in_model_context;
	sse_int err = SSE_E_GENERIC;
	MoatObject *obj = NULL;
	sse_int status_code = -1;

	if (self->transfer->direction == HTTP_USE_DIRECTION_DOWNLOAD) {
		err = fileapp_transfer_download_file(self, self->transfer);
	} else {
		err = fileapp_transfer_upload_file(self, self->transfer);
	}

	if(err) {
		obj = fileapp_create_fileresult_obj(status_code);
		if (obj != NULL) {
			moat_send_notification(self->Moat, self->ServiceID,
					self->transfer->AsyncKey, self->ModelName, obj, NULL, NULL);
			moat_object_free(obj);
		} else {
			LOG_FL_ERR("failed to create fileresult object. error");
		}
		/* release having all fields, values, and service id,*/
		fileapp_context_reset(self);
	}
	return err;
}

/////////////////////// Mapper API (Common) ///////////////////////

/**
 * @brief	モデルデータ更新処理
 *　			ContentInfo, ConfigurationInfo, SyslogInfoモデルのモデルデータ更新処理。
 *			上記モデルのUpdateメソッドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_object 更新モデルデータ
 * @param	in_model_context モデルコンテキスト
 * @return	処理結果
 */
static sse_int
File_UpdateProc(Moat in_moat, sse_char *in_uid, MoatObject *in_object,
		sse_pointer in_model_context)
{
	FileAppContext *self = NULL;
	int err = SSE_E_GENERIC;

	/* verify arguments */
	if (in_object == NULL || in_model_context == NULL) {
		LOG_FL_ERR("unexpected arguments. err");
		return SSE_E_INVAL;
	}

	self = (FileAppContext *)in_model_context;
	err = fileapp_update_obj_collection(self, in_object);
	if (err) {
		LOG_FL_ERR("failed to update obj collection. error");
		return SSE_E_INVAL;
	}

	return err;
}

/**
 * @brief	モデルデータ部分更新処理
 *　			ContentInfo, ConfigurationInfo, SyslogInfoモデルのモデルデータ部分更新処理。
 *			上記モデルのUpdateメソッドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_object 更新モデルデータ
 * @param	in_model_context モデルコンテキスト
 * @return	処理結果
 */
static sse_int
File_UpdateFieldsProc(Moat in_moat, sse_char *in_uid, MoatObject *in_object,
		sse_pointer in_model_context)
{
	FileAppContext *self = NULL;
	int err;

	/* verify arguments */
	if (in_object == NULL || in_model_context == NULL) {
		LOG_FL_ERR("unexpected arguments. err");
		return SSE_E_INVAL;
	}

	self = (FileAppContext *)in_model_context;
	err = fileapp_update_obj_collection(self, in_object);
	if (err) {
		LOG_FL_ERR("failed to update obj collection. error");
		return SSE_E_INVAL;
	}

	return err;
}

/////////////////////// ContentInfo Command API ///////////////////////

/**
 * @brief	ファイル配信コマンド処理
 *　			ContentInfoモデルのdownloadコマンドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @return	正常終了時は、SSE_E_INPROGRESSを返す。SSE_E_INPROGRESS以外はエラー
 */
sse_int
ContentInfo_download(Moat in_moat, sse_char *in_uid, sse_char *in_key,
		MoatValue *in_data, sse_pointer in_model_context)
{
	FileAppContext *self = NULL;
	sse_char *deliveryUrl;
	sse_char *destinationPath;
	MoatValue *value;
	FileAppTransfer *transfer;
	int err = SSE_E_INVAL;

	LOG_IN();
	if (in_key == NULL || in_data == NULL || in_model_context == NULL) {
		LOG_FL_ERR("invalid args. err");
		return err;
	}

	self = (FileAppContext *)in_model_context;

	if (self->ModelObj == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory all fields. err");
		return err;
	}
	LOG_FL_DBG("modelname = %s", self->ModelName);
	LOG_OBJ("received obj", self->ModelObj);

	self->ServiceID = create_notification_id(self->AppID,
			MDL_CONTENT_SERVICEID_DELIVERY);
	if (self->ServiceID == NULL) {
		LOG_FL_ERR("failed to create service id, err.");
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_CONTENT_DELIVERY);
	deliveryUrl = fileapp_util_clone_string_value(value);
	if (deliveryUrl == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory field. %s. err", MDL_CONTENT_DELIVERY);
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_CONTENT_DESTINATION);
	destinationPath = fileapp_util_clone_string_value(value);
	if (destinationPath == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory field. %s. err", MDL_CONTENT_DESTINATION);
		goto exit;
	}

	transfer = fileapp_transfer_new(in_key, deliveryUrl, destinationPath,
			HTTP_USE_DIRECTION_DOWNLOAD);
	if (transfer == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("failed to http initialization. err");
		goto exit;
	}
	sse_free(deliveryUrl);
	deliveryUrl = NULL;
	sse_free(destinationPath);
	destinationPath = NULL;

	self->transfer = transfer;
	/* 非同期で処理を開始 */
	err = moat_start_async_command(in_moat, in_uid, in_key, in_data, fileapp_async_command_cb, self);
	if (err) {
		LOG_FL_ERR("failed to start async command");
		goto exit;
	}
	return SSE_E_INPROGRESS;

exit:
	if (deliveryUrl != NULL) {
		sse_free(deliveryUrl);
	}
	if (destinationPath != NULL) {
		sse_free(destinationPath);
	}
	LOG_OUT();
	return err;

}

/**
 * @brief	ファイル取得コマンド処理
 *　			ContentInfoモデルのuploadコマンドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @return	正常終了時は、SSE_E_INPROGRESSを返す。SSE_E_INPROGRESS以外はエラー
 */
sse_int
ContentInfo_upload(Moat in_moat, sse_char *in_uid, sse_char *in_key,
		MoatValue *in_data, sse_pointer in_model_context)
{
	FileAppContext *self = NULL;
	sse_char *uploadUrl;
	sse_char *sourcePath;
	MoatValue *value;
	FileAppTransfer *transfer;
	int err = SSE_E_INVAL;

	LOG_IN();
	if (in_key == NULL || in_data == NULL || in_model_context == NULL) {
		LOG_FL_ERR("invalid args. err");
		return err;
	}

	self = (FileAppContext *)in_model_context;

	if (self->ModelObj == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory all fields. err");
		return err;
	}
	LOG_FL_DBG("modelname = %s", self->ModelName);
	LOG_OBJ("received obj", self->ModelObj);

	self->ServiceID = create_notification_id(self->AppID,
			MDL_CONTENT_SERVICEID_FETCH);
	if (self->ServiceID == NULL) {
		LOG_FL_ERR("failed to create service id, err.");
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_CONTENT_UPLOAD);
	uploadUrl = fileapp_util_clone_string_value(value);
	if (uploadUrl == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory field. %s. err", MDL_CONTENT_UPLOAD);
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_CONTENT_SOURCE);
	sourcePath = fileapp_util_clone_string_value(value);
	if (sourcePath == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory field. %s. err", MDL_CONTENT_SOURCE);
		goto exit;
	}
	transfer = fileapp_transfer_new(in_key, sourcePath, uploadUrl,
			HTTP_USE_DIRECTION_UPLOAD);
	if (transfer == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("failed to http initialization. err");
		goto exit;
	}
	sse_free(sourcePath);
	sourcePath = NULL;
	sse_free(uploadUrl);
	uploadUrl = NULL;

	self->transfer = transfer;
	err = moat_start_async_command(in_moat, in_uid, in_key, in_data, fileapp_async_command_cb, self);
	if (err) {
		LOG_FL_ERR("failed to start async command.");
		goto exit;
	}
	return SSE_E_INPROGRESS;

exit:
	if (uploadUrl != NULL) {
		sse_free(uploadUrl);
	}
	if (sourcePath != NULL) {
		sse_free(sourcePath);
	}
	LOG_OUT();
	return err;
}

/////////////////////// ConfigrationInfo Command API ///////////////////////

/**
 * @brief	設定ファイル配信コマンド処理
 *　			ConfigurationInfoモデルのdownloadAndApplyコマンドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @return	正常終了時は、SSE_E_INPROGRESSを返す。SSE_E_INPROGRESS以外はエラー
 */
sse_int
ConfigurationInfo_downloadAndApply(Moat in_moat, sse_char *in_uid,
		sse_char *in_key, MoatValue *in_data, sse_pointer in_model_context)
{
	FileAppContext *self = NULL;
	sse_char *deliveryUrl;
	MoatValue *value;
	FileAppTransfer *transfer;
	sse_int err = SSE_E_INVAL;

	LOG_IN();
	if (in_key == NULL || in_data == NULL || in_model_context == NULL) {
		LOG_FL_ERR("invalid args. err");
		return err;
	}

	self = (FileAppContext *)in_model_context;

	if (self->ModelObj == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory all fields. err");
		return err;
	}
	LOG_FL_DBG("modelname = %s", self->ModelName);
	LOG_OBJ("received obj", self->ModelObj);

	self->ServiceID = create_notification_id(self->AppID,
			MDL_CONFIG_SERVICEID_DELIVERY);
	if (self->ServiceID == NULL) {
		LOG_FL_ERR("failed to create service id, err.");
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_CONFIG_DELIVERY);
	deliveryUrl = fileapp_util_clone_string_value(value);
	if (deliveryUrl == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory field. %s. err", MDL_CONFIG_DELIVERY);
		return err;
	}

	transfer = fileapp_transfer_new(in_key, deliveryUrl,
			MDL_CONFIG_FIXED_CONFNAME, HTTP_USE_DIRECTION_DOWNLOAD);
	if (transfer == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("failed to http initialization. err");
		goto exit;
	}
	sse_free(deliveryUrl);
	deliveryUrl = NULL;

	self->transfer = transfer;
	err = moat_start_async_command(in_moat, in_uid, in_key, in_data, fileapp_async_command_cb, self);
	if (err) {
		LOG_FL_ERR("failed to start async command.");
		goto exit;
	}
	return SSE_E_INPROGRESS;

exit:
	if (deliveryUrl != NULL) {
		sse_free(deliveryUrl);
	}
	LOG_OUT();
	return err;
}

/**
 * @brief	設定ファイル取得コマンド処理
 *　			ConfigurationInfoモデルのuploadConfigコマンドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @return	正常終了時は、SSE_E_INPROGRESSを返す。SSE_E_INPROGRESS以外はエラー
 */
sse_int
ConfigurationInfo_uploadConfig(Moat in_moat, sse_char *in_uid, sse_char *in_key,
		MoatValue *in_data, sse_pointer in_model_context)
{
	FileAppContext *self = NULL;
	sse_char *uploadUrl = NULL;
	MoatValue *value;
	FileAppTransfer *transfer;
	int err = SSE_E_INVAL;

	LOG_IN();
	if (in_key == NULL || in_data == NULL || in_model_context == NULL) {
		LOG_FL_ERR("invalid args. err");
		return err;
	}

	self = (FileAppContext *)in_model_context;

	if (self->ModelObj == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory all fields. err");
		return err;
	}
	LOG_FL_DBG("modelname = %s", self->ModelName);
	LOG_OBJ("received obj", self->ModelObj);

	self->ServiceID = create_notification_id(self->AppID,
			MDL_CONFIG_SERVICEID_FETCH);
	if (self->ServiceID == NULL) {
		LOG_FL_ERR("failed to create service id, err.");
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_CONFIG_UPLOAD);
	uploadUrl = fileapp_util_clone_string_value(value);
	if (uploadUrl == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory field. %s. err", MDL_CONFIG_UPLOAD);
		return err;
	}

	transfer = fileapp_transfer_new(in_key, MDL_CONFIG_FIXED_CONFNAME,
			uploadUrl, HTTP_USE_DIRECTION_UPLOAD);
	if (transfer == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("failed to http initialization. err");
		err = SSE_E_INVAL;
		goto exit;
	}
	sse_free(uploadUrl);
	uploadUrl = NULL;

	self->transfer = transfer;
	err = moat_start_async_command(in_moat, in_uid, in_key, in_data, fileapp_async_command_cb, self);
	if (err) {
		LOG_FL_ERR("failed to start async command.");
		goto exit;
	}
	return SSE_E_INPROGRESS;

exit:
	if (uploadUrl != NULL) {
		sse_free(uploadUrl);
	}
	LOG_OUT();
	return err;
}

/////////////////////// SysloInfo Command API ///////////////////////
/**
 * @brief	SYSLOG取得コマンド処理
 *　			SyslogInfoモデルのuploadコマンドの実装。
 *
 * @param	in_moat MOATハンドル
 * @param	in_uid UUID
 * @param	in_key 非同期実行キー
 * @param	in_data コマンドパラメータ
 * @param	in_model_context モデルコンテキスト
 * @return	正常終了時は、SSE_E_INPROGRESSを返す。SSE_E_INPROGRESS以外はエラー
 */
sse_int
SyslogInfo_upload(Moat in_moat, sse_char *in_uid, sse_char *in_key,
		MoatValue *in_data, sse_pointer in_model_context)
{
	FileAppContext *self = NULL;
	sse_char *uploadUrl;
	sse_int32 maxLogs;
	MoatValue *value;
	FileAppTransfer *transfer;
	int err = SSE_E_INVAL;

	LOG_IN();
	if (in_key == NULL || in_data == NULL || in_model_context == NULL) {
		LOG_FL_ERR("invalid args. err");
		return err;
	}

	self = (FileAppContext *)in_model_context;

	if (self->ModelObj == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory all fields. err");
		return err;
	}
	LOG_FL_DBG("modelname = %s", self->ModelName);
	LOG_OBJ("received obj", self->ModelObj);

	self->ServiceID = create_notification_id(self->AppID,
			MDL_SYSLOG_SERVICEID_FETCH);
	if (self->ServiceID == NULL) {
		LOG_FL_ERR("failed to create service id, err.");
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_SYSLOG_UPLOAD);
	uploadUrl = fileapp_util_clone_string_value(value);
	if (uploadUrl == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("missing mandatory field. %s. err", MDL_SYSLOG_UPLOAD);
		return err;
	}

	value = moat_object_get_value(self->ModelObj, MDL_SYSLOG_MAXLOG);
	err = moat_value_get_int32(value, &maxLogs);
	if (err) {
		/* mandatory field missing. error */
		LOG_FL_ERR("failed to get value from %s field. err", MDL_SYSLOG_MAXLOG);
		return err;
	}

	/* create uploaded file which is packed syslog file(s) into one file */
	err = fileapp_collect_syslog(MDL_SYSLOG_FIXED_LOGPATH, maxLogs,
			MDL_SYSLOG_TARGETMARKER, MDL_SYSLOG_FIXED_UPFILE);
	if (err) {
		/* mandatory field missing. error */
		LOG_FL_ERR("failed to collect syslog. err");
		goto exit;
	}

	/* send */
	transfer = fileapp_transfer_new(in_key, MDL_SYSLOG_FIXED_UPFILE, uploadUrl,
			HTTP_USE_DIRECTION_UPLOAD);
	if (transfer == NULL) {
		/* mandatory field missing. error */
		LOG_FL_ERR("failed to http initialization. err");
		goto exit;
	}
	sse_free(uploadUrl);
	uploadUrl = NULL;

	self->transfer = transfer;
	err = moat_start_async_command(in_moat, in_uid, in_key, in_data, fileapp_async_command_cb, self);
	if (err) {
		LOG_FL_ERR("failed to start async command.");
		goto exit;
	}
	return SSE_E_INPROGRESS;

exit:
	if (uploadUrl != NULL) {
		sse_free(uploadUrl);
	}
	LOG_OUT();
	return err;
}

/////////////////////// Entry Point ///////////////////////

/**
 * @brief	ファイル操作アプリエントリポイント
 *　			ファイル操作アプリのメイン関数。
 *
 * @param	argc 引数の数
 * @param	引数
 * @return	終了コード
 */
sse_int
moat_app_main(sse_int argc, sse_char *argv[])
{
	Moat moat;
	ModelMapper mapper;
	sse_int err = SSE_E_GENERIC;
	FileAppContext *contentinfo_context = NULL;
	FileAppContext *configrationinfo_context = NULL;
	FileAppContext *sysloginfo_context = NULL;
	sse_int result = EXIT_SUCCESS;
	LOG_FL_DBG("h-e-l-l-(o) m-o-a-t...");
	err = moat_init(argv[0], &moat);

	if (err != SSE_E_OK) {
		LOG_FL_ERR("failed to initialize.");
		result = EXIT_FAILURE;
		goto done;
	}

	/* ContentInfoモデル用コンテキスト生成 */
	contentinfo_context = fileapp_context_new(moat);
	if (contentinfo_context == NULL) {
		LOG_FL_ERR("failed to create context.");
		return EXIT_FAILURE;
	} else {
		contentinfo_context->ModelName = sse_strdup(MDL_NAME_CONTENTINF);
		contentinfo_context->AppID = sse_strdup(argv[0]);
		contentinfo_context->SaveKey = sse_strdup(MDL_KEY_CONTENTINF);
		if (contentinfo_context->ModelName == NULL
		|| contentinfo_context->AppID == NULL
		|| contentinfo_context->SaveKey == NULL) {
			LOG_FL_ERR("nomemory.");
			return EXIT_FAILURE;
		}
	}

	/* ConfigurationInfoモデル用コンテキスト生成 */
	configrationinfo_context = fileapp_context_new(moat);
	if (configrationinfo_context == NULL) {
		LOG_FL_ERR("failed to create context.");
		result = EXIT_FAILURE;
		goto done;
	} else {
		configrationinfo_context->ModelName = sse_strdup(MDL_NAME_CONFIGINF);
		configrationinfo_context->AppID = sse_strdup(argv[0]);
		configrationinfo_context->SaveKey = sse_strdup(MDL_KEY_CONFIGINF);
		if (configrationinfo_context->ModelName == NULL
		|| configrationinfo_context->AppID == NULL
		|| configrationinfo_context->SaveKey == NULL) {
			LOG_FL_ERR("nomemory.");
			return EXIT_FAILURE;
		}

	}

	/* SyslogInfoモデル用コンテキスト生成 */
	sysloginfo_context = fileapp_context_new(moat);
	if (sysloginfo_context == NULL) {
		LOG_FL_ERR("failed to create context.");
		result = EXIT_FAILURE;
		goto done;
	} else {
		sysloginfo_context->ModelName = sse_strdup(MDL_NAME_SYSLOGINF);
		sysloginfo_context->AppID = sse_strdup(argv[0]);
		sysloginfo_context->SaveKey = sse_strdup(MDL_KEY_SYSLOGINF);
		if (sysloginfo_context->ModelName == NULL
		|| sysloginfo_context->AppID == NULL
		|| sysloginfo_context->SaveKey == NULL) {
			LOG_FL_ERR("nomemory.");
			return EXIT_FAILURE;
		}

	}

	/* ContentInfo用Mapper設定、モデル登録 */
	mapper.AddProc = NULL;
	mapper.RemoveProc = NULL;
	mapper.UpdateProc = File_UpdateProc;
	mapper.UpdateFieldsProc = File_UpdateFieldsProc;
	mapper.FindAllUidsProc = NULL;
	mapper.FindByUidProc = NULL;
	mapper.CountProc = NULL;
	err = moat_register_model(moat, "ContentInfo", &mapper,
			contentinfo_context);
	if (err != SSE_E_OK) {
		LOG_FL_ERR("failed to register model.");
		result = EXIT_FAILURE;
		goto done;
	}

	/* ConfigurationInfo用Mapper設定、モデル登録 */
	mapper.AddProc = NULL;
	mapper.RemoveProc = NULL;
	mapper.UpdateProc = File_UpdateProc;
	mapper.UpdateFieldsProc = File_UpdateFieldsProc;
	mapper.FindAllUidsProc = NULL;
	mapper.FindByUidProc = NULL;
	mapper.CountProc = NULL;
	err = moat_register_model(moat, MDL_NAME_CONFIGINF, &mapper,
			configrationinfo_context);
	if (err != SSE_E_OK) {
		LOG_FL_ERR("failed to register model.");
		result = EXIT_FAILURE;
		goto done;
	}

	/* SyslogInfo用Mapper設定、モデル登録 */
	mapper.AddProc = NULL;
	mapper.RemoveProc = NULL;
	mapper.UpdateProc = File_UpdateProc;
	mapper.UpdateFieldsProc = File_UpdateFieldsProc;
	mapper.FindAllUidsProc = NULL;
	mapper.FindByUidProc = NULL;
	mapper.CountProc = NULL;
	err = moat_register_model(moat, "SyslogInfo", &mapper, sysloginfo_context);
	if (err != SSE_E_OK) {
		LOG_FL_ERR("failed to register model.");
		result = EXIT_FAILURE;
		goto done;
	}

	/* 設定ファイル反映結果があれば、サーバに結果通知 */
	err = fileapp_transfer_notify_apply_config(configrationinfo_context,
			MDL_KEY_CONFIG_LEAVE_INF);
	if (err) {
		LOG_FL_ERR("failed to send notify regarding configuration apply. err");
		result = EXIT_FAILURE;
		goto done;
	}

	/* イベントループ実行 */
	moat_run(moat);

done:
	moat_unregister_model(moat, "ContentInfo");
	moat_unregister_model(moat, "ConfigrationInfo");
	moat_unregister_model(moat, "SyslogInfo");
	fileapp_context_free(contentinfo_context);
	fileapp_context_free(configrationinfo_context);
	fileapp_context_free(sysloginfo_context);
	moat_destroy(moat);
	LOG_FL_DBG("file end.");
	return result;
}
