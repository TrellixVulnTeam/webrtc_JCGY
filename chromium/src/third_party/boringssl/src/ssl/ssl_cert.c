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
 * Copyright (c) 1998-2007 The OpenSSL Project.  All rights reserved.
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
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project. */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/buf.h>
#include <openssl/ec_key.h>
#include <openssl/dh.h>
#include <openssl/err.h>
#include <openssl/mem.h>
#include <openssl/obj.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "../crypto/dh/internal.h"
#include "../crypto/directory.h"
#include "../crypto/internal.h"
#include "internal.h"


int SSL_get_ex_data_X509_STORE_CTX_idx(void) {
  /* The ex_data index to go from |X509_STORE_CTX| to |SSL| always uses the
   * reserved app_data slot. Before ex_data was introduced, app_data was used.
   * Avoid breaking any software which assumes |X509_STORE_CTX_get_app_data|
   * works. */
  return 0;
}

CERT *ssl_cert_new(void) {
  CERT *ret = (CERT *)OPENSSL_malloc(sizeof(CERT));
  if (ret == NULL) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return NULL;
  }
  memset(ret, 0, sizeof(CERT));

  return ret;
}

CERT *ssl_cert_dup(CERT *cert) {
  CERT *ret = (CERT *)OPENSSL_malloc(sizeof(CERT));
  if (ret == NULL) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return NULL;
  }
  memset(ret, 0, sizeof(CERT));

  ret->mask_k = cert->mask_k;
  ret->mask_a = cert->mask_a;

  if (cert->dh_tmp != NULL) {
    ret->dh_tmp = DHparams_dup(cert->dh_tmp);
    if (ret->dh_tmp == NULL) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_DH_LIB);
      goto err;
    }
    if (cert->dh_tmp->priv_key) {
      BIGNUM *b = BN_dup(cert->dh_tmp->priv_key);
      if (!b) {
        OPENSSL_PUT_ERROR(SSL, ERR_R_BN_LIB);
        goto err;
      }
      ret->dh_tmp->priv_key = b;
    }
    if (cert->dh_tmp->pub_key) {
      BIGNUM *b = BN_dup(cert->dh_tmp->pub_key);
      if (!b) {
        OPENSSL_PUT_ERROR(SSL, ERR_R_BN_LIB);
        goto err;
      }
      ret->dh_tmp->pub_key = b;
    }
  }
  ret->dh_tmp_cb = cert->dh_tmp_cb;

  ret->ecdh_nid = cert->ecdh_nid;
  ret->ecdh_tmp_cb = cert->ecdh_tmp_cb;

  if (cert->x509 != NULL) {
    ret->x509 = X509_up_ref(cert->x509);
  }

  if (cert->privatekey != NULL) {
    ret->privatekey = EVP_PKEY_up_ref(cert->privatekey);
  }

  if (cert->chain) {
    ret->chain = X509_chain_up_ref(cert->chain);
    if (!ret->chain) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
      goto err;
    }
  }

  ret->cert_cb = cert->cert_cb;
  ret->cert_cb_arg = cert->cert_cb_arg;

  return ret;

err:
  ssl_cert_free(ret);
  return NULL;
}

/* Free up and clear all certificates and chains */
void ssl_cert_clear_certs(CERT *cert) {
  if (cert == NULL) {
    return;
  }

  X509_free(cert->x509);
  cert->x509 = NULL;
  EVP_PKEY_free(cert->privatekey);
  cert->privatekey = NULL;
  sk_X509_pop_free(cert->chain, X509_free);
  cert->chain = NULL;
  cert->key_method = NULL;
}

void ssl_cert_free(CERT *c) {
  if (c == NULL) {
    return;
  }

  DH_free(c->dh_tmp);

  ssl_cert_clear_certs(c);
  OPENSSL_free(c->peer_sigalgs);
  OPENSSL_free(c->shared_sigalgs);

  OPENSSL_free(c);
}

int ssl_cert_set0_chain(CERT *cert, STACK_OF(X509) *chain) {
  sk_X509_pop_free(cert->chain, X509_free);
  cert->chain = chain;
  return 1;
}

