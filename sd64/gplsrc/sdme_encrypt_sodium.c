/* SDME_ENCRYPT_SODIUM.C
 * Encypt / Decrypt using libsodium  - integration for ScarletDME via SDME.EXT (op_sdme_ext.c) BASIC function
 *
 * 
 * START-HISTORY:
 * 15 Jul 2024 MAB add SDME_ENCRYPT_SODIUM.C
 * START-DESCRIPTION:
 * 
 *  Encryption, Decryptoin and encoding using libsodium package
 *  https://doc.libsodium.org/
 *  This routine is based on information (code) found here:
 *  https://doc.libsodium.org/secret-key_cryptography/secretbox
 *  https://github.com/jedisct1/libsodium/blob/master/test/default/secretbox_easy2.c
 *  https://doc.libsodium.org/password_hashing/default_phf
 * 
 *  we encrypt using crypto_secretbox_easy
 * 
 *  int crypto_secretbox_easy(unsigned char *c, const unsigned char *m,
 *                         unsigned long long mlen, const unsigned char *n,
 *                         const unsigned char *k)
 * 
 *  The crypto_secretbox_easy() function encrypts a message m whose length is mlen bytes, with a key k and a nonce n. 
 *   k should be crypto_secretbox_KEYBYTES bytes (currently defined as 32 bytes / 256 bits) and n should be crypto_secretbox_NONCEBYTES bytes.
 *   c should be at least crypto_secretbox_MACBYTES + mlen bytes long.
 *   This function writes the authentication tag, whose length is crypto_secretbox_MACBYTES bytes,
 *    in c, immediately followed by the encrypted message, whose length is the same as the plaintext: mlen.
 *  Functions returning an int return 0 on success and -1 to indicate an error.
 * 
 *  REM:
 *   An 256-bit key can be expressed as a hexadecimal string with 64 characters. It will require 44 characters in base64.
 * 
 *  Note we return the encrtption output with the nonce appended to the end
 *  Rem to encode either base64 or hex before returning to ScarletDME!
 * 
 * we decrypt using crypto_secretbox_open_easy
 * 
 *  int crypto_secretbox_open_easy(unsigned char *m, const unsigned char *c,
 *                              unsigned long long clen, const unsigned char *n,
 *                              const unsigned char *k);
 * 
 *  c is a pointer to an authentication tag + encrypted message combination,
 *    as produced by crypto_secretbox_easy().
 *  clen is the length of this authentication tag + encrypted message combination.
 *  Put differently, clen is the number of bytes written by crypto_secretbox_easy(),
 *    which is crypto_secretbox_MACBYTES + the length of the message.
 *  The nonce n and the key k have to match those used to encrypt and authenticate the message.
 *  The function returns -1 if the verification fails, and 0 on success. 
 *  On success, the decrypted message is stored into m.
 * 
 *  If the user wishes to use a password for encryption / decryption we need to generate a key for it.
 *  The project recommends using crypto_pwhash to convert a password to a key, but to be reproducible the routine requires:
 *    the salt to be know along with some other parameter constants
 *    https://doc.libsodium.org/key_derivation and https://doc.libsodium.org/password_hashing/default_phf
 * 
 * To do this we probably will use function:
 * 
 *  int crypto_pwhash(unsigned char * const out,
 *                  unsigned long long outlen,
 *                  const char * const passwd,
 *                  unsigned long long passwdlen,
 *                 const unsigned char * const salt,
 *                 unsigned long long opslimit,
 *                  size_t memlimit, int alg);
 *  The crypto_pwhash() function derives an outlen bytes long key from a password passwd whose length is passwdlen
 *  and a salt salt whose fixed length is crypto_pwhash_SALTBYTES bytes. 
 *  passwdlen should be at least crypto_pwhash_PASSWD_MIN and crypto_pwhash_PASSWD_MAX.
 *  outlen should be  at least crypto_pwhash_BYTES_MIN = 16 (128 bits) and at most crypto_pwhash_BYTES_MAX. 
 *
 *  The salt should be unpredictable. randombytes_buf() is the easiest way to fill the crypto_pwhash_SALTBYTES bytes of the salt.
 * 
 *  Keep in mind that to produce the same key from the same password, the same algorithm,
 *  the same salt, and the same values for opslimit and memlimit must be used.
 *  Therefore, these parameters must be stored for each user. 
 * 
 * 
 * 
 * END-DESCRIPTION
 *
 * START-CODE
 */

