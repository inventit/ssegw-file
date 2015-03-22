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
 * http://www.yourinventit.com/
 */

/*!
 * @file    mkpath.h
 * @brief   ディレクトリ作成ユーティリティ
 *
 *          ディレクトリを再帰的に作成する
 */

#ifndef FILEUTIL_MKPATH_H_
#define FILEUTIL_MKPATH_H_

typedef struct FileMkpath_ FileMkpath;

FileMkpath *mkpath_new(void);
sse_int mkpath_make(FileMkpath *in_mkpath, const sse_char *in_path, mode_t mode);
sse_int mkpath_is_lasterr_rofs(FileMkpath *in_mkpath, sse_bool *result);
void mkpath_fini(FileMkpath *in_mkpath);

#endif /* FILEUTIL_MKPATH_H_ */