int ssl_cert_set1_chain(CERT *cert, STACK_OF(X509) *chain) {
  STACK_OF(X509) *dchain;
  if (chain == NULL) {
    return ssl_cert_set0_chain(cert, NULL);
  }

  dchain = X509_chain_up_ref(chain);
  if (dchain == NULL) {
    return 0;
  }

  if (!ssl_cert_set0_chain(cert, dchain)) {
    sk_X509_pop_free(dchain, X509_free);
    return 0;
  }

  return 1;
}

int ssl_cert_add0_chain_cert(CERT *cert, X509 *x509) {
  if (cert->chain == NULL) {
    cert->chain = sk_X509_new_null();
  }
  if (cert->chain == NULL || !sk_X509_push(cert->chain, x509)) {
    return 0;
  }

  return 1;
}

int ssl_cert_add1_chain_cert(CERT *cert, X509 *x509) {
  if (!ssl_cert_add0_chain_cert(cert, x509)) {
    return 0;
  }

  X509_up_ref(x509);
  return 1;
}

void ssl_cert_set_cert_cb(CERT *c, int (*cb)(SSL *ssl, void *arg), void *arg) {
  c->cert_cb = cb;
  c->cert_cb_arg = arg;
}

SESS_CERT *ssl_sess_cert_new(void) {
  SESS_CERT *ret;

  ret = OPENSSL_malloc(sizeof *ret);
  if (ret == NULL) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return NULL;
  }

  memset(ret, 0, sizeof *ret);

  return ret;
}

SESS_CERT *ssl_sess_cert_dup(const SESS_CERT *sess_cert) {
  SESS_CERT *ret = ssl_sess_cert_new();
  if (ret == NULL) {
    return NULL;
  }

  if (sess_cert->cert_chain != NULL) {
    ret->cert_chain = X509_chain_up_ref(sess_cert->cert_chain);
    if (ret->cert_chain == NULL) {
      ssl_sess_cert_free(ret);
      return NULL;
    }
  }
  if (sess_cert->peer_cert != NULL) {
    ret->peer_cert = X509_up_ref(sess_cert->peer_cert);
  }
  if (sess_cert->peer_dh_tmp != NULL) {
    ret->peer_dh_tmp = sess_cert->peer_dh_tmp;
    DH_up_ref(ret->peer_dh_tmp);
  }
  if (sess_cert->peer_ecdh_tmp != NULL) {
    ret->peer_ecdh_tmp = sess_cert->peer_ecdh_tmp;
    EC_KEY_up_ref(ret->peer_ecdh_tmp);
  }
  return ret;
}

void ssl_sess_cert_free(SESS_CERT *sess_cert) {
  if (sess_cert == NULL) {
    return;
  }

  sk_X509_pop_free(sess_cert->cert_chain, X509_free);
  X509_free(sess_cert->peer_cert);
  DH_free(sess_cert->peer_dh_tmp);
  EC_KEY_free(sess_cert->peer_ecdh_tmp);

  OPENSSL_free(sess_cert);
}

int ssl_verify_cert_chain(SSL *s, STACK_OF(X509) *sk) {
  X509 *x;
  int i;
  X509_STORE_CTX ctx;

  if (sk == NULL || sk_X509_num(sk) == 0) {
    return 0;
  }

  x = sk_X509_value(sk, 0);
  if (!X509_STORE_CTX_init(&ctx, s->ctx->cert_store, x, sk)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_X509_LIB);
    return 0;
  }
  X509_STORE_CTX_set_ex_data(&ctx, SSL_get_ex_data_X509_STORE_CTX_idx(), s);

  /* We need to inherit the verify parameters. These can be determined by the
   * context: if its a server it will verify SSL client certificates or vice
   * versa. */
  X509_STORE_CTX_set_default(&ctx, s->server ? "ssl_client" : "ssl_server");

  /* Anything non-default in "param" should overwrite anything in the ctx. */
  X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(&ctx), s->param);

  if (s->verify_callback) {
    X509_STORE_CTX_set_verify_cb(&ctx, s->verify_callback);
  }

  if (s->ctx->app_verify_callback != NULL) {
    i = s->ctx->app_verify_callback(&ctx, s->ctx->app_verify_arg);
  } else {
    i = X509_verify_cert(&ctx);
  }

  s->verify_result = ctx.error;
  X509_STORE_CTX_cleanup(&ctx);

  return i;
}