#include "sd.h"
#include <stdlib.h>
#include <stdio.h>
#include <sodium.h>

#include "SDME_EXT.h"

void sdme_crypt(int key, char *Arg0, char *Arg1);
void sdme_err_rsp(int errnbr);

int sdme_encryptHX(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char *encrypted_HX, int encrypted_HX_len);

int sdme_encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char **cipher_out, size_t *cipher_out_len);
int sdme_decrypt(unsigned char *cipher_in, int cipher_in_len, unsigned char *key, unsigned char **plantext_out);

#ifdef dumphex
void dump_hex_buff(unsigned char buf[], unsigned int len)
{
    int i;
    for (i=0; i<len; i++) printf("%02X ", buf[i]);
    printf("\r\n");
}
#endif

/* main function that sorts it all out */
void sdme_crypt(int func_key, char *Arg0, char *Arg1) {
  char myErrMsg[SDMEE_ErrMsg_LEN + 1];    /* hardcoded max string lenght is set to 512 char need to do something more "durable" */
  unsigned char key[crypto_secretbox_KEYBYTES];
  unsigned char *cipher_buf;
  unsigned char *plaintext_buf;
  char *encode_out;

  size_t key_len;
  size_t bin_len;
  size_t cipher_buf_len;
  size_t encode_sz;
  size_t plaintext_sz;
  size_t encrypted_sz;
  

  encode_out = NULL;
  cipher_buf = NULL;

  /* set the process.status flag to  "successful"      */
  /* User can retrieve this status with the BASIC function STATUS()*/
  process.status = 0;

  /* all important libsodium initialization */
  if (sodium_init() == -1) {
    sdme_err_rsp(SDMME_SodInit_Err);  /* initialization error */
    return ;
  }

  /*Evaluate Function KEY */

  switch (func_key) {
    case SDMEE_EncryptHX: /* Encrypt Arg1 text with Key Arg0 returning encrypted text in hex string format */

      plaintext_sz = strlen(Arg1); /* size of text to encrypt */
      if (plaintext_sz == 0){
        sdme_err_rsp(SDMME_Encrypt_Err);   /* nothing to encrypt */
        break;
      }


      key_len = strlen(Arg0);  /* encoded key length */
      /* valid key lenght (rem encoded in hex so 2X the expected sixe)*/
      if (key_len != crypto_secretbox_KEYBYTES *2){
        sdme_err_rsp(SDMME_KeyLen_Err);    /* bad key */
        break;
      }

      /* convert key from hex encodeing to bytes */
      if (sodium_hex2bin(key, crypto_secretbox_KEYBYTES, Arg0, key_len, NULL, &bin_len, NULL) != 0) {
        sdme_err_rsp(SDMME_Decode_Err);
        break;
      }

      /* encrypt the text*/
      cipher_buf_len = 0;    /* get rid of 'cipher_buf_len’ may be used uninitialized warning????*/
      /* rem sdme_encrypt allocates mem for cipher_buf */
      if (sdme_encrypt((unsigned char *)Arg1, plaintext_sz, key, &cipher_buf, &cipher_buf_len) != 0) {
        sdme_err_rsp(SDMME_Encrypt_Err);   /* encrypt failed */
        break;
      }

      /* allocate our encode buffer*/
      encode_sz = (cipher_buf_len * 2) + 1;
      encode_out = sodium_malloc(encode_sz);
      if (encode_out == NULL){
        sodium_free(cipher_buf);
        cipher_buf = NULL;
        sdme_err_rsp(SDMME_Mem_Err);
        break;  
      }

      /* encode cipher text */
      sodium_bin2hex(encode_out, encode_sz, cipher_buf, cipher_buf_len);
      /*  sodium_free(encode_out);
        sodium_free(cipher_buf);
        encode_out = NULL;
        cipher_buf = NULL;
        sdme_err_rsp(SDMME_Encode_Err); 
        break;
       */ 
      


      /* we made it, pass encrypted and encoded text back to caller */
      k_put_c_string(encode_out, e_stack); /* sets descr as type string and encrypted and encoded text it */
      e_stack++;
      /* and free up our buffers */
      sodium_free(encode_out);
      sodium_free(cipher_buf);
      encode_out = NULL;
      cipher_buf = NULL;
      break;

    case SDMEE_DecryptHX: /* Decrypt Arg1 text with Key Arg0 returning decripted text  */

      encrypted_sz = strlen(Arg1);    /* size of the encoded encrypted string passed*/
      /* rem this encryption method has appended to the end of the string: 
         1) the authentication tag of size  crypto_secretbox_MACBYTES
         2) the nonce of size crypto_secretbox_NONCEBYTES 
         All hex encoded.
         If the string to decode then decrypt is smaller than (crypto_secretbox_MACBYTES + crypto_secretbox_NONCEBYTES) *2)
        Something is not right, error out*/
      if (encrypted_sz < (crypto_secretbox_MACBYTES + crypto_secretbox_NONCEBYTES) *2){
        sdme_err_rsp(SDMME_Decrypt_Err);
        break;
      }

      key_len = strlen(Arg0);  /* encoded key length */
      /* valid key lenght (rem encoded in hex so 2X the expected sixe)*/
      if (key_len != crypto_secretbox_KEYBYTES *2){
        sdme_err_rsp(SDMME_KeyLen_Err);
        break;
      }


      /* convert key from hex encodeing to bytes */
      if (sodium_hex2bin(key, crypto_secretbox_KEYBYTES, Arg0, key_len, NULL, &bin_len, NULL) != 0) {
        sdme_err_rsp(SDMME_Decode_Err);
        break;
      }
  
      /* need a buffer to hold decoded, encrypted bytes, which SHOULD be 1/2 the size of the Hex Encoded Encrypted Text */
      bin_len = (encrypted_sz / 2) + 1;
      cipher_buf = sodium_malloc(bin_len);
      if (cipher_buf == NULL){
        sdme_err_rsp(SDMME_Mem_Err);
        break;  
      }
      
      /* decode the encoded encrypted text */
      if (sodium_hex2bin(cipher_buf, encrypted_sz / 2, Arg1, encrypted_sz, NULL, &bin_len, NULL) != 0) {
        sodium_free(cipher_buf);
        cipher_buf = NULL;
        sdme_err_rsp(SDMME_Decode_Err);
        break;
      }

      /* need a buffer to hold the decrypted text */
      plaintext_buf = sodium_malloc(bin_len);
      if (plaintext_buf == NULL){
        sodium_free(cipher_buf);
        cipher_buf = NULL;
        sdme_err_rsp(SDMME_Mem_Err);
        break;  
      }

      plaintext_sz = 0;  /* get rid of 'plaintext_sz’ may be used uninitialized warning????*/
      /* decrypt the encrypted byte buffer */

      if (sdme_decrypt(cipher_buf, bin_len, key, &plaintext_buf) != 0) {
        sodium_free(cipher_buf);
        cipher_buf = NULL;
        sodium_free(plaintext_buf);
        plaintext_buf = NULL;
        sdme_err_rsp(SDMME_Decrypt_Err);
        break; 
      }
 
     /* we made it, text back to caller */
      k_put_c_string((char *)plaintext_buf, e_stack); /* sets descr as type string and encrypted and encoded text it */
      e_stack++;
      /* and free up our buffers */
      sodium_free(cipher_buf);
      cipher_buf = NULL;
      sodium_free(plaintext_buf);
      plaintext_buf = NULL;
      break;

    default:
      /* unknown key */
      process.status = 1;    /* BASIC STATUS() function return value */
      snprintf( myErrMsg, SDMEE_ErrMsg_LEN, "op_sdme_ext() Invalid Key: %d\n", func_key);
      k_put_c_string(myErrMsg, e_stack);   /* sets as descr as type string and place the value in myErrMsg into it */
                                           /* this will then get transferred to RTNVAL via */
      e_stack++;


  }
  return;
}


