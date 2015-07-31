/*
 * LEGAL NOTICE
 *
 * Copyright (C) 2012-2015 InventIt Inc. All rights reserved.
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

#include <servicesync/moat.h>
#include <sseutils.h>
#include <file/file.h>

#define TAG "File"
#define LOG_ERROR(format, ...) MOAT_LOG_ERROR(TAG, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  MOAT_LOG_WARN(TAG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  MOAT_LOG_INFO(TAG, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) MOAT_LOG_DEBUG(TAG, format, ##__VA_ARGS__)
#define LOG_TRACE(format, ...) MOAT_LOG_TRACE(TAG, format, ##__VA_ARGS__)
#include <stdlib.h>
#define ASSERT(cond) if(!(cond)) { LOG_ERROR("ASSERTION FAILED:" #cond); abort(); }

static void TFILEDownloader_DoPreAction(TFILEDownloader *self);
static void FILEDownloader_DoPreActionOnCompletedCallback(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_result);
static void FILEDownloader_DoPreActionOnReadCallback(TSseUtilShellCommand* self, sse_pointer in_user_data);
static void FILEDownloader_DoPreActionOnErrorCallback(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_error_code, const sse_char* in_message);
static void TFILEDownloader_DoDownload(TFILEDownloader *self);
static void FILEDownloader_OnDownloadCompletionCallback(MoatDownloader *in_dl, sse_bool in_canceled, sse_pointer in_user_data);
static void FILEDownlaoder_OnDownloadErrorCallback(MoatDownloader *in_dl, sse_int in_err_code, sse_pointer in_user_data);
static void TFILEDownloader_DoCopy(TFILEDownloader *self);
static void TFILEDownloader_DoPostAction(TFILEDownloader *self);
static void FILEDownloader_DoPostActionOnCompletedCallback(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_result);
static void FILEDownloader_DoPostActionOnReadCallback(TSseUtilShellCommand* self, sse_pointer in_user_data);
static void FILEDownloader_DoPostActionOnErrorCallback(TSseUtilShellCommand* self, sse_pointer in_user_data, sse_int in_error_code, const sse_char* in_message);
static void TFILEDownloader_CallOnCompleteCallback(TFILEDownloader *self);
static sse_int TFILEDownloader_StoreResultCode(TFILEDownloader *self, const sse_char *in_err_code, const sse_char *in_err_msg, sse_bool in_overwrite);

/*
 * Do pre-action
 */

static void
TFILEDownloader_DoPreAction(TFILEDownloader *self)
{
  sse_int err;
  MoatValue *preaction;
  sse_char *str;
  sse_uint len;
  sse_char *cmd;

  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);

  preaction = TFILEFilesysInfo_GetPreAction(self->fFilesysInfo);
  if (preaction == NULL) {
      LOG_DEBUG("No pre-action. so download the file.");
      TFILEDownloader_DoDownload(self);
      return;
  }

  err = moat_value_get_string(preaction, &str, &len);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_value_get_string() has been failed with [%s].", sse_get_error_string(err));
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_CONF, "Invalid pre-action script configuration.", sse_false);
    return;
  }
  cmd = sse_strndup(str, len);
  ASSERT(cmd);

  LOG_INFO("Execute pre-action=[%s].", cmd);
  self->fPreAction = SseUtilShellCommand_New();
  ASSERT(self->fPreAction);
  
  err = TSseUtilShellCommand_SetShellCommand(self->fPreAction, cmd);
  sse_free(cmd);
  if (err != SSE_E_OK) {
    LOG_ERROR("TSseUtilShellCommand_SetShellCommand() has been failed with [%s].", sse_get_error_string(err));
    TSseUtilShellCommand_Delete(self->fPreAction);
    self->fPreAction = NULL;
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_EXEC, "Executing pre-action script has been failed.", sse_false);
    return;
  }

  TSseUtilShellCommand_SetOnComplatedCallback(self->fPreAction,
                                              FILEDownloader_DoPreActionOnCompletedCallback,
                                              self);
  TSseUtilShellCommand_SetOnReadCallback(self->fPreAction,
                                         FILEDownloader_DoPreActionOnReadCallback,
                                         self);
  TSseUtilShellCommand_SetOnErrorCallback(self->fPreAction,
                                          FILEDownloader_DoPreActionOnErrorCallback,
                                          self);

  err = TSseUtilShellCommand_Execute(self->fPreAction);
  if (err != SSE_E_OK) {
    LOG_ERROR("TSseUtilShellCommand_Execute() has been failed with [%s].", sse_get_error_string(err));
    TSseUtilShellCommand_Delete(self->fPreAction);
    self->fPreAction = NULL;
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_EXEC, "Executing pre-action script has been failed.", sse_true);
    return;
  }

  return;
}

