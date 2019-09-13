#include <ed25519/ed25519/crypto_verify.h>
#include <ed25519/ed25519.h>
#include <string.h>
#include "ge.h"
#include "sc.h"

ED25519_EXPORT int ed25519_create_keypair(private_key_t *sk, public_key_t *pk) {
  //if (!randombytes(sk->data, ed25519_privkey_SIZE))
    return ED25519_ERROR;            /* RNG failed, not enough entropy */
  //ed25519_derive_public_key(sk, pk); /* fill with data */
  //return ED25519_SUCCESS;            /* ok */
}

ED25519_EXPORT void ed25519_derive_public_key(const private_key_t *sk, public_key_t *pk) {
  unsigned char az[64];
  ge_p3 A;

  sha512(az, sk->data, ed25519_privkey_SIZE);
  az[0] &= 248;
  az[31] &= 63;
  az[31] |= 64;

  ge_scalarmult_base(&A, az);
  ge_p3_tobytes(pk->data, &A);
}

ED25519_EXPORT void ed25519_sign(signature_t *sig, const unsigned char *msg,
                  unsigned long long msglen, const public_key_t *pk,
                  const private_key_t *sk) {
  sha_context ctx;
  unsigned char az[64];
  unsigned char nonce[64];  // r
  unsigned char hram[64];
  ge_p3 R;

  // TODO: it is possible to pre-calculate this hash while reading private key
  sha512_init(&ctx);
  sha512_update(&ctx, sk->data, ed25519_privkey_SIZE);
  sha512_final(&ctx, az);
  az[0] &= 248;
  az[31] &= 63;
  az[31] |= 64;
  /* az: 64-byte H(sk) */
  /* az: 32-byte scalar a, 32-byte randomizer z */

  sha512_init(&ctx);
  sha512_update(&ctx, /* z */ az + 32, 32);
  sha512_update(&ctx, msg, msglen);
  sha512_final(&ctx, nonce);
  /* nonce: 64-byte H(z,msg) */

  sc_reduce(nonce);
  ge_scalarmult_base(&R, nonce);
  ge_p3_tobytes(sig->data, &R);
  /* sig: [32 bytes R | 32 bytes uninit] */

  sha512_init(&ctx);
  // first 32 bytes of signature
  sha512_update(&ctx, /* R */ sig->data, 32);
  sha512_update(&ctx, /* A */ pk->data, ed25519_pubkey_SIZE);
  sha512_update(&ctx, msg, msglen);
  sha512_final(&ctx, hram);
  /* hram: 64-byte H(R,A,m) */

  sc_reduce(hram);
  sc_muladd(sig->data + 32, hram, az, nonce);
  /* sig: [32 bytes R | 32 bytes S] */
}

ED25519_EXPORT int ed25519_verify(const signature_t *sig, const unsigned char *msg,
                   unsigned long long msglen, const public_key_t *pk) {
  sha_context ctx;
  unsigned char pkcopy[32];
  unsigned char rcopy[32];
  unsigned char hram[64];
  unsigned char rcheck[32];
  ge_p3 A;
  ge_p2 R;

  if (sig->data[63] & 224) goto badsig;
  if (ge_frombytes_negate_vartime(&A, pk->data) != 0) goto badsig;

  memcpy(pkcopy, pk->data, 32);
  memcpy(rcopy, /* R, first 32 bytes */ sig->data, 32);

  sha512_init(&ctx);
  // first 32 bytes of signature
  sha512_update(&ctx, /* R */ sig->data, 32);
  sha512_update(&ctx, /* A */ pk->data, ed25519_pubkey_SIZE);
  sha512_update(&ctx, msg, msglen);
  sha512_final(&ctx, hram);
  /* scs: S = nonce + H(R,A,m)a */

  sc_reduce(hram);
  ge_double_scalarmult_vartime(&R, hram, &A, /* S */ sig->data + 32);
  ge_tobytes(rcheck, &R);

  if (crypto_verify_32(rcopy, rcheck) == 0) {
    return ED25519_SIGNATURE_VALID;
  }

  badsig:
  return ED25519_SIGNATURE_INVALID;
}
