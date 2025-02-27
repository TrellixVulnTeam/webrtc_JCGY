/* ssl/ssl3.h */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * ECC cipher suite support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */

#ifndef HEADER_SSL3_H
#define HEADER_SSL3_H

#include <openssl/aead.h>
#include <openssl/buf.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/type_check.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* These are kept to support clients that negotiates higher protocol versions
 * using SSLv2 client hello records. */
#define SSL2_MT_CLIENT_HELLO 1
#define SSL2_VERSION 0x0002

/* Signalling cipher suite value from RFC 5746. */
#define SSL3_CK_SCSV 0x030000FF
/* Fallback signalling cipher suite value from RFC 7507. */
#define SSL3_CK_FALLBACK_SCSV 0x03005600

#define SSL3_CK_RSA_NULL_MD5 0x03000001
#define SSL3_CK_RSA_NULL_SHA 0x03000002
#define SSL3_CK_RSA_RC4_40_MD5 0x03000003
#define SSL3_CK_RSA_RC4_128_MD5 0x03000004
#define SSL3_CK_RSA_RC4_128_SHA 0x03000005
#define SSL3_CK_RSA_RC2_40_MD5 0x03000006
#define SSL3_CK_RSA_IDEA_128_SHA 0x03000007
#define SSL3_CK_RSA_DES_40_CBC_SHA 0x03000008
#define SSL3_CK_RSA_DES_64_CBC_SHA 0x03000009
#define SSL3_CK_RSA_DES_192_CBC3_SHA 0x0300000A

#define SSL3_CK_DH_DSS_DES_40_CBC_SHA 0x0300000B
#define SSL3_CK_DH_DSS_DES_64_CBC_SHA 0x0300000C
#define SSL3_CK_DH_DSS_DES_192_CBC3_SHA 0x0300000D
#define SSL3_CK_DH_RSA_DES_40_CBC_SHA 0x0300000E
#define SSL3_CK_DH_RSA_DES_64_CBC_SHA 0x0300000F
#define SSL3_CK_DH_RSA_DES_192_CBC3_SHA 0x03000010

#define SSL3_CK_EDH_DSS_DES_40_CBC_SHA 0x03000011
#define SSL3_CK_EDH_DSS_DES_64_CBC_SHA 0x03000012
#define SSL3_CK_EDH_DSS_DES_192_CBC3_SHA 0x03000013
#define SSL3_CK_EDH_RSA_DES_40_CBC_SHA 0x03000014
#define SSL3_CK_EDH_RSA_DES_64_CBC_SHA 0x03000015
#define SSL3_CK_EDH_RSA_DES_192_CBC3_SHA 0x03000016

#define SSL3_CK_ADH_RC4_40_MD5 0x03000017
#define SSL3_CK_ADH_RC4_128_MD5 0x03000018
#define SSL3_CK_ADH_DES_40_CBC_SHA 0x03000019
#define SSL3_CK_ADH_DES_64_CBC_SHA 0x0300001A
#define SSL3_CK_ADH_DES_192_CBC_SHA 0x0300001B

#define SSL3_TXT_RSA_NULL_MD5 "NULL-MD5"
#define SSL3_TXT_RSA_NULL_SHA "NULL-SHA"
#define SSL3_TXT_RSA_RC4_40_MD5 "EXP-RC4-MD5"
#define SSL3_TXT_RSA_RC4_128_MD5 "RC4-MD5"
#define SSL3_TXT_RSA_RC4_128_SHA "RC4-SHA"
#define SSL3_TXT_RSA_RC2_40_MD5 "EXP-RC2-CBC-MD5"
#define SSL3_TXT_RSA_IDEA_128_SHA "IDEA-CBC-SHA"
#define SSL3_TXT_RSA_DES_40_CBC_SHA "EXP-DES-CBC-SHA"
#define SSL3_TXT_RSA_DES_64_CBC_SHA "DES-CBC-SHA"
#define SSL3_TXT_RSA_DES_192_CBC3_SHA "DES-CBC3-SHA"