static void set_client_CA_list(STACK_OF(X509_NAME) **ca_list,
                               STACK_OF(X509_NAME) *name_list) {
  sk_X509_NAME_pop_free(*ca_list, X509_NAME_free);
  *ca_list = name_list;
}

STACK_OF(X509_NAME) *SSL_dup_CA_list(STACK_OF(X509_NAME) *sk) {
  size_t i;
  STACK_OF(X509_NAME) *ret;
  X509_NAME *name;

  ret = sk_X509_NAME_new_null();
  for (i = 0; i < sk_X509_NAME_num(sk); i++) {
    name = X509_NAME_dup(sk_X509_NAME_value(sk, i));
    if (name == NULL || !sk_X509_NAME_push(ret, name)) {
      sk_X509_NAME_pop_free(ret, X509_NAME_free);
      return NULL;
    }
  }

  return ret;
}

void SSL_set_client_CA_list(SSL *s, STACK_OF(X509_NAME) *name_list) {
  set_client_CA_list(&(s->client_CA), name_list);
}

void SSL_CTX_set_client_CA_list(SSL_CTX *ctx, STACK_OF(X509_NAME) *name_list) {
  set_client_CA_list(&(ctx->client_CA), name_list);
}

STACK_OF(X509_NAME) *SSL_CTX_get_client_CA_list(const SSL_CTX *ctx) {
  return ctx->client_CA;
}

STACK_OF(X509_NAME) *SSL_get_client_CA_list(const SSL *s) {
  if (s->server) {
    if (s->client_CA != NULL) {
      return s->client_CA;
    } else {
      return s->ctx->client_CA;
    }
  } else {
    if ((s->version >> 8) == SSL3_VERSION_MAJOR && s->s3 != NULL) {
      return s->s3->tmp.ca_names;
    } else {
      return NULL;
    }
  }
}

static int add_client_CA(STACK_OF(X509_NAME) **sk, X509 *x) {
  X509_NAME *name;

  if (x == NULL) {
    return 0;
  }
  if (*sk == NULL) {
    *sk = sk_X509_NAME_new_null();
    if (*sk == NULL) {
      return 0;
    }
  }

  name = X509_NAME_dup(X509_get_subject_name(x));
  if (name == NULL) {
    return 0;
  }

  if (!sk_X509_NAME_push(*sk, name)) {
    X509_NAME_free(name);
    return 0;
  }

  return 1;
}

int SSL_add_client_CA(SSL *ssl, X509 *x) {
  return add_client_CA(&(ssl->client_CA), x);
}

int SSL_CTX_add_client_CA(SSL_CTX *ctx, X509 *x) {
  return add_client_CA(&(ctx->client_CA), x);
}

static int xname_cmp(const X509_NAME **a, const X509_NAME **b) {
  return X509_NAME_cmp(*a, *b);
}

/* Load CA certs from a file into a STACK. Note that it is somewhat misnamed;
 * it doesn't really have anything to do with clients (except that a common use
 * for a stack of CAs is to send it to the client). Actually, it doesn't have
 * much to do with CAs, either, since it will load any old cert.
 *
 * \param file the file containing one or more certs.
 * \return a ::STACK containing the certs. */
STACK_OF(X509_NAME) *SSL_load_client_CA_file(const char *file) {
  BIO *in;
  X509 *x = NULL;
  X509_NAME *xn = NULL;
  STACK_OF(X509_NAME) *ret = NULL, *sk;

  sk = sk_X509_NAME_new(xname_cmp);
  in = BIO_new(BIO_s_file());

  if (sk == NULL || in == NULL) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    goto err;
  }

  if (!BIO_read_filename(in, file)) {
    goto err;
  }

  for (;;) {
    if (PEM_read_bio_X509(in, &x, NULL, NULL) == NULL) {
      break;
    }
    if (ret == NULL) {
      ret = sk_X509_NAME_new_null();
      if (ret == NULL) {
        OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
        goto err;
      }
    }
    xn = X509_get_subject_name(x);
    if (xn == NULL) {
      goto err;
    }

    /* check for duplicates */
    xn = X509_NAME_dup(xn);
    if (xn == NULL) {
      goto err;
    }
    if (sk_X509_NAME_find(sk, NULL, xn)) {
      X509_NAME_free(xn);
    } else {
      sk_X509_NAME_push(sk, xn);
      sk_X509_NAME_push(ret, xn);
    }
  }

  if (0) {
  err:
    sk_X509_NAME_pop_free(ret, X509_NAME_free);
    ret = NULL;
  }

  sk_X509_NAME_free(sk);
  BIO_free(in);
  X509_free(x);
  if (ret != NULL) {
    ERR_clear_error();
  }
  return ret;
}

