/*	$NetBSD: crypto-headers.h,v 1.3.14.2 2023/09/15 15:34:27 martin Exp $	*/

#ifndef __crypto_header__
#define __crypto_header__

#ifndef PACKAGE_NAME
#error "need config.h"
#endif

#ifdef HAVE_HCRYPTO_W_OPENSSL


#define OPENSSL_DES_LIBDES_COMPATIBILITY

#include <openssl/evp.h>
#include <openssl/des.h>
#include <openssl/rc4.h>
#include <openssl/rc2.h>
#include <openssl/md4.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/ui.h>
#include <openssl/rand.h>
#include <openssl/engine.h>
#include <openssl/pkcs12.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/ecdh.h>
#if OPENSSL_VERSION_NUMBER < 0x10100000UL
# ifndef BN_is_negative
#  define BN_set_negative(bn, flag) ((bn)->neg=(flag)?1:0)
#  define BN_is_negative(bn) ((bn)->neg != 0)
# endif
#endif

#include <hcrypto/ui.h>

#else /* !HAVE_HCRYPTO_W_OPENSSL */

#ifdef KRB5
#include <krb5/krb5-types.h>
#endif

#include <hcrypto/evp.h>
#include <hcrypto/des.h>
#include <hcrypto/md4.h>
#include <hcrypto/md5.h>
#include <hcrypto/sha.h>
#include <hcrypto/rc4.h>
#include <hcrypto/rc2.h>
#include <hcrypto/ui.h>
#include <hcrypto/rand.h>
#include <hcrypto/engine.h>
#include <hcrypto/pkcs12.h>
#include <hcrypto/hmac.h>

#endif

#endif /* __crypto_header__ */