#define SSL3_TXT_DH_DSS_DES_40_CBC_SHA "EXP-DH-DSS-DES-CBC-SHA"
#define SSL3_TXT_DH_DSS_DES_64_CBC_SHA "DH-DSS-DES-CBC-SHA"
#define SSL3_TXT_DH_DSS_DES_192_CBC3_SHA "DH-DSS-DES-CBC3-SHA"
#define SSL3_TXT_DH_RSA_DES_40_CBC_SHA "EXP-DH-RSA-DES-CBC-SHA"
#define SSL3_TXT_DH_RSA_DES_64_CBC_SHA "DH-RSA-DES-CBC-SHA"
#define SSL3_TXT_DH_RSA_DES_192_CBC3_SHA "DH-RSA-DES-CBC3-SHA"

#define SSL3_TXT_EDH_DSS_DES_40_CBC_SHA "EXP-EDH-DSS-DES-CBC-SHA"
#define SSL3_TXT_EDH_DSS_DES_64_CBC_SHA "EDH-DSS-DES-CBC-SHA"
#define SSL3_TXT_EDH_DSS_DES_192_CBC3_SHA "EDH-DSS-DES-CBC3-SHA"
#define SSL3_TXT_EDH_RSA_DES_40_CBC_SHA "EXP-EDH-RSA-DES-CBC-SHA"
#define SSL3_TXT_EDH_RSA_DES_64_CBC_SHA "EDH-RSA-DES-CBC-SHA"
#define SSL3_TXT_EDH_RSA_DES_192_CBC3_SHA "EDH-RSA-DES-CBC3-SHA"

#define SSL3_TXT_ADH_RC4_40_MD5 "EXP-ADH-RC4-MD5"
#define SSL3_TXT_ADH_RC4_128_MD5 "ADH-RC4-MD5"
#define SSL3_TXT_ADH_DES_40_CBC_SHA "EXP-ADH-DES-CBC-SHA"
#define SSL3_TXT_ADH_DES_64_CBC_SHA "ADH-DES-CBC-SHA"
#define SSL3_TXT_ADH_DES_192_CBC_SHA "ADH-DES-CBC3-SHA"

#define SSL3_SSL_SESSION_ID_LENGTH 32
#define SSL3_MAX_SSL_SESSION_ID_LENGTH 32

#define SSL3_MASTER_SECRET_SIZE 48
#define SSL3_RANDOM_SIZE 32
#define SSL3_SESSION_ID_SIZE 32
#define SSL3_RT_HEADER_LENGTH 5

#define SSL3_HM_HEADER_LENGTH 4

#ifndef SSL3_ALIGN_PAYLOAD
/* Some will argue that this increases memory footprint, but it's not actually
 * true. Point is that malloc has to return at least 64-bit aligned pointers,
 * meaning that allocating 5 bytes wastes 3 bytes in either case. Suggested
 * pre-gaping simply moves these wasted bytes from the end of allocated region
 * to its front, but makes data payload aligned, which improves performance. */
#define SSL3_ALIGN_PAYLOAD 8
#else
#if (SSL3_ALIGN_PAYLOAD & (SSL3_ALIGN_PAYLOAD - 1)) != 0
#error "insane SSL3_ALIGN_PAYLOAD"
#undef SSL3_ALIGN_PAYLOAD
#endif
#endif

/* This is the maximum MAC (digest) size used by the SSL library. Currently
 * maximum of 20 is used by SHA1, but we reserve for future extension for
 * 512-bit hashes. */

#define SSL3_RT_MAX_MD_SIZE 64

/* Maximum block size used in all ciphersuites. Currently 16 for AES. */

#define SSL_RT_MAX_CIPHER_BLOCK_SIZE 16

#define SSL3_RT_MAX_EXTRA (16384)

/* Maximum plaintext length: defined by SSL/TLS standards */
#define SSL3_RT_MAX_PLAIN_LENGTH 16384
/* Maximum compression overhead: defined by SSL/TLS standards */
#define SSL3_RT_MAX_COMPRESSED_OVERHEAD 1024

/* The standards give a maximum encryption overhead of 1024 bytes. In practice
 * the value is lower than this. The overhead is the maximum number of padding
 * bytes (256) plus the mac size.
 *
 * TODO(davidben): This derivation doesn't take AEADs into account, or TLS 1.1
 * explicit nonces. It happens to work because |SSL3_RT_MAX_MD_SIZE| is larger
 * than necessary and no true AEAD has variable overhead in TLS 1.2. */
