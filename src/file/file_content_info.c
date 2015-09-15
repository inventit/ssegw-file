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

static void
FILEContentInfo_OnCompleteCallback(MoatValue *in_err_code,
                                   MoatValue *in_err_msg,
                                   const sse_char *in_uid,
                                   const sse_char *in_key,
                                   const sse_char *operation,
                                   sse_pointer in_user_data)
{
  TFILEContentInfo *self = (TFILEContentInfo*)in_user_data;
  sse_char *job_service_id = NULL;
  MoatObject *collection = NULL;
  sse_int err;
  sse_char *str;
  sse_uint len;
  sse_bool success;
  sse_int request_id;

  ASSERT(self);
  ASSERT(in_err_code);
  ASSERT(in_err_msg);

  job_service_id = moat_create_notification_id_with_moat(self->fMoat, (sse_char*)operation, "1.0.0");
  ASSERT(job_service_id);
  LOG_DEBUG("URI=[%s]", job_service_id);

  collection = moat_object_new();
  ASSERT(collection);

  err = moat_value_get_string(in_err_code, &str, &len);
  ASSERT(err == SSE_E_OK);
  if ((sse_strlen(FILE_ERROR_OK) == len) && (sse_strncmp(FILE_ERROR_OK, str, len) == 0)) {
    success = sse_true;
  } else {
    success = sse_false;
  }

  err = moat_object_add_boolean_value(collection, "success", success, sse_true);
  ASSERT(err == SSE_E_OK);
  err = moat_object_add_value(collection, "message", in_err_msg, sse_true, sse_true);
  ASSERT(err == SSE_E_OK);
  err = moat_object_add_value(collection, "code", in_err_code, sse_true, sse_true);
  ASSERT(err == SSE_E_OK);
  if (in_uid) {
    err = moat_object_add_string_value(collection, "uid", (sse_char*)in_uid, 0, sse_true, sse_true);
    ASSERT(err == SSE_E_OK);
  }

  /* Send a notification. */
  request_id = moat_send_notification(self->fMoat,
                                      job_service_id,
                                      (sse_char*)in_key,
                                      FILE_MODELNAME_FILERESULT,
                                      collection,
                                      NULL, //FIXME
                                      NULL); //FIXME
  if (request_id < 0) {
    LOG_ERROR("moat_send_notification() ... failed with [%s].", request_id);
  }
  LOG_INFO("moat_send_notification(job_service_id=[%s], key=[%s]) ... in progress.", job_service_id, in_key);
  MOAT_OBJECT_DUMP_INFO(TAG, collection);

  moat_object_free(collection);
  sse_free(job_service_id);

  return;
}

static void
FILEContentInfo_OnDownloadCompleteCallback(TFILEDownloader *downloader,
                                           MoatValue *in_err_code,
                                           MoatValue *in_err_msg,
                                           const sse_char *in_uid,
                                           const sse_char *in_key,
                                           sse_pointer in_user_data)
{
  ASSERT(downloader);
  FILEContentInfo_OnCompleteCallback(in_err_code, in_err_msg, in_uid, in_key, FILE_OPERATION_DELIVER_RESULT, in_user_data);
  TFILEDownloader_Delete(downloader);
}

static void
FILEContentInfo_OnUploadCompleteCallback(TFILEUploader *uploader,
                                         MoatValue *in_err_code,
                                         MoatValue *in_err_msg,
                                         const sse_char *in_uid,
                                         const sse_char *in_key,
                                         sse_pointer in_user_data)
{
  ASSERT(uploader);
  FILEContentInfo_OnCompleteCallback(in_err_code, in_err_msg, in_uid, in_key, FILE_OPERATION_FETCH_RESULT, in_user_data);
  TFILEUploader_Delete(uploader);
}

sse_int
TFILEContentInfo_Initialize(TFILEContentInfo *self,
                            Moat in_moat)
{
  sse_int err;
  const sse_char *path = FILE_CONFIG_FILESYSTEM_PATH;

  LOG_DEBUG("Enter: self=[%p], moat=[%p]", self, in_moat);
  ASSERT(self);
  ASSERT(in_moat);

  self->fMoat = in_moat;
  self->fObject = NULL;
  err = TFILEFilesysInfoTbl_Initialize(&self->fFilesysInfo);
  if (err != SSE_E_OK) {
    LOG_ERROR("TFILEFilesysInfoTbl_Initialize() has been failed with [%s].", sse_get_error_string(err));
    return err;
  }

  err = TFILEFilesysInfoTbl_LoadConfig(&self->fFilesysInfo, path);
  if (err != SSE_E_OK) {
    LOG_WARN("TFILEFilesysInfoTbl_LoadConfig() has been failed with [%s].", sse_get_error_string(err));
  }
  return SSE_E_OK;
}

void
TFILEContentInfo_Finalize(TFILEContentInfo *self)
{
  LOG_DEBUG("Enter: self=[%p]", self);

  ASSERT(self);

  if (self->fObject) {
    moat_object_free(self->fObject);
    self->fObject = NULL;
  }
  TFILEFilesysInfoTbl_Finalize(&self->fFilesysInfo);
  return;
}

