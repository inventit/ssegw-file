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

#ifndef __FILE_UPLOADER_H__
#define __FILE_UPLOADER_H__

SSE_BEGIN_C_DECLS

/**
 * @struct TFILEUploader_
 * @brief The uploader class in order to upload the file to the web storage.
 */  
struct TFILEUploader_ {
  sse_char *fUid;                          /** uid of upload command requeet in ContentInfo model */
  sse_char *fKey;                          /** key of upload command requeet in ContentInfo model */
  MoatValue *fUrl;                         /** Upload URL */
  MoatValue *fFilePath;                    /** Source file path */
  MoatUploader *fUploader;                 /** MOAT Uploader instance */
  void (*fOnCompleteCallback)(struct TFILEUploader_*, MoatValue*, MoatValue*, const sse_char*, const sse_char*, sse_pointer); /** Callback function */
  sse_pointer fOnCompleteCallbackUserData; /** User data passed with callback. */
  MoatObject *fResultCode;                 /** Result code and message. */
};
typedef struct TFILEUploader_ TFILEUploader;


/**
 * @brief Constructor of TFILEUploader class
 *
 * Constructor of TFILEUploader class
 *
 * @param [in] in_uid      uid of upload command requeet in ContentInfo model
 * @param [in] in_key      key of upload command requeet in ContentInfo model
 *
 * @retval SSE_E_OK  Success
 * @retval others    Failuer
 */
TFILEUploader*
FILEUploader_New(const sse_char *in_uid,
                 const sse_char *in_key);

/**
 * @brief Destructor of TFILEUploader clann
 *
 * Denstructor of TFILEUploader clann
 *
 * @param [in] self Instance
 *
 * @return none
 */
void
TFILEUploader_Delete(TFILEUploader *self);

/**
 * @brief Prototype of callback of upload completion.
 *
 * This function will be called when the uploading has been completed.
 *
 * @param [in] self Instance
 * @param [in] in_err_code error code
 * @param [in] in_err_msg  error message
 * @param [in] in_uid      uid of upload command requeet in ContentInfo model
 * @param [in] in_key      key of upload command requeet in ContentInfo model
 *
 * @return none
 */
typedef void (*TFILEUploader_OnUploadCompleteCallback)(TFILEUploader *self,
                                                       MoatValue *in_err_code,
                                                       MoatValue *in_err_msg,
                                                       const sse_char *in_uid,
                                                       const sse_char *in_key,
                                                       sse_pointer in_user_data);

/**
 * @brief Set a on-complete callback
 *
 * Set a callback function which will be called when uploading has been complated.
 *
 * @param [in] self          Instance
 * @param [in] in_callback   Callback function
 * @param [in] in_user_data  User data
 *
 * @return none
 */
void
TFILEUploader_SetOnCompleteCallback(TFILEUploader *self,
                                    TFILEUploader_OnUploadCompleteCallback in_callback,
                                    sse_pointer in_user_data);

/**
 * @brief Remove a on-complete callback
 *
 * Remove the callback function which will be called when uploading has been complated.
 *
 * @param [in] self        Instance
 *
 * @return none
 */
void
TFILEUploader_RemoveOnCompleteCallback(TFILEUploader *self);

/**
 * @brief Set a resource path
 *
 * Set a resource path, upload URL and source file path.
 *
 * @param [in] self                Instance
 * @param [in] in_src_filepath     Source file path
 * @param [in] in_dst_url          Upload URL
 *
 * @retval SSE_E_OK Success
 * @retval others   Failure
 */
sse_int
TFILEUploader_SetResourcePath(TFILEUploader *self,
                              MoatValue *in_src_filepath,
                              MoatValue *in_dst_url);

/**
 * @brief Upload the file
 *
 * Upload the file to the web storage
 *
 * @param [in] self Instance
 *
 * @return none
 */
void
TFILEUploader_UploadFile(TFILEUploader *self);


SSE_END_C_DECLS

#endif /*__FILE_UPLOADER_H__*/