#define SSL3_RT_MAX_ENCRYPTED_OVERHEAD (256 + SSL3_RT_MAX_MD_SIZE)

/* SSL3_RT_SEND_MAX_ENCRYPTED_OVERHEAD is the maximum overhead in encrypting a
 * record. This does not include the record header. Some ciphers use explicit
 * nonces, so it includes both the AEAD overhead as well as the nonce. */
#define SSL3_RT_SEND_MAX_ENCRYPTED_OVERHEAD \
    (EVP_AEAD_MAX_OVERHEAD + EVP_AEAD_MAX_NONCE_LENGTH)

OPENSSL_COMPILE_ASSERT(
    SSL3_RT_MAX_ENCRYPTED_OVERHEAD >= SSL3_RT_SEND_MAX_ENCRYPTED_OVERHEAD,
    max_overheads_are_consistent);

/* SSL3_RT_MAX_COMPRESSED_LENGTH is an alias for
 * |SSL3_RT_MAX_PLAIN_LENGTH|. Compression is gone, so don't include the
 * compression overhead. */
#define SSL3_RT_MAX_COMPRESSED_LENGTH SSL3_RT_MAX_PLAIN_LENGTH

#define SSL3_RT_MAX_ENCRYPTED_LENGTH \
  (SSL3_RT_MAX_ENCRYPTED_OVERHEAD + SSL3_RT_MAX_COMPRESSED_LENGTH)
#define SSL3_RT_MAX_PACKET_SIZE \
  (SSL3_RT_MAX_ENCRYPTED_LENGTH + SSL3_RT_HEADER_LENGTH)

#define SSL3_MD_CLIENT_FINISHED_CONST "\x43\x4C\x4E\x54"
#define SSL3_MD_SERVER_FINISHED_CONST "\x53\x52\x56\x52"

#define SSL3_VERSION 0x0300
#define SSL3_VERSION_MAJOR 0x03
#define SSL3_VERSION_MINOR 0x00

#define SSL3_RT_CHANGE_CIPHER_SPEC 20
#define SSL3_RT_ALERT 21
#define SSL3_RT_HANDSHAKE 22
#define SSL3_RT_APPLICATION_DATA 23

/* Pseudo content type for SSL/TLS header info */
#define SSL3_RT_HEADER 0x100

#define SSL3_AL_WARNING 1
#define SSL3_AL_FATAL 2

#define SSL3_AD_CLOSE_NOTIFY 0
#define SSL3_AD_UNEXPECTED_MESSAGE 10    /* fatal */
#define SSL3_AD_BAD_RECORD_MAC 20        /* fatal */
#define SSL3_AD_DECOMPRESSION_FAILURE 30 /* fatal */
#define SSL3_AD_HANDSHAKE_FAILURE 40     /* fatal */
#define SSL3_AD_NO_CERTIFICATE 41
#define SSL3_AD_BAD_CERTIFICATE 42
#define SSL3_AD_UNSUPPORTED_CERTIFICATE 43
#define SSL3_AD_CERTIFICATE_REVOKED 44
#define SSL3_AD_CERTIFICATE_EXPIRED 45
#define SSL3_AD_CERTIFICATE_UNKNOWN 46
#define SSL3_AD_ILLEGAL_PARAMETER 47      /* fatal */
#define SSL3_AD_INAPPROPRIATE_FALLBACK 86 /* fatal */

typedef struct ssl3_record_st {
  /* type is the record type. */
  uint8_t type;
  /* length is the number of unconsumed bytes of |data|. */
  uint16_t length;
  /* off is the number of consumed bytes of |data|. */
  uint16_t off;
  /* data is a non-owning pointer to the record contents. The total length of
   * the buffer is |off| + |length|. */
  uint8_t *data;
  /* epoch, in DTLS, is the epoch number of the record. */
  uint16_t epoch;
} SSL3_RECORD;

typedef struct ssl3_buffer_st {
  uint8_t *buf;       /* at least SSL3_RT_MAX_PACKET_SIZE bytes, see
                         ssl3_setup_buffers() */
  size_t len;         /* buffer size */
  int offset;         /* where to 'copy from' */
  int left;           /* how many bytes left */
} SSL3_BUFFER;

