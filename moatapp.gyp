{ 'includes': [
    'common.gypi',
    'config.gypi',
  ],
  'targets': [
    # File deliver and fetch application
    {
      'target_name': '<(package_name)',
      'sources': [
        'src/file/file_content_info.c',
        'src/<(package_name).c',
       ],
      'product_prefix': '',
      'type': 'shared_library',
      'cflags': [ '-fPIC' ],
      'include_dirs' : [
      ],
      'libraries': [
        '-lmoatapp',
      ],
      'dependencies': [
      ],
    },
  ],
}
