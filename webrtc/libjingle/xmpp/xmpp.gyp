# Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

{
  'includes': [ '../../build/common.gypi', ],
  'targets': [
    {
      'target_name': 'rtc_xmpp',
      'type': 'static_library',
      'dependencies': [
        '<(webrtc_root)/base/base.gyp:rtc_base',
        '<(webrtc_root)/libjingle/xmllite/xmllite.gyp:rtc_xmllite',
      ],
      'defines': [
        'FEATURE_ENABLE_SSL',
      ],
      'cflags_cc!': [
        '-Wnon-virtual-dtor',
      ],
      'sources': [
        'asyncsocket.h',
        'chatroommodule.h',
        'chatroommoduleimpl.cc',
        'constants.cc',
        'constants.h',
        'discoitemsquerytask.cc',
        'discoitemsquerytask.h',
        'hangoutpubsubclient.cc',
        'hangoutpubsubclient.h',
        'iqtask.cc',
        'iqtask.h',
        'jid.cc',
        'jid.h',
        'module.h',
        'moduleimpl.cc',
        'moduleimpl.h',
        'mucroomconfigtask.cc',
        'mucroomconfigtask.h',
        'mucroomdiscoverytask.cc',
        'mucroomdiscoverytask.h',
        'mucroomlookuptask.cc',
        'mucroomlookuptask.h',
        'mucroomuniquehangoutidtask.cc',
        'mucroomuniquehangoutidtask.h',
        'pingtask.cc',
        'pingtask.h',
        'plainsaslhandler.h',
        'presenceouttask.cc',
        'presenceouttask.h',
        'presencereceivetask.cc',
        'presencereceivetask.h',
        'presencestatus.cc',
        'presencestatus.h',
        'prexmppauth.h',
        'pubsub_task.cc',
        'pubsub_task.h',
        'pubsubclient.cc',
        'pubsubclient.h',
        'pubsubstateclient.cc',
        'pubsubstateclient.h',
        'pubsubtasks.cc',
        'pubsubtasks.h',
        'receivetask.cc',
        'receivetask.h',
        'rostermodule.h',
        'rostermoduleimpl.cc',
        'rostermoduleimpl.h',
        'saslcookiemechanism.h',
        'saslhandler.h',
        'saslmechanism.cc',
        'saslmechanism.h',
        'saslplainmechanism.h',
        'xmppauth.cc',
        'xmppauth.h',
        'xmppclient.cc',
        'xmppclient.h',
        'xmppclientsettings.h',
        'xmppengine.h',
        'xmppengineimpl.cc',
        'xmppengineimpl.h',
        'xmppengineimpl_iq.cc',
        'xmpplogintask.cc',
        'xmpplogintask.h',
        'xmpppump.cc',
        'xmpppump.h',
        'xmppsocket.cc',
        'xmppsocket.h',
        'xmppstanzaparser.cc',
        'xmppstanzaparser.h',
        'xmpptask.cc',
        'xmpptask.h',
        'xmppthread.cc',
        'xmppthread.h',
      ],
      'direct_dependent_settings': {
        'cflags_cc!': [
          '-Wnon-virtual-dtor',
        ],
        'defines': [
          'FEATURE_ENABLE_SSL',
          'FEATURE_ENABLE_VOICEMAIL',
        ],
      },
      'conditions': [
        ['build_expat==1', {
          'dependencies': [
            '<(webrtc_depth)/third_party/expat/expat.gyp:expat',
          ],
          'export_dependent_settings': [
            '<(webrtc_depth)/third_party/expat/expat.gyp:expat',
          ],
        }],
        ['build_with_chromium==0', {
          'defines': [
            'FEATURE_ENABLE_VOICEMAIL',
            'FEATURE_ENABLE_PSTN',
          ],
        }],
        ['os_posix==1', {
          'configurations': {
            'Debug_Base': {
              'defines': [
                # Chromium's build/common.gypi defines this for all posix
                # _except_ for ios & mac.  We want it there as well, e.g.
                # because ASSERT and friends trigger off of it.
                '_DEBUG',
              ],
            },
          }
        }],
        ['OS=="android"', {
          'cflags!': [
            '-Wextra',
            '-Wall',
          ],
        }],
      ],
    }],
}

