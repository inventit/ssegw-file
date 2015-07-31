{
  'variables': {
    'sseutils_root': './ssegw-utils-moatc',
  },
  'includes': [
    'common.gypi',
    'config.gypi',
    './ssegw-utils-moatc/sseutils.gypi',
  ],
  'targets': [
    # File deliver and fetch application
    {
      'target_name': '<(package_name)',
      'sources': [
        '<@(sseutils_src)',
        'src/file/file_downloader.c',
        'src/file/file_filesys_info.c',
        'src/file/file_content_info.c',
        'src/<(package_name).c',
       ],
      'product_prefix': '',
      'type': 'shared_library',
      'cflags': [ '-fPIC' ],
      'include_dirs' : [
        '<(sseutils_include)',
      ],
      'libraries': [
        '-lmoatapp',
      ],
      'dependencies': [
      ],
    },
  ],
}