/* Add a file of certs to a stack.
 *
 * \param stack the stack to add to.
 * \param file the file to add from. All certs in this file that are not
 *     already in the stack will be added.
 * \return 1 for success, 0 for failure. Note that in the case of failure some
 *     certs may have been added to \c stack. */
int SSL_add_file_cert_subjects_to_stack(STACK_OF(X509_NAME) *stack,
                                        const char *file) {
  BIO *in;
  X509 *x = NULL;
  X509_NAME *xn = NULL;
  int ret = 1;
  int (*oldcmp)(const X509_NAME **a, const X509_NAME **b);

  oldcmp = sk_X509_NAME_set_cmp_func(stack, xname_cmp);
  in = BIO_new(BIO_s_file());

  if (in == NULL) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    goto err;
  }

  if (!BIO_read_filename(in, file)) {
    goto err;
  }

  for (;;) {
    if (PEM_read_bio_X509(in, &x, NULL, NULL) == NULL) {
      break;
    }
    xn = X509_get_subject_name(x);
    if (xn == NULL) {
      goto err;
    }
    xn = X509_NAME_dup(xn);
    if (xn == NULL) {
      goto err;
    }
    if (sk_X509_NAME_find(stack, NULL, xn)) {
      X509_NAME_free(xn);
    } else {
      sk_X509_NAME_push(stack, xn);
    }
  }

  ERR_clear_error();

  if (0) {
  err:
    ret = 0;
  }

  BIO_free(in);
  X509_free(x);

  (void) sk_X509_NAME_set_cmp_func(stack, oldcmp);

  return ret;
}

/* Add a directory of certs to a stack.
 *
 * \param stack the stack to append to.
 * \param dir the directory to append from. All files in this directory will be
 *     examined as potential certs. Any that are acceptable to
 *     SSL_add_dir_cert_subjects_to_stack() that are not already in the stack will
 *     be included.
 * \return 1 for success, 0 for failure. Note that in the case of failure some
 *     certs may have been added to \c stack. */
int SSL_add_dir_cert_subjects_to_stack(STACK_OF(X509_NAME) *stack,
                                       const char *dir) {
  OPENSSL_DIR_CTX *d = NULL;
  const char *filename;
  int ret = 0;

  /* Note that a side effect is that the CAs will be sorted by name */
  while ((filename = OPENSSL_DIR_read(&d, dir))) {
    char buf[1024];
    int r;

    if (strlen(dir) + strlen(filename) + 2 > sizeof(buf)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_PATH_TOO_LONG);
      goto err;
    }

    r = BIO_snprintf(buf, sizeof buf, "%s/%s", dir, filename);
    if (r <= 0 || r >= (int)sizeof(buf) ||
        !SSL_add_file_cert_subjects_to_stack(stack, buf)) {
      goto err;
    }
  }

  if (errno) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_SYS_LIB);
    ERR_add_error_data(3, "OPENSSL_DIR_read(&ctx, '", dir, "')");
    goto err;
  }

  ret = 1;

err:
  if (d) {
    OPENSSL_DIR_end(&d);
  }
  return ret;
}

/* Add a certificate to a BUF_MEM structure */
static int ssl_add_cert_to_buf(BUF_MEM *buf, unsigned long *l, X509 *x) {
  int n;
  uint8_t *p;

  n = i2d_X509(x, NULL);
  if (!BUF_MEM_grow_clean(buf, (int)(n + (*l) + 3))) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_BUF_LIB);
    return 0;
  }
  p = (uint8_t *)&(buf->data[*l]);
  l2n3(n, p);
  i2d_X509(x, &p);
  *l += n + 3;

  return 1;
}

