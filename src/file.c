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

/*!
 * @filedevinfo.c
 * @brief MOAT application in order to deliver and fetch a file
 *
 * MOAT application in order to deliver and fetch a file.
 */

#include <stdlib.h> // EXIT_SUCCESS
#include <servicesync/moat.h>
//#include <sseutils.h>
#include <file/file.h>

#define TAG "File"
#define LOG_ERROR(format, ...) MOAT_LOG_ERROR(TAG, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  MOAT_LOG_WARN(TAG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  MOAT_LOG_INFO(TAG, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) MOAT_LOG_DEBUG(TAG, format, ##__VA_ARGS__)
#define LOG_TRACE(format, ...) MOAT_LOG_TRACE(TAG, format, ##__VA_ARGS__)
#include <stdlib.h>
#define ASSERT(cond) if(!(cond)) { LOG_ERROR("ASSERTION FAILED:" #cond); abort(); }

  
/**
 * @breaf   Main routine
 *
 * Main routine of MOAT application
 *
 * @param [in] argc Number of arguments
 * @param [in] argv Arguments
 *
 * @retval EXIT_SUCCESS Success
 * @retval EXIT_FAILURE Failure
 */
sse_int
moat_app_main(sse_int argc, sse_char *argv[])
{
  sse_int err;
  Moat moat;
  ModelMapper mapper;
  TFILEContentInfo context;

  LOG_DEBUG("File application has been started.");
  err = moat_init(argv[0], &moat);
  if (err != SSE_E_OK) {
    LOG_ERROR("moat_init() has been failed with [%d]", sse_get_error_string(err));
    goto err_exit;
  }

  err = TFILEContentInfo_Initialize(&context, &moat);
  if (err != SSE_E_OK) {
    LOG_ERROR("TFILEContentInfo_Initialize() has been failed with [%s].", sse_get_error_string(err));
    goto err_exit;
  }

  /* Register the model */
  mapper.AddProc = NULL;
  mapper.RemoveProc = NULL;
  mapper.UpdateProc = FILEContentInfo_UpdateProc;
  mapper.UpdateFieldsProc = FILEContentInfo_UpdateFieldsProc;
  mapper.FindAllUidsProc = NULL;
  mapper.FindByUidProc = NULL;
  mapper.CountProc = NULL;
  err = moat_register_model(moat, FILE_MODELNAME_CONTENTINFO, &mapper, &context);
  if (err != SSE_E_OK) {
    LOG_ERROR("failed to register model.");
    goto err_exit;
  }


  /* main loop */
  moat_run(moat);

  /* Teardown */
  moat_unregister_model(moat, FILE_MODELNAME_CONTENTINFO);
  TFILEContentInfo_Finalize(&context);
  moat_destroy(moat);

  LOG_INFO("Teardown with SUCCESS");
  return EXIT_SUCCESS;

 err_exit:
  moat_unregister_model(moat, FILE_MODELNAME_CONTENTINFO);
  TFILEContentInfo_Finalize(&context);
  moat_destroy(moat);
  LOG_ERROR("Teardown with FAILURE");
  return EXIT_FAILURE;
}