#define SSL3_CT_RSA_SIGN 1
#define SSL3_CT_DSS_SIGN 2
#define SSL3_CT_RSA_FIXED_DH 3
#define SSL3_CT_DSS_FIXED_DH 4
#define SSL3_CT_RSA_EPHEMERAL_DH 5
#define SSL3_CT_DSS_EPHEMERAL_DH 6
#define SSL3_CT_FORTEZZA_DMS 20


/* TODO(davidben): This flag can probably be merged into s3->change_cipher_spec
 * to something tri-state. (Normal / Expect CCS / Between CCS and Finished). */
#define SSL3_FLAGS_EXPECT_CCS 0x0080

typedef struct ssl3_state_st {
  long flags;

  uint8_t read_sequence[8];
  int read_mac_secret_size;
  uint8_t read_mac_secret[EVP_MAX_MD_SIZE];
  uint8_t write_sequence[8];
  int write_mac_secret_size;
  uint8_t write_mac_secret[EVP_MAX_MD_SIZE];

  uint8_t server_random[SSL3_RANDOM_SIZE];
  uint8_t client_random[SSL3_RANDOM_SIZE];

  /* flags for countermeasure against known-IV weakness */
  int need_record_splitting;

  /* The value of 'extra' when the buffers were initialized */
  int init_extra;

  /* have_version is true if the connection's final version is known. Otherwise
   * the version has not been negotiated yet. */
  char have_version;

  /* initial_handshake_complete is true if the initial handshake has
   * completed. */
  char initial_handshake_complete;

  SSL3_BUFFER rbuf; /* read IO goes into here */
  SSL3_BUFFER wbuf; /* write IO goes into here */

  SSL3_RECORD rrec; /* each decoded record goes in here */

  /* storage for Handshake protocol data received but not yet processed by
   * ssl3_read_bytes: */
  uint8_t handshake_fragment[4];
  unsigned int handshake_fragment_len;

  /* partial write - check the numbers match */
  unsigned int wnum; /* number of bytes sent so far */
  int wpend_tot;     /* number bytes written */
  int wpend_type;
  int wpend_ret; /* number of bytes submitted */
  const uint8_t *wpend_buf;

  /* handshake_buffer, if non-NULL, contains the handshake transcript. */
  BUF_MEM *handshake_buffer;
  /* handshake_hash, if initialized with an |EVP_MD|, maintains the handshake
   * hash. For TLS 1.1 and below, it is the SHA-1 half. */
  EVP_MD_CTX handshake_hash;
  /* handshake_md5, if initialized with an |EVP_MD|, maintains the MD5 half of
   * the handshake hash for TLS 1.1 and below. */
  EVP_MD_CTX handshake_md5;

  /* this is set whenerver we see a change_cipher_spec message come in when we
   * are not looking for one */
  int change_cipher_spec;

  int warn_alert;
  int fatal_alert;
  /* we allow one fatal and one warning alert to be outstanding, send close
   * alert via the warning alert */
  int alert_dispatch;
  uint8_t send_alert[2];

  int total_renegotiations;

  /* empty_record_count is the number of consecutive empty records received. */
  uint8_t empty_record_count;

  /* warning_alert_count is the number of consecutive warning alerts
   * received. */
  uint8_t warning_alert_count;

  /* State pertaining to the pending handshake.
   *
   * TODO(davidben): State is current spread all over the place. Move
   * pending handshake state here so it can be managed separately from
   * established connection state in case of renegotiations. */
  struct {
    /* actually only need to be 16+20 for SSLv3 and 12 for TLS */
    uint8_t finish_md[EVP_MAX_MD_SIZE * 2];
    int finish_md_len;
    uint8_t peer_finish_md[EVP_MAX_MD_SIZE * 2];
    int peer_finish_md_len;

    unsigned long message_size;
    int message_type;

    /* used to hold the new cipher we are going to use */
    const SSL_CIPHER *new_cipher;
    DH *dh;

    EC_KEY *ecdh; /* holds short lived ECDH key */

    /* used when SSL_ST_FLUSH_DATA is entered */
    int next_state;

    int reuse_message;

    union {
      /* sent is a bitset where the bits correspond to elements of kExtensions
       * in t1_lib.c. Each bit is set if that extension was sent in a
       * ClientHello. It's not used by servers. */
      uint32_t sent;
      /* received is a bitset, like |sent|, but is used by servers to record
       * which extensions were received from a client. */
      uint32_t received;
    } extensions;

    union {
      /* sent is a bitset where the bits correspond to elements of
       * |client_custom_extensions| in the |SSL_CTX|. Each bit is set if that
       * extension was sent in a ClientHello. It's not used by servers. */
      uint16_t sent;
      /* received is a bitset, like |sent|, but is used by servers to record
       * which custom extensions were received from a client. The bits here
       * correspond to |server_custom_extensions|. */
      uint16_t received;
    } custom_extensions;

    /* SNI extension */

    /* should_ack_sni is used by a server and indicates that the SNI extension
     * should be echoed in the ServerHello. */
    unsigned should_ack_sni:1;


    /* Client-only: cert_req determines if a client certificate is to be sent.
     * This is 0 if no client Certificate message is to be sent, 1 if there is
     * a client certificate, and 2 to send an empty client Certificate
     * message. */
    int cert_req;

    /* Client-only: ca_names contains the list of CAs received in a
     * CertificateRequest message. */
    STACK_OF(X509_NAME) *ca_names;

    /* Client-only: certificate_types contains the set of certificate types
     * received in a CertificateRequest message. */
    uint8_t *certificate_types;
    size_t num_certificate_types;

    int key_block_length;
    uint8_t *key_block;

    const EVP_AEAD *new_aead;
    uint8_t new_mac_secret_len;
    uint8_t new_fixed_iv_len;
    uint8_t new_variable_iv_len;

    /* Server-only: cert_request is true if a client certificate was
     * requested. */
    int cert_request;

    /* certificate_status_expected is true if OCSP stapling was negotiated and
     * the server is expected to send a CertificateStatus message. */
    char certificate_status_expected;

    /* Server-only: peer_ellipticcurvelist contains the EC curve IDs advertised
     * by the peer. This is only set on the server's end. The server does not
     * advertise this extension to the client. */
    uint16_t *peer_ellipticcurvelist;
    size_t peer_ellipticcurvelist_length;

    /* extended_master_secret indicates whether the extended master secret
     * computation is used in this handshake. Note that this is different from
     * whether it was used for the current session. If this is a resumption
     * handshake then EMS might be negotiated in the client and server hello
     * messages, but it doesn't matter if the session that's being resumed
     * didn't use it to create the master secret initially. */
    char extended_master_secret;

    /* Client-only: peer_psk_identity_hint is the psk_identity_hint sent by the
     * server when using a PSK key exchange. */
    char *peer_psk_identity_hint;

    /* new_mac_secret_size is unused and exists only until wpa_supplicant can
     * be updated. It is only needed for EAP-FAST, which we don't support. */
    uint8_t new_mac_secret_size;

    /* Client-only: in_false_start is one if there is a pending handshake in
     * False Start. The client may write data at this point. */
    char in_false_start;
  } tmp;

  /* Connection binding to prevent renegotiation attacks */
  uint8_t previous_client_finished[EVP_MAX_MD_SIZE];
  uint8_t previous_client_finished_len;
  uint8_t previous_server_finished[EVP_MAX_MD_SIZE];
  uint8_t previous_server_finished_len;
  int send_connection_binding; /* TODOEKR */

  /* Set if we saw the Next Protocol Negotiation extension from our peer. */
  int next_proto_neg_seen;

  /* ALPN information
   * (we are in the process of transitioning from NPN to ALPN.) */

  /* In a server these point to the selected ALPN protocol after the
   * ClientHello has been processed. In a client these contain the protocol
   * that the server selected once the ServerHello has been processed. */
  uint8_t *alpn_selected;
  size_t alpn_selected_len;

  /* In a client, this means that the server supported Channel ID and that a
   * Channel ID was sent. In a server it means that we echoed support for
   * Channel IDs and that tlsext_channel_id will be valid after the
   * handshake. */
  char tlsext_channel_id_valid;
  /* For a server:
   *     If |tlsext_channel_id_valid| is true, then this contains the
   *     verified Channel ID from the client: a P256 point, (x,y), where
   *     each are big-endian values. */
  uint8_t tlsext_channel_id[64];
} SSL3_STATE;