static void
FILEDownloader_DoPreActionOnCompletedCallback(TSseUtilShellCommand* self,
                                              sse_pointer in_user_data,
                                              sse_int in_result)
{
  TFILEDownloader *downloader = (TFILEDownloader*)in_user_data;
  ASSERT(self);

  if (in_result != SSE_E_OK) {
    LOG_ERROR("Pre-action(%s) has been failed with [%s].", self->fShellCommand, sse_get_error_string(in_result));
    TFILEDownloader_StoreResultCode(downloader, FILE_ERROR_EXEC, "Executing pre-action script has been failed.", sse_true);
    return;
  }

  LOG_INFO("Pre-action(%s) has been completed successfully.", self->fShellCommand);
  TFILEDownloader_DoDownload(downloader);
}

static void
FILEDownloader_DoPreActionOnReadCallback(TSseUtilShellCommand* self,
                                         sse_pointer in_user_data)
{
  sse_int err;
  sse_char *buff;
  TFILEDownloader *downloader = (TFILEDownloader*)in_user_data;
  ASSERT(self);
  ASSERT(downloader);

  err = TSseUtilShellCommand_ReadLine(self, &buff, sse_true);
  if (err != SSE_E_OK) {
    LOG_ERROR("TSseUtilShellCommand_ReadLine() has been failed with [%s].", sse_get_error_string(err));
    TFILEDownloader_StoreResultCode(downloader, FILE_ERROR_EXEC, "Executing pre-action script has been failed.", sse_true);
    return;
  }

  LOG_DEBUG("%s=[%s]", self->fShellCommand, buff);
  sse_free(buff);
  return;
}

static void
FILEDownloader_DoPreActionOnErrorCallback(TSseUtilShellCommand* self,
                                          sse_pointer in_user_data,
                                          sse_int in_error_code,
                                          const sse_char* in_message)
{
  TFILEDownloader *downloader = (TFILEDownloader*)in_user_data;
  ASSERT(self);
  ASSERT(downloader);

  LOG_ERROR("TSseUtilShellCommand_ReadLine() has been failed with [%s], message=[%s].", sse_get_error_string(in_error_code), in_message);
  TFILEDownloader_StoreResultCode(downloader, FILE_ERROR_EXEC, "Executing pre-action script has been failed.", sse_false);
  TFILEDownloader_CallOnCompleteCallback(downloader);
}

/*
 * Do download
 */

