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

static void FILEUploader_OnUploadCompletionCallback(MoatUploader *in_dl, sse_bool in_canceled, sse_pointer in_user_data);
static void FILEUplaoder_OnUploadErrorCallback(MoatUploader *in_dl, sse_int in_err_code, sse_pointer in_user_data);
static void TFILEUploader_CallOnCompleteCallback(TFILEUploader *self);
static sse_int TFILEUploader_StoreResultCode(TFILEUploader *self, const sse_char *in_err_code, const sse_char *in_err_msg, sse_bool in_overwrite);


static void
FILEUploader_OnUploadCompletionCallback(MoatUploader *in_dl,
                                        sse_bool in_canceled,
                                        sse_pointer in_user_data)
{
  TFILEUploader *uploader;

  uploader = (TFILEUploader *)in_user_data;
  ASSERT(uploader);

  if (in_canceled) {
    LOG_INFO("Upload has been canceled.");
    TFILEUploader_StoreResultCode(uploader, FILE_ERROR_UPLOAD, "File upload has been canceled.", sse_false);
  }
  TFILEUploader_CallOnCompleteCallback(uploader);

  return;
}

static void
FILEUplaoder_OnUploadErrorCallback(MoatUploader *in_dl,
                                   sse_int in_err_code,
                                   sse_pointer in_user_data)
{
  TFILEUploader *uploader;

  uploader = (TFILEUploader *)in_user_data;
  ASSERT(uploader);

  LOG_ERROR("Upload has been failed with [%d].", in_err_code);
  MOAT_VALUE_DUMP_ERROR(TAG, uploader->fUrl);
  MOAT_VALUE_DUMP_ERROR(TAG, uploader->fFilePath);

  TFILEUploader_StoreResultCode(uploader, FILE_ERROR_UPLOAD, "File upload failure.", sse_false);
  TFILEUploader_CallOnCompleteCallback(uploader);
  return;
}


/*
 * Constructor / Destructor
 */

TFILEUploader*
FILEUploader_New(const sse_char *in_uid,
                 const sse_char *in_key)
{
  TFILEUploader *self;

  LOG_DEBUG("Enter: Uid=[%s], Key=[%s]", in_uid, in_key);

  self = sse_zeroalloc(sizeof(TFILEUploader));
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

  self->fUploader = moat_uploader_new();
  ASSERT(self->fUploader);
  moat_uploader_set_callbacks(self->fUploader,
                              FILEUploader_OnUploadCompletionCallback,
                              FILEUplaoder_OnUploadErrorCallback,
                              self);
  self->fUrl = NULL;
  self->fFilePath = NULL;
  self->fOnCompleteCallback = NULL;
  self->fOnCompleteCallbackUserData = NULL;
  self->fResultCode = NULL;

  LOG_DEBUG("Leave: self=[%p]", self);
  return self;
}

void
TFILEUploader_Delete(TFILEUploader *self)
{
  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);

  if (self->fUid)         sse_free(self->fUid);
  if (self->fKey)         sse_free(self->fKey);
  if (self->fUploader)    moat_uploader_free(self->fUploader);
  if (self->fUrl)         moat_value_free(self->fUrl);
  if (self->fFilePath)    moat_value_free(self->fFilePath);
  if (self->fResultCode)  moat_object_free(self->fResultCode);
  sse_free(self);
}

void
TFILEUploader_SetOnCompleteCallback(TFILEUploader *self,
                                    TFILEUploader_OnUploadCompleteCallback in_callback,
                                    sse_pointer in_user_data)
{
  ASSERT(self);
  self->fOnCompleteCallback = in_callback;
  self->fOnCompleteCallbackUserData = in_user_data;
}

void
TFILEUploader_RemoveOnCompleteCallback(TFILEUploader *self)
{
  ASSERT(self);
  self->fOnCompleteCallback = NULL;
  self->fOnCompleteCallbackUserData = NULL;
}

sse_int
TFILEUploader_SetResourcePath(TFILEUploader *self,
                              MoatValue *in_src_filepath,
                              MoatValue *in_dst_url)
{
  ASSERT(in_src_filepath);
  ASSERT(in_dst_url);

  self->fFilePath = moat_value_clone(in_src_filepath);
  ASSERT(self->fFilePath);
  self->fUrl = moat_value_clone(in_dst_url);
  ASSERT(self->fUrl);

  return SSE_E_OK;
}

void
TFILEUploader_UploadFile(TFILEUploader *self)
{
  sse_int err;
  sse_char *dst_url;
  sse_uint dst_url_len;
  sse_char *src_file;
  sse_uint src_file_len;
  sse_char *src_file_path;

  ASSERT(self);

  LOG_INFO("Upload the file.");
  MOAT_VALUE_DUMP_INFO(TAG, self->fFilePath);
  MOAT_VALUE_DUMP_INFO(TAG, self->fUrl);

  err = moat_value_get_string(self->fUrl, &dst_url, &dst_url_len);
  ASSERT(err == SSE_E_OK);
  err = moat_value_get_string(self->fFilePath, &src_file, &src_file_len);
  ASSERT(err == SSE_E_OK);
  src_file_path = sse_strndup(src_file, src_file_len);
  ASSERT(src_file_path);

  err = moat_uploader_upload(self->fUploader, sse_false, /* Use PUT */
                             dst_url, dst_url_len, src_file_path);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_uploader_upload() has been failed with [%s].", sse_get_error_string(err));
    TFILEUploader_StoreResultCode(self, FILE_ERROR_UPLOAD, "File upload failure.", sse_false);
    TFILEUploader_CallOnCompleteCallback(self);
  }
  sse_free(src_file_path);

  return;
}

static void
TFILEUploader_CallOnCompleteCallback(TFILEUploader *self)
{
  MoatValue *err_code;
  MoatValue *err_msg;

  ASSERT(self);
  if (self->fOnCompleteCallback) {
    if (self->fResultCode == NULL) {
      LOG_INFO("Uploading file has been completed successfuly.");
      TFILEUploader_StoreResultCode(self, FILE_ERROR_OK, "Uploading file has been complated successfuly.", sse_true);
    } else {
      LOG_ERROR("Uploading file has been failed.");
      MOAT_OBJECT_DUMP_ERROR(TAG, self->fResultCode);
    }
    err_code = moat_object_get_value(self->fResultCode, "err_code");
    err_msg  = moat_object_get_value(self->fResultCode, "err_msg");
    self->fOnCompleteCallback(self, err_code, err_msg, self->fUid, self->fKey, self->fOnCompleteCallbackUserData);
  }
}

static sse_int
TFILEUploader_StoreResultCode(TFILEUploader *self,
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