/* SSLv3 */
/* client */
/* extra state */
#define SSL3_ST_CW_FLUSH (0x100 | SSL_ST_CONNECT)
#define SSL3_ST_FALSE_START (0x101 | SSL_ST_CONNECT)
/* write to server */
#define SSL3_ST_CW_CLNT_HELLO_A (0x110 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CLNT_HELLO_B (0x111 | SSL_ST_CONNECT)
/* read from server */
#define SSL3_ST_CR_SRVR_HELLO_A (0x120 | SSL_ST_CONNECT)
#define SSL3_ST_CR_SRVR_HELLO_B (0x121 | SSL_ST_CONNECT)
#define DTLS1_ST_CR_HELLO_VERIFY_REQUEST_A (0x126 | SSL_ST_CONNECT)
#define DTLS1_ST_CR_HELLO_VERIFY_REQUEST_B (0x127 | SSL_ST_CONNECT)
#define SSL3_ST_CR_CERT_A (0x130 | SSL_ST_CONNECT)
#define SSL3_ST_CR_CERT_B (0x131 | SSL_ST_CONNECT)
#define SSL3_ST_CR_KEY_EXCH_A (0x140 | SSL_ST_CONNECT)
#define SSL3_ST_CR_KEY_EXCH_B (0x141 | SSL_ST_CONNECT)
#define SSL3_ST_CR_CERT_REQ_A (0x150 | SSL_ST_CONNECT)
#define SSL3_ST_CR_CERT_REQ_B (0x151 | SSL_ST_CONNECT)
#define SSL3_ST_CR_SRVR_DONE_A (0x160 | SSL_ST_CONNECT)
#define SSL3_ST_CR_SRVR_DONE_B (0x161 | SSL_ST_CONNECT)
/* write to server */
#define SSL3_ST_CW_CERT_A (0x170 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CERT_B (0x171 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CERT_C (0x172 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CERT_D (0x173 | SSL_ST_CONNECT)
#define SSL3_ST_CW_KEY_EXCH_A (0x180 | SSL_ST_CONNECT)
#define SSL3_ST_CW_KEY_EXCH_B (0x181 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CERT_VRFY_A (0x190 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CERT_VRFY_B (0x191 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CERT_VRFY_C (0x192 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CHANGE_A (0x1A0 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CHANGE_B (0x1A1 | SSL_ST_CONNECT)
#define SSL3_ST_CW_NEXT_PROTO_A (0x200 | SSL_ST_CONNECT)
#define SSL3_ST_CW_NEXT_PROTO_B (0x201 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CHANNEL_ID_A (0x220 | SSL_ST_CONNECT)
#define SSL3_ST_CW_CHANNEL_ID_B (0x221 | SSL_ST_CONNECT)
#define SSL3_ST_CW_FINISHED_A (0x1B0 | SSL_ST_CONNECT)
#define SSL3_ST_CW_FINISHED_B (0x1B1 | SSL_ST_CONNECT)
/* read from server */
#define SSL3_ST_CR_CHANGE (0x1C0 | SSL_ST_CONNECT)
#define SSL3_ST_CR_FINISHED_A (0x1D0 | SSL_ST_CONNECT)
#define SSL3_ST_CR_FINISHED_B (0x1D1 | SSL_ST_CONNECT)
#define SSL3_ST_CR_SESSION_TICKET_A (0x1E0 | SSL_ST_CONNECT)
#define SSL3_ST_CR_SESSION_TICKET_B (0x1E1 | SSL_ST_CONNECT)
#define SSL3_ST_CR_CERT_STATUS_A (0x1F0 | SSL_ST_CONNECT)
#define SSL3_ST_CR_CERT_STATUS_B (0x1F1 | SSL_ST_CONNECT)

