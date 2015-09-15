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

#ifndef __FILE_FILESYS_INFO_H__
#define __FILE_FILESYS_INFO_H__

SSE_BEGIN_C_DECLS

struct TFILEFilesysInfoTbl_ {
  MoatObject *fObject;
};
typedef struct TFILEFilesysInfoTbl_ TFILEFilesysInfoTbl;

sse_int
TFILEFilesysInfoTbl_Initialize(TFILEFilesysInfoTbl *self);

void
TFILEFilesysInfoTbl_Finalize(TFILEFilesysInfoTbl *self);

sse_int
TFILEFilesysInfoTbl_LoadConfig(TFILEFilesysInfoTbl *self,
                               const sse_char *in_file_path);

MoatValue*
TFILEFilesysInfoTbl_FindFilesysInfo(TFILEFilesysInfoTbl *self,
                                    MoatValue *in_file_path);

typedef MoatValue TFILEFilesysInfo;

MoatValue*
TFILEFilesysInfo_GetType(TFILEFilesysInfo *self);

MoatValue*
TFILEFilesysInfo_GetPreAction(TFILEFilesysInfo *self);

MoatValue*
TFILEFilesysInfo_GetPostAction(TFILEFilesysInfo *self);

MoatValue*
TFILEFilesysInfo_GetTmpDir(TFILEFilesysInfo *self);

SSE_END_C_DECLS

#endif /*__FILE_FILESYS_INFO_H__*/