/* generic error return with null response, setting process.status */
void sdme_err_rsp(int errNbr){
  char EmptyResp[1] = {'\0'}; /*  empty return message  */
  k_put_c_string(EmptyResp, e_stack); /* sets descr as type string, empty */
  e_stack++;
  process.status = errNbr;

}


/* Encrypt plaintest using key returning cipher_out
   Caller is responsible for freeing cipher_out buffer! */
int sdme_encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char **cipher_out_buf, size_t * cipher_out_len) {
  unsigned char nonce[crypto_secretbox_NONCEBYTES]; /* buffer for nonce (Initialization Vector IV by any other name)*/
  size_t cipher_len;
  unsigned char *cipher_out;

  /* Using random bytes to create our nonce (used only once) */
  randombytes_buf(nonce, crypto_secretbox_NONCEBYTES);
  #ifdef dumphex
  printf("nonce:\r\n");
  dump_hex_buff(nonce, crypto_secretbox_NONCEBYTES);
  #endif

  /* In our implementation we save the nance at the end of the cybertext, we need to make space for it  in our buffer*/
  cipher_len = crypto_secretbox_MACBYTES + crypto_secretbox_NONCEBYTES + plaintext_len;
  cipher_out = (unsigned char *)sodium_malloc(cipher_len);
  if (cipher_out == NULL) {
    return SDMME_Mem_Err;
  }

  /* perform the encryption  */
  if (crypto_secretbox_easy(cipher_out, plaintext, (unsigned long long)plaintext_len, nonce, key) != 0) {
    sodium_free(cipher_out);
    cipher_out = NULL;
    return SDMME_Encrypt_Err;
  }
    #ifdef dumphex
    printf("ciphertext:\r\n");
    dump_hex_buff(cipher_out, cipher_len);
    #endif

  /* now append the nonce to the cipher output */
  /* rem only reason this works is cipher to plain text is a one to one */
  memcpy(cipher_out + plaintext_len + crypto_secretbox_MACBYTES, nonce, crypto_secretbox_NONCEBYTES);
  #ifdef dumphex
  printf("ciphertext+nonce:\r\n");
  dump_hex_buff(cipher_out, cipher_len);
  #endif

  /* caller needs to know the size of cipher output and its location */
  * cipher_out_len = cipher_len;
  * cipher_out_buf = cipher_out;

  return 0;
}

