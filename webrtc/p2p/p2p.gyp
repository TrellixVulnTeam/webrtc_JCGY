# Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

{
  'includes': [ '../build/common.gypi', ],
  'targets': [
    {
      'target_name': 'rtc_p2p',
      'type': 'static_library',
      'dependencies': [
        '<(webrtc_root)/base/base.gyp:rtc_base',
        '<(webrtc_root)/common.gyp:webrtc_common',
      ],
      'cflags_cc!': [
        '-Wnon-virtual-dtor',
      ],
      'sources': [
        'base/asyncstuntcpsocket.cc',
        'base/asyncstuntcpsocket.h',
        'base/basicpacketsocketfactory.cc',
        'base/basicpacketsocketfactory.h',
        'base/candidate.h',
        'base/common.h',
        'base/constants.cc',
        'base/constants.h',
        'base/dtlstransportchannel.cc',
        'base/dtlstransportchannel.h',
        'base/p2ptransport.cc',
        'base/p2ptransport.h',
        'base/p2ptransportchannel.cc',
        'base/p2ptransportchannel.h',
        'base/packetsocketfactory.h',
        'base/port.cc',
        'base/port.h',
        'base/portallocator.cc',
        'base/portallocator.h',
        'base/portinterface.h',
        'base/pseudotcp.cc',
        'base/pseudotcp.h',
        'base/rawtransport.cc',
        'base/rawtransport.h',
        'base/rawtransportchannel.cc',
        'base/rawtransportchannel.h',
        'base/relayport.cc',
        'base/relayport.h',
        'base/relayserver.cc',
        'base/relayserver.h',
        'base/session.cc',
        'base/session.h',
        'base/sessiondescription.cc',
        'base/sessiondescription.h',
        'base/sessionid.h',
        'base/stun.cc',
        'base/stun.h',
        'base/stunport.cc',
        'base/stunport.h',
        'base/stunrequest.cc',
        'base/stunrequest.h',
        'base/stunserver.cc',
        'base/stunserver.h',
        'base/tcpport.cc',
        'base/tcpport.h',
        'base/transport.cc',
        'base/transport.h',
        'base/transportchannel.cc',
        'base/transportchannel.h',
        'base/transportchannelimpl.h',
        'base/transportchannelproxy.cc',
        'base/transportchannelproxy.h',
        'base/transportdescription.cc',
        'base/transportdescription.h',
        'base/transportdescriptionfactory.cc',
        'base/transportdescriptionfactory.h',
        'base/transportinfo.h',
        'base/turnport.cc',
        'base/turnport.h',
        'base/turnserver.cc',
        'base/turnserver.h',
        'base/udpport.h',
        'client/basicportallocator.cc',
        'client/basicportallocator.h',
        'client/httpportallocator.cc',
        'client/httpportallocator.h',
        'client/socketmonitor.cc',
        'client/socketmonitor.h',
      ],
      'direct_dependent_settings': {
        'cflags_cc!': [
          '-Wnon-virtual-dtor',
        ],
        'defines': [
          'FEATURE_ENABLE_VOICEMAIL',
        ],
      },
      'conditions': [
        ['build_with_chromium==0', {
          'defines': [
            'FEATURE_ENABLE_VOICEMAIL',
            'FEATURE_ENABLE_PSTN',
          ],
        }],
      ],
    },
    {
      'target_name': 'libstunprober',
      'type': 'static_library',
      'dependencies': [
        '<(webrtc_root)/base/base.gyp:rtc_base',
        '<(webrtc_root)/common.gyp:webrtc_common',
      ],
      'cflags_cc!': [
        '-Wnon-virtual-dtor',
      ],
      'sources': [
        'stunprober/stunprober.cc',
      ],
    },
    {
      'target_name': 'stun_prober',
      'type': 'executable',
      'dependencies': [
        'libstunprober',
        'rtc_p2p'
      ],
      'cflags_cc!': [
        '-Wnon-virtual-dtor',
      ],
      'sources': [
        'stunprober/main.cc',
      ],
    }],
}