static void
TFILEDownloader_DoDownload(TFILEDownloader *self)
{
  sse_int err;
  sse_char *str;
  sse_uint len;
  MoatValue *dl_dir;
  MoatValue *basename = NULL;
  SSEString *path = NULL;
  sse_char src_url[1024];
  sse_char dst_path[1024];

  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);

  if ((self->fUrl == NULL) || (self->fFilePath == NULL)) {
    LOG_ERROR("Source URL or local file path does not specifiled, source=[%p], destination=[%p].", self->fUrl, self->fFilePath);
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_INVAL, "Source URL or local file path does not specifiled.", sse_false);
    TFILEDownloader_DoPostAction(self);
    goto error_exit;
  }

  /* Get the source URL */
  err = moat_value_get_string(self->fUrl, &str, &len);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_value_get_string() has been failed with [%s].", sse_get_error_string(err));
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_INVAL, "Could not find the source URL.", sse_false);
    TFILEDownloader_DoPostAction(self);
    goto error_exit;
  }
  if (len > sizeof(src_url)) {
    LOG_ERROR("Too long source URL (%d).", len);
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_INVAL, "Too long source URL.", sse_false);
    TFILEDownloader_DoPostAction(self);
    goto error_exit;
  }
  sse_strncpy(src_url, str, len);
  src_url[len] = '\0';


  /* Get the directory path for download, then tests an accessability to store temporary file. */
  dl_dir = TFILEFilesysInfo_GetTmpDir(self->fFilesysInfo);
  if (dl_dir == NULL) {
    LOG_INFO("Directory for download does not specified, so use /tmp.");
    path = sse_string_new("/tmp");
    ASSERT(path);
  } else {
    err = moat_value_get_string(dl_dir, &str, &len);
    if (err == SSE_E_OK) {
      if (SseUtilFile_IsDirectory(dl_dir)) {
        path = sse_string_new_with_length(str, len);
        ASSERT(path);
      } else {
        LOG_WARN("Does not able to access to the download directory, so use /tmp.");
        path = sse_string_new("/tmp");
        ASSERT(path);      
      }
    } else {
      LOG_WARN("moat_value_get_string() has been failed with [%s], so use /tmp", sse_get_error_string(err));
      path = sse_string_new("/tmp");
      ASSERT(path);
    }
  }

  /* Create a tentative destination file path, ${DOWNLOAD_DIR}/${ORIGIN_FILENAME}.part.*/
  err = SseUtilFile_GetFileName(self->fFilePath, &basename);
  if (err != SSE_E_OK) {
    LOG_ERROR("SseUtilFile_GetFileName() has been failed with [%s].", sse_get_error_string(err));
    MOAT_VALUE_DUMP_ERROR(TAG, self->fFilePath);
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_INVAL, "Could not find a destination file name.", sse_false);
    TFILEDownloader_DoPostAction(self);
    goto error_exit;
  }

  err = sse_string_concat_cstr(path, "/");             ASSERT(err == SSE_E_OK);
  err = moat_value_get_string(basename, &str, &len);   ASSERT(err == SSE_E_OK);
  err = sse_string_concat_with_length(path, str, len); ASSERT(err == SSE_E_OK);
  err = sse_string_concat_cstr(path, ".part");         ASSERT(err == SSE_E_OK);
  if (sse_string_get_length(path) > sizeof(dst_path)) {
    LOG_ERROR("Too long destination file path (len=%d).", sse_string_get_length(path));
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_INVAL, "Too long destination file name.", sse_false);
    goto error_exit;
  }
  sse_strncpy(dst_path, sse_string_get_cstr(path), sse_string_get_length(path));
  dst_path[sse_string_get_length(path)] = '\0';
  self->fTmpFilePath = moat_value_new_string(dst_path, 0, sse_true);
  ASSERT(self->fTmpFilePath);

  /* Download the file from web storage. */
  LOG_INFO("Download the file, source=[%s] to local=[%s].", src_url, dst_path);
  err = moat_downloader_download(self->fDownloader, src_url, sse_strlen(src_url), dst_path);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_downloader_download() has been failed with [%s].", sse_get_error_string(err));
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_DOWNLOAD, "File download failure.", sse_false);
    TFILEDownloader_DoPostAction(self);
    goto error_exit;
  }

  moat_value_free(basename);
  sse_string_free(path, sse_true);
  return;

 error_exit:
  if (basename) moat_value_free(basename);
  if (path)     sse_string_free(path, sse_true);
}

static void
FILEDownloader_OnDownloadCompletionCallback(MoatDownloader *in_dl,
                                            sse_bool in_canceled,
                                            sse_pointer in_user_data)
{
  TFILEDownloader *downloader;

  downloader = (TFILEDownloader *)in_user_data;
  ASSERT(downloader);

  if (in_canceled) {
    LOG_INFO("Download has been canceled.");
    TFILEDownloader_DoPostAction(downloader);
  } else {
    LOG_INFO("Download has been completed.");
    TFILEDownloader_DoCopy(downloader);
  }

  return;
}

static void
FILEDownlaoder_OnDownloadErrorCallback(MoatDownloader *in_dl,
                                       sse_int in_err_code,
                                       sse_pointer in_user_data)
{
  TFILEDownloader *downloader;

  downloader = (TFILEDownloader *)in_user_data;
  ASSERT(downloader);

  LOG_ERROR("Download has been failed with [%d].", in_err_code);
  MOAT_VALUE_DUMP_ERROR(TAG, downloader->fUrl);
  MOAT_VALUE_DUMP_ERROR(TAG, downloader->fFilePath);

  /* Cleanup the temporary file. */
  if (SseUtilFile_IsFile(downloader->fTmpFilePath)) {
    LOG_INFO("Delete the temporary file. The file might be incomplete because download has been failed.");
    SseUtilFile_DeleteFile(downloader->fTmpFilePath);
  }

  TFILEDownloader_StoreResultCode(downloader, FILE_ERROR_DOWNLOAD, "File download failure.", sse_false);
  TFILEDownloader_DoPostAction(downloader);
  return;
}

