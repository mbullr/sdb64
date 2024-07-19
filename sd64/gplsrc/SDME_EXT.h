/* SDME_EXT.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * 
 * Ladybridge Systems can be contacted via the www.openqm.com web site.
*/

/* SDME_EXT.h  */
#ifndef SDME_EXT
# define SDME_EXT

/* Misc stuff */
#define SDMEE_ErrMsg_LEN  512  /* buffer setup for error message */
#define SDMEE_Max_Args     10  /* max number of Args we are buffering */

/* encrytption decryption  */
#define SDMEE_EncryptHX      200  /* perfrom encryption on passsed text, return in encoded Hex String */
#define SDMEE_DecryptHX      201  /* perfrom decryption  on  passed encrypted text encoded Hex String */
#define SDMEE_Encrypt64      202  /* perfrom encryption on passsed text, return in encoded base64 string */
#define SDMEE_Decrypt64      203  /* perfrom decryption on passed encrypted text encoded base64 String */
#define SDMEE_EncodeHX     204  /* encode passed string as Hex String */
#define SDMEE_Encode64     205  /* encode as Base64 */
#define SDMEE_DecodeHX     206  /* decode passed Hex String */
#define SDMEE_Decode64     207  /* decode Base64 */

/* Key Codes for op_sdme_ext     */
#define SDMEE_TestIt 1  /* Function Test */

/* embedded python */
#define  SDMEE_PyInit        2000  /* initialize the python interpreter   */
#define  SDMEE_PyFinal       2001  /* Finalize the python interpreter   */
#define  SDMEE_IsPyInit      2002  /* Is python interpreter initialized   */
#define  SDMEE_PyRunStr      2010  /* Take the string in qmBasic variable VAL and run in python interpreter   */
#define  SDMEE_PyRunFile     2011  /* Take the file and path defined in qmBasic variable VAL and run in python interpreter   */
#define  SDMEE_PyGetAtt      2100  /* Return the (string) value of python attribute defined in qmBasic variable VAL   */

/* SDMEE Error Codes */
/* note, basic funct STATUS returns abs value of process.status */
/* And we do not want to get confused with QM / ScarletDME Standard error code found */
/* in SYSCOM ERR.h. So Start at 10000 */
/* Generic error codes */
#define SDMME_Mem_Err   -10100  /* memory allocation error */

/* encrytption decryption error codes */
#define SDMME_SodInit_Err -10199  /* libsodium initialization error */
#define SDMME_Encrypt_Err -10200  /* error in call to crypto_secretbox_easy */
#define SDMME_Decrypt_Err -10201  /* error in call to crypto_secretbox_easy_open */
#define SDMME_Encode_Err  -10202  /* error in call to bin2hex or bin2base64 */
#define SDMME_Decode_Err  -10203  /* error in call to hex2bin or base642bin */
#define SDMME_KeyLen_Err  -10204  /* key length error */

/* Embedded Python Error codes   */
#define SDMEE_PyEr_String     "<@@!>"  /* text string preamble to signify string response from Eng_PyGetAtt is an error msg */
#define SDMEE_PyEr_NotInit    -12001    /* interperter not initiialized */
#define SDMEE_PyEr_Dict       -12002    /* PyDict_New() failed */
#define SDMEE_PyEr_Builtin    -12003    /* failed to set __builtins__ link to the built-in scope */
#define SDMEE_PyEr_Excpt      -12004    /* exception on PyRun_String */
#define SDMEE_PyEr_FinalEr    -12005    /*  error reported by GPL.BP Program PY_FINALIZE */
#define SDMEE_PyEr_NOF        -12006    /* could not open script file */


#endif