/* Decrypt cipher_in using key returning plantext_out
   Caller is responsible for freeing plantext_out buffer! */

int sdme_decrypt(unsigned char *cipher_in, int cipher_in_len, unsigned char *key, unsigned char **plantext_out_ptr) {
  unsigned char nonce[crypto_secretbox_NONCEBYTES]; /* buffer for nonce (Initialization Vector IV by any other name)*/
  unsigned char *plaintext_out;
  unsigned int plaintext_len;
  unsigned int cipher_len;

  /* Remeber SDMEE_Encrypt appends the nonce to the end of the cipher output, do not what to add to decrypted output */
  cipher_len = cipher_in_len - crypto_secretbox_NONCEBYTES;

  /* Also remember cipher_len is the length of this authentication tag + encrypted message */
  /* We want the actual message size*/
  plaintext_len = cipher_len - crypto_secretbox_MACBYTES;

  /* allocate space for the plaintext output, add room for string terminator */
  plaintext_out = (unsigned char *)sodium_malloc(plaintext_len+1);
  if (plaintext_out == NULL) {
    return SDMME_Mem_Err;
  }

  /* pull off the nonce from the end of the cipher */
  memcpy(nonce, cipher_in + cipher_len, crypto_secretbox_NONCEBYTES);

  /* decode our cihper */
  if (crypto_secretbox_open_easy(plaintext_out, cipher_in, (unsigned long long)cipher_len, nonce, key) == 0) {
    /* success, add in string terminator */
    plaintext_out[plaintext_len] = '\0';
    *plantext_out_ptr = plaintext_out;
  } else { 
    /* failed, free memory */ 
    sodium_free(plaintext_out);
    plaintext_out = NULL;
    return SDMME_Decrypt_Err;
  }

  return 0;
}