/*
 * Do copy
 */

static void
TFILEDownloader_DoCopy(TFILEDownloader *self)
{
  sse_int err;

  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);

  err = SseUtilFile_MoveFile(self->fTmpFilePath, self->fFilePath);
  if (err != SSE_E_OK) {
    LOG_ERROR("SseUtilFile_MoveFile() has been failed with [%s].", sse_get_error_string(err));
    MOAT_VALUE_DUMP_ERROR(TAG, self->fTmpFilePath);
    MOAT_VALUE_DUMP_ERROR(TAG, self->fFilePath);
    if (err == SSE_E_ACCES) {
      TFILEDownloader_StoreResultCode(self, FILE_ERROR_ACCES, "Rename file has been faield.", sse_false);
    } else if (err == SSE_E_NOMEM) {
      TFILEDownloader_StoreResultCode(self, FILE_ERROR_NOMEM, "Rename file has been faield.", sse_false);
    } else if (err == SSE_E_NOENT) {
      TFILEDownloader_StoreResultCode(self, FILE_ERROR_NOENT, "Rename file has been faield.", sse_false);
    } else {
      TFILEDownloader_StoreResultCode(self, FILE_ERROR_RENAME, "Rename file has been faield.", sse_false);
    }
  }
  TFILEDownloader_DoPostAction(self);
  return;
}

/*
 * Do post-action
 */

static void
TFILEDownloader_DoPostAction(TFILEDownloader *self)
{
  sse_int err;
  MoatValue *postaction;
  sse_char *str;
  sse_uint len;
  sse_char *cmd;

  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);

  postaction = TFILEFilesysInfo_GetPostAction(self->fFilesysInfo);
  if (postaction == NULL) {
      LOG_DEBUG("No post-action. so download the file.");
      TFILEDownloader_CallOnCompleteCallback(self);
      return;
  }

  err = moat_value_get_string(postaction, &str, &len);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_value_get_string() has been failed with [%s].", sse_get_error_string(err));
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_CONF, "Invalid post-action script configuration.", sse_false);
    TFILEDownloader_CallOnCompleteCallback(self);
    return;
  }
  cmd = sse_strndup(str, len);
  ASSERT(cmd);

  LOG_INFO("Execute post-action=[%s].", cmd);
  self->fPostAction = SseUtilShellCommand_New();
  ASSERT(self->fPostAction);
  
  err = TSseUtilShellCommand_SetShellCommand(self->fPostAction, cmd);
  sse_free(cmd);
  if (err != SSE_E_OK) {
    LOG_ERROR("TSseUtilShellCommand_SetShellCommand() has been failed with [%s].", sse_get_error_string(err));
    TSseUtilShellCommand_Delete(self->fPostAction);
    self->fPostAction = NULL;
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_EXEC, "Executing post-action script has been failed.", sse_false);
    TFILEDownloader_CallOnCompleteCallback(self);
    return;
  }

  TSseUtilShellCommand_SetOnComplatedCallback(self->fPostAction,
                                              FILEDownloader_DoPostActionOnCompletedCallback,
                                              self);
  TSseUtilShellCommand_SetOnReadCallback(self->fPostAction,
                                         FILEDownloader_DoPostActionOnReadCallback,
                                         self);
  TSseUtilShellCommand_SetOnErrorCallback(self->fPostAction,
                                          FILEDownloader_DoPostActionOnErrorCallback,
                                          self);

  err = TSseUtilShellCommand_Execute(self->fPostAction);
  if (err != SSE_E_OK) {
    LOG_ERROR("TSseUtilShellCommand_Execute() has been failed with [%s].", sse_get_error_string(err));
    TSseUtilShellCommand_Delete(self->fPostAction);
    self->fPostAction = NULL;
    TFILEDownloader_StoreResultCode(self, FILE_ERROR_EXEC, "Executing post-action script has been failed.", sse_true);
    TFILEDownloader_CallOnCompleteCallback(self);
    return;
  }

  return;
}

