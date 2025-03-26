/* OP_SDPYOBJ.C
 *
 * python integration for SD, BASIC function SD_PYOBJ
 * Copyright (c)2025 The SD Developers, All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
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
 * START-HISTORY:
 * rev 0.9-2 Mar 25 mab op_sd_pyobj
 * END-HISTORY
 *
 * START-DESCRIPTION:
 *
 * Opcodes:
 *    op_sdpyobj   SDPYOBJ   
 *    for basic function SD_PYOBJ(Function_id, ARG1, ARG2, ARG3)
 *    where:
 *    Function_Id = the integer value used to identify what c code / function is execute.
 *    Arg1,2,3     = string value passed to the c code / function. 
 *
 * END-DESCRIPTION
 *
 
 * START-CODE
 */


#include <linux/limits.h>

#include "sd.h"
#include "keys.h"

/* defined in sdext_py.c */
extern int PyDictCrte(char* dictname);
extern int PyDictClr(char* dictname );
extern int PyDictValSetS(char* dictname, char* key, char* value);
extern int PyDictValGetS(char* dictname, char* key);

Private void sdme_err_rsp(int errnbr);
Private void getarg(char* arg);
Private void freeArg(char* Arg);

/* ======================================================================
   op_sdpyobj()   op code for BASIC function SD_PYOBJ   
 
      Stack:
        +=============================++=============================+
        +       STACK On Entry        ++       STACK On Exit         +
        +=============================++=============================+
estack> +    next available descr     ++    next available descr     +
        +=============================++=============================+
        +  descriptor w/ integer      ++  Addr to descr for RTNVAL   +
        +      key value              ++                             +
        +=============================++=============================+
        + Addr to descriptor for Arg1 ++  
        +=============================++
        + Addr to descriptor for Arg2 ++   
        +=============================++
        + Addr to descriptor for Arg3++   
        +=============================++
		
	   patterned from op_ospath() in op_dio2.c */

void op_sdpyobj() {

  int16_t key;
  char* Arg1;    /* pointers to string buffers created to hold user passed data */
  char* Arg2;
  char* Arg3;

  int myResult;

  DESCRIPTOR* descr;
  
  /* set the process.status flag to  "successful"      */
  /* User can retrieve this status with the BASIC function STATUS()*/
  process.status = 0;
  myResult = 0;
  Arg1 = NULL;
  Arg2 = NULL;
  Arg3 = NULL;

  /* Get action key */
  descr = e_stack - 1;   /* e_stack - 1 points to key descriptor */
  GetInt(descr);
  key = (int16_t)(descr->data.value);
  k_pop(1);   /* after pop() e_stack - 1 points to descr which holds ARG1  */

  getarg(Arg1); /* get string Arg from descr and dismiss() */
  getarg(Arg2);
  getarg(Arg3);
  /* rem e_stack now points to descr for return value */

  /* look for sd_pyobj function key */
  switch (key) {
	case SD_PyDictCrte: 
      // Arg is name of dictionary to create
      myResult = PyDictCrte(Arg1);
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SD_PyDictClr: 
      // Arg is name of dictionary to Clear
      myResult = PyDictClr(Arg1);
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;


    case SD_PyDictVset:
    // Arg is name of dictionary 
    // Arg2 is key
    // Arg3 is value to set
      myResult = PyDictValSetS(Arg1, Arg2, Arg3 );
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SD_PyDictVget:
    // Arg is name of dictionary 
    // Arg2 is key
    // value will be placed in data descriptor by PyDictValGet
    /* rem value ends up as string and must be returned to sd with
       k_put_c_string(pyResult, e_stack);*/
      myResult = PyDictValGetS(Arg1, Arg2);    
      process.status = myResult;
      e_stack++;
      break;
  
    default:
      /* unknown key */
      sdme_err_rsp(SD_EXT_KEY_ERR);

  }
  
  /* release our arg Buffers  */
  freeArg(Arg1);
  freeArg(Arg2);
  freeArg(Arg3);

  return;
}


Private void getarg(char* arg){
/* get args from stack  */
  DESCRIPTOR* descr;
  STRING_CHUNK* str;
  int32_t myval_len;
  int32_t mybuf_sz;

  /* Get val string */
  descr = e_stack - 1;
  k_get_string(descr);
  str = descr->data.str.saddr;

  /* is there something there? */
  if (str == NULL){
	myval_len = 0;  
    mybuf_sz = 1;              /* room for string terminator */
  }else{
	myval_len =  str->string_len;
    mybuf_sz  =  myval_len+1; /* room for string terminator */
  }
  
  /* allocate space for arg val string */
  arg = malloc(mybuf_sz * sizeof(char));
  if (arg == NULL){
    /* so here is a question, what to do if we cannot allocate memory?
	 We will end execution of program and attempt to report error  */
    k_error(sysmsg(10005));   /* Insufficient memory for buffer allocation */
	/* We never come back from k_error */
  }

  /* move the passed argument string to our buffer */
  if (myval_len == 0){
    arg[0] = '\0';
  } else {
   /* rem string length returned by k_get_c_string excludes terminator in count!*/ 
    myval_len = k_get_c_string(descr, arg, myval_len);
  }
  
  k_dismiss();   /* done with passed arg  descriptor */
                 /* Things to note                   */  
                 /* we use dismiss() instead of pop() because this is a string */ 
                 /* which may be made up of a linked list of string blocks     */
                 /* Using pop would not free the sting blocks                  */
                 /* After dismiss() e_stack  points to next descr on stack,    */
                 /* either the descr for next Arg or the one to receive RTNVAL */
  return;
}

Private void freeArg(char* Arg){
    if (Arg != NULL){
        free(Arg);    
        Arg = NULL;
    } 
}

/* generic error return with null response, setting process.status */
Private void sdme_err_rsp(int errNbr){
  char EmptyResp[1] = {'\0'}; /*  empty return message  */
  k_put_c_string(EmptyResp, e_stack); /* sets descr as type string, empty */
  e_stack++;
  process.status = errNbr;

}
