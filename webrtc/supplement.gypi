{
  'variables': {
    'variables': {
      'webrtc_root%': '<(webrtc_depth)/webrtc',
    },
    'webrtc_root%': '<(webrtc_root)',
    'build_with_chromium': 0,
    'conditions': [
      ['OS=="ios"', {
        # Default to using BoringSSL on iOS.
        'use_openssl%': 1,

        # Set target_subarch for if not already set. This is needed because the
        # Chromium iOS toolchain relies on target_subarch being set.
        'conditions': [
          ['target_arch=="arm" or target_arch=="ia32"', {
            'target_subarch%': 'arm32',
          }],
          ['target_arch=="arm64" or target_arch=="x64"', {
            'target_subarch%': 'arm64',
          }],
        ],
      }],
    ],
  },
  'target_defaults': {
    'target_conditions': [
      ['_target_name=="sanitizer_options"', {
        'conditions': [
          ['lsan==1', {
            # Replace Chromium's LSan suppressions with our own for WebRTC.
            'sources/': [
              ['exclude', 'lsan_suppressions.cc'],
            ],
            'sources': [
              '<(webrtc_root)/build/sanitizers/lsan_suppressions_webrtc.cc',
            ],
          }],
          ['tsan==1', {
            # Replace Chromium's TSan v2 suppressions with our own for WebRTC.
            'sources/': [
              ['exclude', 'tsan_suppressions.cc'],
            ],
            'sources': [
              '<(webrtc_root)/build/sanitizers/tsan_suppressions_webrtc.cc',
            ],
          }],
        ],
      }],
    ],
  },
}