static void
FILEDownloader_DoPostActionOnCompletedCallback(TSseUtilShellCommand* self,
                                               sse_pointer in_user_data,
                                               sse_int in_result)
{
  TFILEDownloader *downloader = (TFILEDownloader*)in_user_data;
  ASSERT(self);
  ASSERT(downloader);

  if (in_result != SSE_E_OK) {
    LOG_ERROR("Pre-action(%s) has been failed with [%s].", self->fShellCommand, sse_get_error_string(in_result));
    TFILEDownloader_StoreResultCode(downloader, FILE_ERROR_EXEC, "Executing post-action script has been failed.", sse_true);
    return;
  }

  LOG_INFO("Post-action(%s) has been completed successfully.", self->fShellCommand);
  TFILEDownloader_CallOnCompleteCallback(downloader);
}

static void
FILEDownloader_DoPostActionOnReadCallback(TSseUtilShellCommand* self,
                                          sse_pointer in_user_data)
{
  sse_int err;
  sse_char *buff;
  TFILEDownloader *downloader = (TFILEDownloader*)in_user_data;
  ASSERT(self);
  ASSERT(downloader);

  err = TSseUtilShellCommand_ReadLine(self, &buff, sse_true);
  if (err != SSE_E_OK) {
    LOG_ERROR("TSseUtilShellCommand_ReadLine() has been failed with [%s].", sse_get_error_string(err));
    TFILEDownloader_StoreResultCode(downloader, FILE_ERROR_EXEC, "Executing post-action script has been failed.", sse_true);
    return;
  }

  LOG_DEBUG("%s=[%s]", self->fShellCommand, buff);
  sse_free(buff);
  return;
}

static void
FILEDownloader_DoPostActionOnErrorCallback(TSseUtilShellCommand* self,
                                          sse_pointer in_user_data,
                                          sse_int in_error_code,
                                          const sse_char* in_message)
{
  TFILEDownloader *downloader = (TFILEDownloader*)in_user_data;
  ASSERT(self);
  ASSERT(downloader);

  LOG_ERROR("TSseUtilShellCommand_ReadLine() has been failed with [%s], message=[%s].", sse_get_error_string(in_error_code), in_message);
  TFILEDownloader_StoreResultCode(downloader, FILE_ERROR_EXEC, "Executing post-action script has been failed.", sse_false);
  TFILEDownloader_CallOnCompleteCallback(downloader);
  return;
}


/*
 * Constructor / Destructor
 */

TFILEDownloader*
FILEDownloader_New(const sse_char *in_uid,
                   const sse_char *in_key)
{
  TFILEDownloader *self;

  LOG_DEBUG("Enter: Uid=[%s], Key=[%s]", in_uid, in_key);

  self = sse_zeroalloc(sizeof(TFILEDownloader));
  ASSERT(self);

  if (in_uid) {
    self->fUid = sse_strdup(in_uid);
    LOG_DEBUG("uid=[%s]", in_uid);
  } else {
    self->fUid = NULL;
    LOG_DEBUG("uid=NULL.");
  }

  if (in_key) {
    self->fKey = sse_strdup(in_key);
    LOG_DEBUG("key=[%s]", in_key);
  } else {
    self->fKey = NULL;
    LOG_DEBUG("key=NULL");
  }

  self->fPreAction = NULL;
  self->fDownloader = moat_downloader_new();
  ASSERT(self->fDownloader);
  moat_downloader_set_callbacks(self->fDownloader,
                                FILEDownloader_OnDownloadCompletionCallback,
                                FILEDownlaoder_OnDownloadErrorCallback,
                                self);
  self->fPostAction = NULL;
  self->fUrl = NULL;
  self->fFilePath = NULL;
  self->fTmpFilePath = NULL;
  self->fFilesysInfo = NULL;
  self->fOnCompleteCallback = NULL;
  self->fOnCompleteCallbackUserData = NULL;
  self->fResultCode = NULL;

  LOG_DEBUG("Leave: self=[%p]", self);
  return self;
}