/* server */
/* extra state */
#define SSL3_ST_SW_FLUSH (0x100 | SSL_ST_ACCEPT)
/* read from client */
#define SSL3_ST_SR_INITIAL_BYTES (0x240 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_V2_CLIENT_HELLO (0x241 | SSL_ST_ACCEPT)
/* Do not change the number values, they do matter */
#define SSL3_ST_SR_CLNT_HELLO_A (0x110 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CLNT_HELLO_B (0x111 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CLNT_HELLO_C (0x112 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CLNT_HELLO_D (0x115 | SSL_ST_ACCEPT)
/* write to client */
#define SSL3_ST_SW_HELLO_REQ_A (0x120 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_HELLO_REQ_B (0x121 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_HELLO_REQ_C (0x122 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SRVR_HELLO_A (0x130 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SRVR_HELLO_B (0x131 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_CERT_A (0x140 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_CERT_B (0x141 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_KEY_EXCH_A (0x150 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_KEY_EXCH_B (0x151 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_KEY_EXCH_C (0x152 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_KEY_EXCH_D (0x153 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_CERT_REQ_A (0x160 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_CERT_REQ_B (0x161 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SRVR_DONE_A (0x170 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SRVR_DONE_B (0x171 | SSL_ST_ACCEPT)
/* read from client */
#define SSL3_ST_SR_CERT_A (0x180 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CERT_B (0x181 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_KEY_EXCH_A (0x190 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_KEY_EXCH_B (0x191 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CERT_VRFY_A (0x1A0 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CERT_VRFY_B (0x1A1 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CHANGE (0x1B0 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_NEXT_PROTO_A (0x210 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_NEXT_PROTO_B (0x211 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CHANNEL_ID_A (0x230 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_CHANNEL_ID_B (0x231 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_FINISHED_A (0x1C0 | SSL_ST_ACCEPT)
#define SSL3_ST_SR_FINISHED_B (0x1C1 | SSL_ST_ACCEPT)

