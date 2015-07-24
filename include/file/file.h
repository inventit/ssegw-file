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

#ifndef __FILE_H__
#define __FILE_H__

SSE_BEGIN_C_DECLS

#define FILE_CONFIG_FILESYSTEM_PATH "filesystem.conf"

#define FILE_MODELNAME_CONTENTINFO "ContentInfo"

#define FILE_FILESYS_TYPE_RAMDISK "ramdisk"
#define FILE_FILESYS_TYPE_NVRAM   "nvram"
#define FILE_FILESYS_TYPE_RO      "ro"
#define FILE_FILESYS_TYPE_RW      "rw"

#define FILE_ERROR_INVAL   "Error.File.InvalidArgument"
#define FILE_ERROR_NOMEM   "Error.File.OutOfMemory"
#define FILE_ERROR_ACCESS  "Error.File.PermissionDenied"
#define FILE_ERROR_NOENT   "Error.File.NoSuchFileOrDirectory"
//TODO
#define FILE_ERROR_RUMTIME "Error.File.Runtime"

#include <file/file_content_info.h>

SSE_END_C_DECLS

#endif /*__FILE_H__*/
