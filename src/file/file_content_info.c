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
#include <file/file.h>

#define TAG "File"
#define LOG_ERROR(format, ...) MOAT_LOG_ERROR(TAG, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  MOAT_LOG_WARN(TAG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  MOAT_LOG_INFO(TAG, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) MOAT_LOG_DEBUG(TAG, format, ##__VA_ARGS__)
#define LOG_TRACE(format, ...) MOAT_LOG_TRACE(TAG, format, ##__VA_ARGS__)
#include <stdlib.h>
#define ASSERT(cond) if(!(cond)) { LOG_ERROR("ASSERTION FAILED:" #cond); abort(); }

sse_int
TFILEContentInfo_Initialize(TFILEContentInfo *self,
                            Moat in_moat)
{
  sse_int err;
  sse_char *err_msg = NULL;
  sse_char *path = FILE_CONFIG_FILESYSTEM_PATH;

  LOG_DEBUG("Enter: self=[%p], moat=[%p]", self, in_moat);
  ASSERT(self);
  ASSERT(in_moat);

  self->fMoat = in_moat;
  self->fObject = NULL;
  err = moat_json_file_to_moat_object(path, &self->fFilesysInfo, &err_msg);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_json_file_to_moat_object(path=[%s]) has been failed with [%s:%s].", path, sse_get_error_string(err), err_msg);
    sse_free(err_msg);
    self->fFilesysInfo = NULL;
  }
  MOAT_OBJECT_DUMP_INFO(TAG, self->fFilesysInfo);

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
  if (self->fFilesysInfo) {
    moat_object_free(self->fFilesysInfo);
    self->fFilesysInfo = NULL;
  }
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
ContentInfo_download(Moat in_moat,
                     sse_char *in_uid,
                     sse_char *in_key,
                     MoatValue *in_data,
                     sse_pointer in_model_context)
{
  sse_int err;

  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]", in_moat, in_uid, in_key, in_data, in_model_context);
  ASSERT(in_moat);
  ASSERT(in_model_context);

  err = moat_start_async_command(in_moat, in_uid, in_key, in_data, FILEContent_DownloadFileAsync, in_model_context);
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
  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]", in_moat, in_uid, in_key, in_data, in_model_context);
  //TODO
  return SSE_E_OK;
}

sse_int
ContentInfo_upload(Moat in_moat,
                   sse_char *in_uid,
                   sse_char *in_key,
                   MoatValue *in_data,
                   sse_pointer in_model_context)
{
  sse_int err;

  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]", in_moat, in_uid, in_key, in_data, in_model_context);
  ASSERT(in_moat);
  ASSERT(in_model_context);

  err = moat_start_async_command(in_moat, in_uid, in_key, in_data, FILEContent_UploadFileAsync, in_model_context);
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
  LOG_DEBUG("Enter: moat=[%p], uid=[%s], key=[%s], data=[%p], context=[%p]", in_moat, in_uid, in_key, in_data, in_model_context);
  //TODO
  return SSE_E_OK;
}