/* write to client */
#define SSL3_ST_SW_CHANGE_A (0x1D0 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_CHANGE_B (0x1D1 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_FINISHED_A (0x1E0 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_FINISHED_B (0x1E1 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SESSION_TICKET_A (0x1F0 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SESSION_TICKET_B (0x1F1 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_CERT_STATUS_A (0x200 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_CERT_STATUS_B (0x201 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SUPPLEMENTAL_DATA_A (0x220 | SSL_ST_ACCEPT)
#define SSL3_ST_SW_SUPPLEMENTAL_DATA_B (0x221 | SSL_ST_ACCEPT)

#define SSL3_MT_HELLO_REQUEST 0
#define SSL3_MT_CLIENT_HELLO 1
#define SSL3_MT_SERVER_HELLO 2
#define SSL3_MT_NEWSESSION_TICKET 4
#define SSL3_MT_CERTIFICATE 11
#define SSL3_MT_SERVER_KEY_EXCHANGE 12
#define SSL3_MT_CERTIFICATE_REQUEST 13
#define SSL3_MT_SERVER_DONE 14
#define SSL3_MT_CERTIFICATE_VERIFY 15
#define SSL3_MT_CLIENT_KEY_EXCHANGE 16
#define SSL3_MT_FINISHED 20
#define SSL3_MT_CERTIFICATE_STATUS 22
#define SSL3_MT_SUPPLEMENTAL_DATA 23
#define SSL3_MT_NEXT_PROTO 67
#define SSL3_MT_ENCRYPTED_EXTENSIONS 203
#define DTLS1_MT_HELLO_VERIFY_REQUEST 3


#define SSL3_MT_CCS 1

/* These are used when changing over to a new cipher */
#define SSL3_CC_READ 0x01
#define SSL3_CC_WRITE 0x02
#define SSL3_CC_CLIENT 0x10
#define SSL3_CC_SERVER 0x20
#define SSL3_CHANGE_CIPHER_CLIENT_WRITE (SSL3_CC_CLIENT | SSL3_CC_WRITE)
#define SSL3_CHANGE_CIPHER_SERVER_READ (SSL3_CC_SERVER | SSL3_CC_READ)
#define SSL3_CHANGE_CIPHER_CLIENT_READ (SSL3_CC_CLIENT | SSL3_CC_READ)
#define SSL3_CHANGE_CIPHER_SERVER_WRITE (SSL3_CC_SERVER | SSL3_CC_WRITE)


#ifdef  __cplusplus
}
#endif
#endif
