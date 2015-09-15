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

sse_int
TFILEFilesysInfoTbl_Initialize(TFILEFilesysInfoTbl *self)
{
  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);
  self->fObject = NULL;
  return SSE_E_OK;
}

void
TFILEFilesysInfoTbl_Finalize(TFILEFilesysInfoTbl *self)
{
  LOG_DEBUG("Enter: self=[%p]", self);
  ASSERT(self);
  if (self->fObject) moat_object_free(self->fObject);
  return;
}

sse_int
TFILEFilesysInfoTbl_LoadConfig(TFILEFilesysInfoTbl *self,
                               const sse_char *in_file_path)
{
  sse_int err;
  sse_char *err_msg;

  LOG_DEBUG("Enter: self=[%p], file_path=[%s]", self, in_file_path);
  ASSERT(self);
  ASSERT(in_file_path);

  err = moat_json_file_to_moat_object((sse_char*)in_file_path, &self->fObject, &err_msg);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_json_file_to_moat_object(path=[%s]) has been failed with [%s]. message=[%s]",
              in_file_path, sse_get_error_string(err), err_msg);
    sse_free(err_msg);
    self->fObject = NULL;
  }
  MOAT_OBJECT_DUMP_INFO(TAG, self->fObject);
  return SSE_E_OK;
}

MoatValue*
TFILEFilesysInfoTbl_FindFilesysInfo(TFILEFilesysInfoTbl *self,
                                    MoatValue *in_file_path)
{
  sse_int err;
  sse_char *file_path;
  sse_uint file_path_len;
  sse_char *path;
  sse_char *p;
  MoatValue *filesys_info;

  LOG_DEBUG("Enter: self=[%p]", self);
  MOAT_VALUE_DUMP_DEBUG(TAG, in_file_path);

  ASSERT(self);
  ASSERT(in_file_path);

  if (self->fObject == NULL) {
    LOG_WARN("No filesystem info is registered.");
    return NULL;
  }

  err = moat_value_get_string(in_file_path, &file_path, &file_path_len);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_value_get_string_value() has been failed with [%s].", sse_get_error_string(err));
    return NULL;
  }
  
  path = sse_strndup(file_path, file_path_len);
  ASSERT(path);

  do {
    filesys_info = moat_object_get_value(self->fObject, (path[0] == '\0') ? "/" : path);
    if (filesys_info) {
      LOG_DEBUG("Filesystem info which includes the target file has been found.");
      MOAT_VALUE_DUMP_DEBUG(TAG, filesys_info);
      sse_free(path);
      return filesys_info;
    }
    p = sse_strrchr(path, '/');
    if (p != NULL) {
      *p = '\0';
    }
  } while (p);
  sse_free(path);

  LOG_WARN("The file path does not be matched in any filesystem info.");
  return NULL;
}

MoatValue*
FILEFilesysInfo_GetValue(MoatValue *in_value,
                         const sse_char *in_key)
{
  MoatObject *object;
  MoatValue *value;
  sse_int err;

  LOG_DEBUG("Enter: in_value=[%p], in_key=[%s]", in_value, in_key);
  ASSERT(in_key);

  if (in_value == NULL) {
    LOG_WARN("No filesys info.");
    return NULL;
  }

  if (moat_value_get_type(in_value) != MOAT_VALUE_TYPE_OBJECT) {
    LOG_ERROR("MoatValue type of TFILEFilesysInfo must be MoatObject.");
    MOAT_VALUE_DUMP_ERROR(TAG, in_value);
    return NULL;
  }

  err = moat_value_get_object(in_value, &object);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_value_get_object() has been failed with [%s].", sse_get_error_string(err));
    MOAT_VALUE_DUMP_ERROR(TAG, in_value);
    return NULL;
  }

  value = moat_object_get_value(object, (sse_char*)in_key);
  if (value == NULL) {
    LOG_ERROR("key=[%s] was not found in the object.", in_key);
    return NULL;
  }

  if (moat_value_get_type(value) != MOAT_VALUE_TYPE_STRING) {
    if (moat_value_get_type(value) == MOAT_VALUE_TYPE_NULL) {
      return NULL;
    } else {
      LOG_ERROR("Enexpected value type = [%d].", moat_value_get_type(value));
      MOAT_VALUE_DUMP_ERROR(TAG, value);
      return NULL;
    }
  }

  return value;
}

MoatValue*
TFILEFilesysInfo_GetType(TFILEFilesysInfo *self)
{
  return FILEFilesysInfo_GetValue((MoatValue *)self, "type");
}

MoatValue*
TFILEFilesysInfo_GetPreAction(TFILEFilesysInfo *self)
{
  return FILEFilesysInfo_GetValue((MoatValue *)self, "preaction");
}

MoatValue*
TFILEFilesysInfo_GetPostAction(TFILEFilesysInfo *self)
{
  return FILEFilesysInfo_GetValue((MoatValue *)self, "postaction");
}

MoatValue*
TFILEFilesysInfo_GetTmpDir(TFILEFilesysInfo *self)
{
  return FILEFilesysInfo_GetValue((MoatValue *)self, "tmpdir");
}

