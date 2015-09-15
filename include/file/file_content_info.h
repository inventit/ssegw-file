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

#ifndef __FILE_CONTENT_INFO_H__
#define __FILE_CONTENT_INFO_H__

SSE_BEGIN_C_DECLS

struct TFILEContentInfo_ {
  Moat fMoat;
  MoatObject *fObject;
  TFILEFilesysInfoTbl fFilesysInfo;
};
typedef struct TFILEContentInfo_ TFILEContentInfo;

/**
 * @brief Constructor of TFILEContentInfo class
 *
 * Constructor of TFILEContentInfo class
 *
 * @param [in] self          Instance
 * @param [in] in_moat       Moat Instance
 *
 * @retval SSE_E_OK  Success
 * @retval others    Failuer
 */
sse_int
TFILEContentInfo_Initialize(TFILEContentInfo *self,
                            Moat in_moat);

/**
 * @brief Destructor of TFILEContentInfo class
 *
 * Destructor of TFILEContentInfo class
 *
 * @param [in] self          Instance
 *
 * @return None
 */
void
TFILEContentInfo_Finalize(TFILEContentInfo *self);


/**
 * @brief Callback function for model update
 *
 * Callback function for updating all feilds of value in the model object
 *
 * @param [in] self             Instance
 * @param [in] in_uid           UUID
 * @param [in] in_object        New model object
 * @param [in] in_model_context Context information associated with the model
 *
 * @retval SSE_E_OK  Success
 * @retval others    Failuer
 */
sse_int
FILEContentInfo_UpdateProc(Moat in_moat,
                           sse_char *in_uid,
                           MoatObject *in_object,
                           sse_pointer in_model_context);

/**
 * @brief Callback function for model fields update
 *
 * Callback function for updating suggested fields of values in the model object
 *
 * @param [in] self             Instance
 * @param [in] in_uid           UUID
 * @param [in] in_object        New model object
 * @param [in] in_model_context Context information associated with the model
 *
 * @retval SSE_E_OK  Success
 * @retval others    Failuer
 */
sse_int
FILEContentInfo_UpdateFieldsProc(Moat in_moat,
                                 sse_char *in_uid,
                                 MoatObject *in_object,
                                 sse_pointer in_model_context);

sse_int
TFILEContentInfo_GetDownloadFilePath(TFILEContentInfo *self,
                                     MoatValue **out_url,
                                     MoatValue **out_file_path);

sse_int
TFILEContentInfo_GetUploadFilePath(TFILEContentInfo *self,
                                   MoatValue **out_url,
                                   MoatValue **out_file_path);


/**
 * @brief Entry point of "download" command in "ContentInfo" model.
 *
 * Entry point of "download" command in "ContentInfo" model.
 *
 * @param [in] in_moat          Moat instance
 * @param [in] in_uid           UUID
 * @param [in] in_key           Continuation key for notifying asynchronous operation result
 * @param [in] in_data          Parameter value of the command
 * @param [in] in_model_context Context information associated with the model
 *
 * @retval SSE_E_INPROGRESS Success
 * @retval others           Failuer
 */
sse_int
ContentInfo_download(Moat in_moat,
                     sse_char *in_uid,
                     sse_char *in_key,
                     MoatValue *in_data,
                     sse_pointer in_model_context);

sse_int
FILEContent_DownloadFileAsync(Moat in_moat,
                              sse_char *in_uid,
                              sse_char *in_key,
                              MoatValue *in_data,
                              sse_pointer in_model_context);

/**
 * @brief Entry point of "upload" command in "ContentInfo" model.
 *
 * Entry point of "upload" command in "ContentInfo" model.
 *
 * @param [in] in_moat          Moat instance
 * @param [in] in_uid           UUID
 * @param [in] in_key           Continuation key for notifying asynchronous operation result
 * @param [in] in_data          Parameter value of the command
 * @param [in] in_model_context Context information associated with the model
 *
 * @retval SSE_E_INPROGRESS Success
 * @retval others           Failuer
 */
sse_int
ContentInfo_upload(Moat in_moat,
                   sse_char *in_uid,
                   sse_char *in_key,
                   MoatValue *in_data,
                   sse_pointer in_model_context);

sse_int
FILEContent_UploadFileAsync(Moat in_moat,
                            sse_char *in_uid,
                            sse_char *in_key,
                            MoatValue *in_data,
                            sse_pointer in_model_context);

SSE_END_C_DECLS

#endif /*__FILE_CONTENT_INFO_H__*/