/* Add certificate chain to internal SSL BUF_MEM structure. */
int ssl_add_cert_chain(SSL *ssl, unsigned long *l) {
  CERT *cert = ssl->cert;
  BUF_MEM *buf = ssl->init_buf;
  int no_chain = 0;
  size_t i;

  X509 *x = cert->x509;
  STACK_OF(X509) *chain = cert->chain;

  if (x == NULL) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_NO_CERTIFICATE_SET);
    return 0;
  }

  if ((ssl->mode & SSL_MODE_NO_AUTO_CHAIN) || chain != NULL) {
    no_chain = 1;
  }

  if (no_chain) {
    if (!ssl_add_cert_to_buf(buf, l, x)) {
      return 0;
    }

    for (i = 0; i < sk_X509_num(chain); i++) {
      x = sk_X509_value(chain, i);
      if (!ssl_add_cert_to_buf(buf, l, x)) {
        return 0;
      }
    }
  } else {
    X509_STORE_CTX xs_ctx;

    if (!X509_STORE_CTX_init(&xs_ctx, ssl->ctx->cert_store, x, NULL)) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_X509_LIB);
      return 0;
    }
    X509_verify_cert(&xs_ctx);
    /* Don't leave errors in the queue */
    ERR_clear_error();
    for (i = 0; i < sk_X509_num(xs_ctx.chain); i++) {
      x = sk_X509_value(xs_ctx.chain, i);

      if (!ssl_add_cert_to_buf(buf, l, x)) {
        X509_STORE_CTX_cleanup(&xs_ctx);
        return 0;
      }
    }
    X509_STORE_CTX_cleanup(&xs_ctx);
  }

  return 1;
}

int SSL_CTX_set0_chain(SSL_CTX *ctx, STACK_OF(X509) *chain) {
  return ssl_cert_set0_chain(ctx->cert, chain);
}

int SSL_CTX_set1_chain(SSL_CTX *ctx, STACK_OF(X509) *chain) {
  return ssl_cert_set1_chain(ctx->cert, chain);
}

int SSL_set0_chain(SSL *ssl, STACK_OF(X509) *chain) {
  return ssl_cert_set0_chain(ssl->cert, chain);
}

int SSL_set1_chain(SSL *ssl, STACK_OF(X509) *chain) {
  return ssl_cert_set1_chain(ssl->cert, chain);
}

int SSL_CTX_add0_chain_cert(SSL_CTX *ctx, X509 *x509) {
  return ssl_cert_add0_chain_cert(ctx->cert, x509);
}

int SSL_CTX_add1_chain_cert(SSL_CTX *ctx, X509 *x509) {
  return ssl_cert_add1_chain_cert(ctx->cert, x509);
}

int SSL_CTX_add_extra_chain_cert(SSL_CTX *ctx, X509 *x509) {
  return SSL_CTX_add0_chain_cert(ctx, x509);
}

int SSL_add0_chain_cert(SSL *ssl, X509 *x509) {
  return ssl_cert_add0_chain_cert(ssl->cert, x509);
}

int SSL_add1_chain_cert(SSL *ssl, X509 *x509) {
  return ssl_cert_add1_chain_cert(ssl->cert, x509);
}

int SSL_CTX_clear_chain_certs(SSL_CTX *ctx) {
  return SSL_CTX_set0_chain(ctx, NULL);
}

int SSL_CTX_clear_extra_chain_certs(SSL_CTX *ctx) {
  return SSL_CTX_clear_chain_certs(ctx);
}

int SSL_clear_chain_certs(SSL *ssl) {
  return SSL_set0_chain(ssl, NULL);
}

int SSL_CTX_get0_chain_certs(const SSL_CTX *ctx, STACK_OF(X509) **out_chain) {
  *out_chain = ctx->cert->chain;
  return 1;
}

int SSL_CTX_get_extra_chain_certs(const SSL_CTX *ctx,
                                  STACK_OF(X509) **out_chain) {
  return SSL_CTX_get0_chain_certs(ctx, out_chain);
}

int SSL_get0_chain_certs(const SSL *ssl, STACK_OF(X509) **out_chain) {
  *out_chain = ssl->cert->chain;
  return 1;
}