sse_int
FILEContentInfo_UpdateProc(Moat in_moat,
                           sse_char *in_uid,
                           MoatObject *in_object,
                           sse_pointer in_model_context)
{
  TFILEContentInfo *self = (TFILEContentInfo *)in_model_context;

  LOG_DEBUG("Enter: moat=[%p], uid=[%s], object=[%p], context=[%p]", in_moat, in_uid, in_object, in_model_context);
  ASSERT(in_object);
  ASSERT(in_model_context);

  if (self->fObject) {
    LOG_INFO("Release the existing model object.");
    MOAT_OBJECT_DUMP_DEBUG(TAG, self->fObject);
    moat_object_free(self->fObject);
    self->fObject = NULL;
  }
  LOG_INFO("Update the model object.");
  self->fObject = moat_object_clone(in_object);
  ASSERT(self->fObject);
  MOAT_OBJECT_DUMP_INFO(TAG, self->fObject);

  return SSE_E_OK;
}

sse_int
FILEContentInfo_UpdateFieldsProc(Moat in_moat,
                                 sse_char *in_uid,
                                 MoatObject *in_object,
                                 sse_pointer in_model_context)
{
  TFILEContentInfo *self = (TFILEContentInfo *)in_model_context;
  MoatObjectIterator *it;
  sse_char *key;
  MoatValue *value;
  sse_int err;

  LOG_DEBUG("Enter: moat=[%p], uid=[%s], object=[%p], context=[%p]", in_moat, in_uid, in_object, in_model_context);
  ASSERT(in_object);
  ASSERT(in_model_context);

  if (self->fObject == NULL) {
    LOG_INFO("Update the model object.");
    self->fObject = moat_object_clone(in_object);
    ASSERT(self->fObject);
    LOG_INFO("All fields of ContentInfo object have been update.");
    MOAT_OBJECT_DUMP_INFO(TAG, self->fObject);
    return SSE_E_OK;
  }

  it = moat_object_create_iterator(in_object);
  while (moat_object_iterator_has_next(it)) {
    key = moat_object_iterator_get_next_key(it);
    ASSERT(key);
    value = moat_object_get_value(in_object, key);
    ASSERT(value);
    LOG_DEBUG("Key=[%s] has been updated.", key);
    MOAT_VALUE_DUMP_DEBUG(TAG, value);
    err = moat_object_add_value(self->fObject, key, value, sse_true, sse_true);
    if (err != SSE_E_OK) {
      LOG_ERROR("moat_object_add_value() has been failed with [%s].", sse_get_error_string(err));
      moat_object_iterator_free(it);
      return err;
    }
  }
  moat_object_iterator_free(it);

  LOG_INFO("Some fields of ContentInfo object have been update.");
  MOAT_OBJECT_DUMP_INFO(TAG, self->fObject);
  return SSE_E_OK;
}

sse_int
TFILEContentInfo_GetDownloadFilePath(TFILEContentInfo *self,
                                     MoatValue **out_url,
                                     MoatValue **out_file_path)
{
  MoatValue *url;
  MoatValue *path;

  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);
  ASSERT(out_url);
  ASSERT(out_file_path);

  if (self->fObject == NULL) {
    LOG_ERROR("self->fObject=[%p]", self->fObject);
    return SSE_E_INVAL;
  }

  url  = moat_object_get_value(self->fObject, "deliveryUrl");
  if (url == NULL) {
    LOG_ERROR("No URL information.");
    MOAT_OBJECT_DUMP_ERROR(TAG, self->fObject);
    return SSE_E_GENERIC;
  }
  path = moat_object_get_value(self->fObject, "destinationPath");
  if (path == NULL) {
    LOG_ERROR("No destination file path information.");
    MOAT_OBJECT_DUMP_ERROR(TAG, self->fObject);
    return SSE_E_GENERIC;
  }

  *out_url = moat_value_clone(url);
  ASSERT(*out_url);
  *out_file_path = moat_value_clone(path);
  ASSERT(*out_file_path);
  return SSE_E_OK;
}

sse_int
TFILEContentInfo_GetUploadUrl(TFILEContentInfo *self,
                              MoatValue **out_file_path,
                              MoatValue **out_url)
{
  MoatValue *url;
  MoatValue *path;

  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);
  ASSERT(out_url);
  ASSERT(out_file_path);

  if (self->fObject == NULL) {
    LOG_ERROR("self->fObject=[%p]", self->fObject);
    return SSE_E_INVAL;
  }

  path = moat_object_get_value(self->fObject, "sourcePath");
  if (path == NULL) {
    LOG_ERROR("No source file path information.");
    MOAT_OBJECT_DUMP_ERROR(TAG, self->fObject);
    return SSE_E_GENERIC;
  }
  url  = moat_object_get_value(self->fObject, "uploadUrl");
  if (url == NULL) {
    LOG_ERROR("No URL information.");
    MOAT_OBJECT_DUMP_ERROR(TAG, self->fObject);
    return SSE_E_GENERIC;
  }

  *out_url = moat_value_clone(url);
  ASSERT(*out_url);
  *out_file_path = moat_value_clone(path);
  ASSERT(*out_file_path);
  return SSE_E_OK;
}

