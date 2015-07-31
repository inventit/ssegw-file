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

#ifndef __FILE_DOWNLOADER_H__
#define __FILE_DOWNLOADER_H__

SSE_BEGIN_C_DECLS

/**
 * @struct TFILEDownloader_
 * @brief The downloader class in order to download the file from thw web storage.
 */  
struct TFILEDownloader_ {
  sse_char *fUid;                          /** uid of download command requeet in ContentInfo model */
  sse_char *fKey;                          /** key of download command requeet in ContentInfo model */
  MoatValue *fFilesysInfo;                 /** Filesystem info which the file will be saved to. */
  MoatValue *fUrl;                         /** Source URL */
  MoatValue *fFilePath;                    /** Destination file path */
  MoatValue *fTmpFilePath;                 /** Temporary file path */
  TSseUtilShellCommand *fPreAction;        /** Shell command instance to execute the pre-action script. */
  MoatDownloader *fDownloader;             /** MOAT Downloader instance */
  TSseUtilShellCommand *fPostAction;       /** Shell command instance to execute the post-action script. */
  void (*fOnCompleteCallback)(struct TFILEDownloader_*, MoatValue*, MoatValue*, const sse_char*, const sse_char*, sse_pointer); /** Callback function */
  sse_pointer fOnCompleteCallbackUserData; /** User data passed with callback. */
  MoatObject *fResultCode;                 /** Result code and message. */
};
typedef struct TFILEDownloader_ TFILEDownloader;


/**
 * @brief Constructor of TFILEDownloader class
 *
 * Constructor of TFILEDownloader class
 *
 * @param [in] in_uid      uid of download command requeet in ContentInfo model
 * @param [in] in_key      key of download command requeet in ContentInfo model
 *
 * @retval SSE_E_OK  Success
 * @retval others    Failuer
 */
TFILEDownloader*
FILEDownloader_New(const sse_char *in_uid,
                   const sse_char *in_key);

/**
 * @brief Destructor of TFILEDownloader clann
 *
 * Denstructor of TFILEDownloader clann
 *
 * @param [in] self Instance
 *
 * @return none
 */
void
TFILEDownloader_Delete(TFILEDownloader *self);

/**
 * @brief Prototype of callback of download completion.
 *
 * This function will be called when the downloading has been completed.
 *
 * @param [in] self Instance
 * @param [in] in_err_code error code
 * @param [in] in_err_msg  error message
 * @param [in] in_uid      uid of download command requeet in ContentInfo model
 * @param [in] in_key      key of download command requeet in ContentInfo model
 *
 * @return none
 */
typedef void (*TFILEDownloader_OnDownloadCompleteCallback)(TFILEDownloader *self,
                                                           MoatValue *in_err_code,
                                                           MoatValue *in_err_msg,
                                                           const sse_char *in_uid,
                                                           const sse_char *in_key,
                                                           sse_pointer in_user_data);

/**
 * @brief Set a on-complete callback
 *
 * Set a callback function which will be called when downloading has been complated.
 *
 * @param [in] self          Instance
 * @param [in] in_callback   Callback function
 * @param [in] in_user_data  User data
 *
 * @return none
 */
void
TFILEDownloader_SetOnCompleteCallback(TFILEDownloader *self,
                                      TFILEDownloader_OnDownloadCompleteCallback in_callback,
                                      sse_pointer in_user_data);

/**
 * @brief Remove a on-complete callback
 *
 * Remove the callback function which will be called when downloading has been complated.
 *
 * @param [in] self        Instance
 *
 * @return none
 */
void
TFILEDownloader_RemoveOnCompleteCallback(TFILEDownloader *self);

/**
 * @brief Set a resource path
 *
 * Set a resource path, source URL and destination file path.
 *
 * @param [in] self                Instance
 * @param [in] in_src_url          Source URL
 * @param [in] in_dst_filepath     Destination file path
 * @param [in] in_filesys_info_tbl Table of the filesystem info
 *
 * @retval SSE_E_OK Success
 * @retval others   Failure
 */
sse_int
TFILEDownloader_SetResourcePath(TFILEDownloader *self,
                                MoatValue *in_src_url,
                                MoatValue *in_dst_filepath,
                                TFILEFilesysInfoTbl * in_filesys_info_tbl);

/**
 * @brief Download the file
 *
 * Download the file from web storage
 *
 * @param [in] self Instance
 *
 * @return none
 */
void
TFILEDownloader_DownloadFile(TFILEDownloader *self);


SSE_END_C_DECLS

#endif /*__FILE_DOWNLOADER_H__*/