void
TFILEDownloader_Delete(TFILEDownloader *self)
{
  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);

  if (self->fUid)         sse_free(self->fUid);
  if (self->fKey)         sse_free(self->fKey);
  if (self->fDownloader)  moat_downloader_free(self->fDownloader);
  if (self->fUrl)         moat_value_free(self->fUrl);
  if (self->fFilePath)    moat_value_free(self->fFilePath);
  if (self->fTmpFilePath) moat_value_free(self->fTmpFilePath);
  if (self->fFilesysInfo) moat_value_free(self->fFilesysInfo);
  if (self->fResultCode)  moat_object_free(self->fResultCode);
  if (self->fPreAction)   TSseUtilShellCommand_Delete(self->fPreAction);
  if (self->fPostAction)  TSseUtilShellCommand_Delete(self->fPostAction);
  sse_free(self);
}

void
TFILEDownloader_SetOnCompleteCallback(TFILEDownloader *self,
                                      TFILEDownloader_OnDownloadCompleteCallback in_callback,
                                      sse_pointer in_user_data)
{
  ASSERT(self);
  self->fOnCompleteCallback = in_callback;
  self->fOnCompleteCallbackUserData = in_user_data;
}

void
TFILEDownloader_RemoveOnCompleteCallback(TFILEDownloader *self)
{
  ASSERT(self);
  self->fOnCompleteCallback = NULL;
  self->fOnCompleteCallbackUserData = NULL;
}

sse_int
TFILEDownloader_SetResourcePath(TFILEDownloader *self,
                                MoatValue *in_src_url,
                                MoatValue *in_dst_filepath,
                                TFILEFilesysInfoTbl *in_filesys_info_tbl)
{
  ASSERT(in_src_url);
  ASSERT(in_dst_filepath);

  self->fUrl = moat_value_clone(in_src_url);
  ASSERT(self->fUrl);
  self->fFilePath = moat_value_clone(in_dst_filepath);
  ASSERT(self->fFilePath);
  self->fFilesysInfo = moat_value_clone(TFILEFilesysInfoTbl_FindFilesysInfo(in_filesys_info_tbl, self->fFilePath));
  /* self->fFilesysInfo == NULL is acceptable. */

  return SSE_E_OK;
}

void
TFILEDownloader_DownloadFile(TFILEDownloader *self)
{
  ASSERT(self);
  TFILEDownloader_DoPreAction(self);
  return;
}

static void
TFILEDownloader_CallOnCompleteCallback(TFILEDownloader *self)
{
  MoatValue *err_code;
  MoatValue *err_msg;

  ASSERT(self);
  LOG_INFO("Download file has been completed successfuly.");
  if (self->fOnCompleteCallback) {
    if (self->fResultCode == NULL) {
      TFILEDownloader_StoreResultCode(self, FILE_ERROR_OK, "Download file has been complated successfuly.", sse_true);
    }
    err_code = moat_object_get_value(self->fResultCode, "err_code");
    err_msg  = moat_object_get_value(self->fResultCode, "err_msg");
    self->fOnCompleteCallback(self, err_code, err_msg, self->fUid, self->fKey, self->fOnCompleteCallbackUserData);
  }
}

static sse_int
TFILEDownloader_StoreResultCode(TFILEDownloader *self,
                                const sse_char *in_err_code,
                                const sse_char *in_err_msg,
                                sse_bool in_overwrite)
{
  sse_int err;

  ASSERT(self);
  if (self->fResultCode) {
    if (in_overwrite) {
      moat_object_free(self->fResultCode);
    } else {
      return SSE_E_OK;
    }
  }
  self->fResultCode = moat_object_new();
  ASSERT(self->fResultCode);
  err = moat_object_add_string_value(self->fResultCode, "err_code", (sse_char*)in_err_code, 0, sse_true, sse_false);
  if (err != SSE_E_OK) {
    moat_object_free(self->fResultCode);
    self->fResultCode = NULL;
    return err;
  }
  err = moat_object_add_string_value(self->fResultCode, "err_msg", (sse_char*)in_err_msg, 0, sse_true, sse_false);
  if (err != SSE_E_OK) {
    moat_object_free(self->fResultCode);
    self->fResultCode = NULL;
    return err;
  }
  return SSE_E_OK;
}