sse_int
ContentInfo_download(Moat in_moat,
                     sse_char *in_uid,
                     sse_char *in_key,
                     MoatValue *in_data,
                     sse_pointer in_model_context)
{
  sse_int err;
  TFILEDownloader *downloader;
  MoatValue *url;
  MoatValue *file_path;
  TFILEContentInfo *self = (TFILEContentInfo*)in_model_context;

  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]", in_moat, in_uid, in_key, in_data, in_model_context);
  ASSERT(in_moat);
  ASSERT(in_model_context);


  downloader = FILEDownloader_New(in_uid, in_key);
  ASSERT(downloader);
  TFILEDownloader_SetOnCompleteCallback(downloader, FILEContentInfo_OnDownloadCompleteCallback, self);

  /* Get the source URL and distination local file path. */
  err = TFILEContentInfo_GetDownloadFilePath(self, &url, &file_path);
  if (err != SSE_E_OK) {
    LOG_ERROR("TFILEContentInfo_GetDownloadFilePath() has been failed with [%s].", sse_get_error_string(err));
    return err;
  }
  LOG_DEBUG("Source URL=...");
  MOAT_VALUE_DUMP_DEBUG(TAG, url);
  LOG_DEBUG("Destination file path=...");
  MOAT_VALUE_DUMP_DEBUG(TAG, file_path);

  err = TFILEDownloader_SetResourcePath(downloader, url, file_path, &self->fFilesysInfo);
  moat_value_free(url);
  moat_value_free(file_path);
  if (err != SSE_E_OK) {
    LOG_ERROR("TFILEDownloader_SetResourcePath() has been failed with [%s].", sse_get_error_string(err));
    return err;
  }

  err = moat_start_async_command(in_moat, in_uid, in_key, in_data, FILEContent_DownloadFileAsync, downloader);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_start_async_command() ... failed with [%s].", sse_get_error_string(err));
    return err;
  }
  return SSE_E_INPROGRESS;
}


sse_int
FILEContent_DownloadFileAsync(Moat in_moat,
                              sse_char *in_uid,
                              sse_char *in_key,
                              MoatValue *in_data,
                              sse_pointer in_model_context)
{
  TFILEDownloader *downloader = (TFILEDownloader *)in_model_context;

  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]",
            in_moat, in_uid, in_key, in_data, in_model_context);
  ASSERT(in_moat);
  ASSERT(in_model_context);

  TFILEDownloader_DownloadFile(downloader);

  return SSE_E_INPROGRESS;
}

sse_int
ContentInfo_upload(Moat in_moat,
                   sse_char *in_uid,
                   sse_char *in_key,
                   MoatValue *in_data,
                   sse_pointer in_model_context)
{
  sse_int err;
  TFILEUploader *uploader;
  MoatValue *src_file_path;
  MoatValue *dst_url;
  TFILEContentInfo *self = (TFILEContentInfo*)in_model_context;

  ASSERT(in_moat);
  ASSERT(in_model_context);
  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]", in_moat, in_uid, in_key, in_data, in_model_context);

  uploader = FILEUploader_New(in_uid, in_key);
  ASSERT(uploader);
  TFILEUploader_SetOnCompleteCallback(uploader, FILEContentInfo_OnUploadCompleteCallback, self);

  /* Get the source file path and distination URL. */
  err = TFILEContentInfo_GetUploadUrl(self, &src_file_path, &dst_url);
  if (err != SSE_E_OK) {
    LOG_ERROR("TFILEContentInfo_GetUploadUrl() has been failed with [%s].", sse_get_error_string(err));
    return err;
  }
  LOG_DEBUG("Source file path=...");
  MOAT_VALUE_DUMP_DEBUG(TAG, src_file_path);
  LOG_DEBUG("Destination URL=...");
  MOAT_VALUE_DUMP_DEBUG(TAG, dst_url);

  err = TFILEUploader_SetResourcePath(uploader, src_file_path, dst_url);
  moat_value_free(src_file_path);
  moat_value_free(dst_url);
  if (err != SSE_E_OK) {
    LOG_ERROR("TFILEUploader_SetResourcePath() has been failed with [%s].", sse_get_error_string(err));
    return err;
  }

  err = moat_start_async_command(in_moat, in_uid, in_key, in_data, FILEContent_UploadFileAsync, uploader);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_start_async_command() ... failed with [%s].", sse_get_error_string(err));
    return err;
  }
  return SSE_E_INPROGRESS;
}

sse_int
FILEContent_UploadFileAsync(Moat in_moat,
                            sse_char *in_uid,
                            sse_char *in_key,
                            MoatValue *in_data,
                            sse_pointer in_model_context)
{
  TFILEUploader *uploader = (TFILEUploader *)in_model_context;

  ASSERT(in_moat);
  ASSERT(in_model_context);
  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]", in_moat, in_uid, in_key, in_data, in_model_context);

  TFILEUploader_UploadFile(uploader);

  return SSE_E_OK;
}



