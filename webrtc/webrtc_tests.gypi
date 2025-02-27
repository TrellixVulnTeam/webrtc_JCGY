# Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
{
  'targets': [
    {
      'target_name': 'rtc_unittests',
      'type': '<(gtest_target_type)',
      'dependencies': [
        'base/base.gyp:rtc_base',
        'base/base_tests.gyp:rtc_base_tests_utils',
        'base/base_tests.gyp:rtc_base_tests',
        'libjingle/xmllite/xmllite.gyp:rtc_xmllite',
        'libjingle/xmpp/xmpp.gyp:rtc_xmpp',
        'p2p/p2p.gyp:rtc_p2p',
        'p2p/p2p.gyp:libstunprober',
        'rtc_p2p_unittest',
        'rtc_sound_tests',
        'rtc_xmllite_unittest',
        'rtc_xmpp_unittest',
        'sound/sound.gyp:rtc_sound',
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_depth)/testing/gmock.gyp:gmock',
      ],
      'conditions': [
        ['OS=="android"', {
          'dependencies': [
            '<(webrtc_depth)/testing/android/native_test.gyp:native_test_native_code',
          ],
        }],
      ],
    },
    {
      'target_name': 'webrtc_tests',
      'type': 'none',
      'dependencies': [
        'video_engine_tests',
        'video_loopback',
        'video_replay',
        'webrtc_perf_tests',
      ],
    },
    {
      'target_name': 'full_stack_quality_sampler',
      'type': 'executable',
      'sources': [
        'test/mac/run_test.mm',
        'test/run_test.cc',
        'test/run_test.h',
        'video/full_stack.cc',
        'video/full_stack_quality_sampler.cc',
      ],
      'conditions': [
        ['OS=="mac"', {
          'sources!': [
            'test/run_test.cc',
          ],
        }],
      ],
      'dependencies': [
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_depth)/third_party/gflags/gflags.gyp:gflags',
        '<(webrtc_root)/modules/modules.gyp:video_capture',
        'test/webrtc_test_common.gyp:webrtc_test_common',
        'test/test.gyp:test_main',
        'webrtc',
      ],
    },
    {
      'target_name': 'loopback_base',
      'type': 'static_library',
      'sources': [
        'video/loopback.cc',
        'video/loopback.h',
      ],
      'dependencies': [
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_root)/modules/modules.gyp:video_capture_module_internal_impl',
        '<(webrtc_root)/modules/modules.gyp:video_render',
        '<(webrtc_root)/system_wrappers/system_wrappers.gyp:system_wrappers',
        'webrtc',
      ],
    },
    {
      'target_name': 'video_loopback',
      'type': 'executable',
      'sources': [
        'test/mac/run_test.mm',
        'test/run_test.cc',
        'test/run_test.h',
        'video/video_loopback.cc',
      ],
      'conditions': [
        ['OS=="mac"', {
          'sources!': [
            'test/run_test.cc',
          ],
        }],
      ],
      'dependencies': [
        'loopback_base',
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_depth)/third_party/gflags/gflags.gyp:gflags',
        'test/webrtc_test_common.gyp:webrtc_test_common',
        'test/webrtc_test_common.gyp:webrtc_test_renderer',
        'test/test.gyp:test_main',
        'webrtc',
      ],
    },
    {
      'target_name': 'screenshare_loopback',
      'type': 'executable',
      'sources': [
        'test/mac/run_test.mm',
        'test/run_test.cc',
        'test/run_test.h',
        'video/screenshare_loopback.cc',
      ],
      'conditions': [
        ['OS=="mac"', {
          'sources!': [
            'test/run_test.cc',
          ],
        }],
      ],
      'dependencies': [
        'loopback_base',
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_depth)/third_party/gflags/gflags.gyp:gflags',
        'test/webrtc_test_common.gyp:webrtc_test_common',
        'test/webrtc_test_common.gyp:webrtc_test_renderer',
        'test/test.gyp:test_main',
        'webrtc',
      ],
    },
    {
      'target_name': 'video_replay',
      'type': 'executable',
      'sources': [
        'test/mac/run_test.mm',
        'test/run_test.cc',
        'test/run_test.h',
        'video/replay.cc',
      ],
      'conditions': [
        ['OS=="mac"', {
          'sources!': [
            'test/run_test.cc',
          ],
        }],
      ],
      'dependencies': [
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_depth)/third_party/gflags/gflags.gyp:gflags',
        'test/webrtc_test_common.gyp:webrtc_test_common',
        'test/webrtc_test_common.gyp:webrtc_test_renderer',
        '<(webrtc_root)/modules/modules.gyp:video_capture',
        '<(webrtc_root)/modules/modules.gyp:video_render',
        '<(webrtc_root)/system_wrappers/system_wrappers.gyp:system_wrappers_default',
        'webrtc',
      ],
    },
    {
      # TODO(pbos): Rename target to webrtc_tests or rtc_tests, this target is
      # not meant to only include video.
      'target_name': 'video_engine_tests',
      'type': '<(gtest_target_type)',
      'sources': [
        'test/common_unittest.cc',
        'test/testsupport/metrics/video_metrics_unittest.cc',
        'tools/agc/agc_manager_unittest.cc',
        'video/bitrate_estimator_tests.cc',
        'video/end_to_end_tests.cc',
        'video/packet_injection_tests.cc',
        'video/send_statistics_proxy_unittest.cc',
        'video/video_capture_input_unittest.cc',
        'video/video_decoder_unittest.cc',
        'video/video_encoder_unittest.cc',
        'video/video_send_stream_tests.cc',
      ],
      'dependencies': [
        '<(webrtc_depth)/testing/gmock.gyp:gmock',
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_root)/common.gyp:webrtc_common',
        '<(webrtc_root)/modules/modules.gyp:rtp_rtcp',
        '<(webrtc_root)/modules/modules.gyp:video_capture',
        '<(webrtc_root)/modules/modules.gyp:video_render',
        '<(webrtc_root)/test/test.gyp:channel_transport',
        '<(webrtc_root)/voice_engine/voice_engine.gyp:voice_engine',
        'test/metrics.gyp:metrics',
        'test/test.gyp:test_main',
        'test/webrtc_test_common.gyp:webrtc_test_common',
        'tools/tools.gyp:agc_manager',
        'webrtc',
      ],
      'conditions': [
        ['OS=="android"', {
          'dependencies': [
            '<(webrtc_depth)/testing/android/native_test.gyp:native_test_native_code',
          ],
        }],
        ['enable_protobuf==1', {
          'defines': [
            'ENABLE_RTC_EVENT_LOG',
          ],
          'dependencies': [
            'webrtc.gyp:rtc_event_log',
            'webrtc.gyp:rtc_event_log_proto',
          ],
          'sources': [
            'video/rtc_event_log_unittest.cc',
          ],
        }],
      ],
    },
    {
      'target_name': 'webrtc_perf_tests',
      'type': '<(gtest_target_type)',
      'sources': [
        'modules/audio_coding/neteq/test/neteq_performance_unittest.cc',
        'modules/remote_bitrate_estimator/remote_bitrate_estimators_test.cc',

        'tools/agc/agc_manager_integrationtest.cc',
        'video/call_perf_tests.cc',
        'video/full_stack.cc',
        'video/rampup_tests.cc',
        'video/rampup_tests.h',
      ],
      'dependencies': [
        '<(webrtc_depth)/testing/gmock.gyp:gmock',
        '<(webrtc_depth)/testing/gtest.gyp:gtest',
        '<(webrtc_root)/modules/modules.gyp:video_capture',
        '<(webrtc_root)/test/test.gyp:channel_transport',
        '<(webrtc_root)/voice_engine/voice_engine.gyp:voice_engine',
        'modules/modules.gyp:neteq_test_support',
        'modules/modules.gyp:bwe_simulator',
        'modules/modules.gyp:rtp_rtcp',
        'test/test.gyp:test_main',
        'test/webrtc_test_common.gyp:webrtc_test_common',
        'tools/tools.gyp:agc_manager',
        'webrtc',
      ],
      'conditions': [
        ['OS=="android"', {
          'dependencies': [
            '<(webrtc_depth)/testing/android/native_test.gyp:native_test_native_code',
          ],
        }],
      ],
    },
  ],
  'conditions': [
    ['OS=="android"', {
      'targets': [
        {
          'target_name': 'rtc_unittests_apk_target',
          'type': 'none',
          'dependencies': [
            '<(apk_tests_path):rtc_unittests_apk',
          ],
        },
        {
          'target_name': 'video_engine_tests_apk_target',
          'type': 'none',
          'dependencies': [
            '<(apk_tests_path):video_engine_tests_apk',
          ],
        },
        {
          'target_name': 'webrtc_perf_tests_apk_target',
          'type': 'none',
          'dependencies': [
            '<(apk_tests_path):webrtc_perf_tests_apk',
          ],
        },
      ],
    }],
    ['test_isolation_mode != "noop"', {
      'targets': [
        {
          'target_name': 'rtc_unittests_run',
          'type': 'none',
          'dependencies': [
            'rtc_unittests',
          ],
          'includes': [
            'build/isolate.gypi',
          ],
          'sources': [
            'rtc_unittests.isolate',
          ],
        },
        {
          'target_name': 'video_engine_tests_run',
          'type': 'none',
          'dependencies': [
            'video_engine_tests',
          ],
          'includes': [
            'build/isolate.gypi',
          ],
          'sources': [
            'video_engine_tests.isolate',
          ],
        },
        {
          'target_name': 'webrtc_perf_tests_run',
          'type': 'none',
          'dependencies': [
            'webrtc_perf_tests',
          ],
          'includes': [
            'build/isolate.gypi',
          ],
          'sources': [
            'webrtc_perf_tests.isolate',
          ],
        },
      ],
    }],
  ],
}
