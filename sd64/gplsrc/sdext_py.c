/* SDEXT_PY.C
 * python integration for SD via SDEXT (op_sdext.c) BASIC function
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
 * to do- add STATUS() = 0 successful call, or  STATUS() = 1 unsuccessful call
 * 
 * START-HISTORY:
 * rev 0.9-2 Mar 25 mab add sdext_pyobj direct control of python dictionary object
 * rev 0.9.0 Jan 25 mab use install script created file sdext_python_inc.h to tell us where to find python headers
 * 11 Aug 2024 mab add PyErr_Print() to file and string script execution failure
 * 15 Jul 2024 MAB add SDME_PY.C 
 * 19 Jul 2024 mab remove Py_DECREF(pdict) at finalize, not necessary (at least samples I have seen dont do it)
 *   Also note, original idea was initialize - run script - finalize for each use (call from BASIC prog).
 *   This does not always appear to work, sometimes end up with double free or corruption (out) error on second call to 
 *   Py_Run_File.  Searching python forums seems to suggest only initialize and finalize once per SD session.
 *   Code already checks for python already initiailize, but how to handle Finalize?
 *   Could add test at SD close if Py_IsInitialized() then Py_FinalizeEx() ?? TBD
 * START-DESCRIPTION:
 *
 *
 * END-DESCRIPTION
 *
 * defined in keys.h
 * embedded python key codes
 * SD_PyInit        2000   initialize the python interpreter   
 * SD_PyFinal       2001  * Finalize the python interpreter   
 * SD_PyRunStr      2010  * Take the string in qmBasic variable VAL and run in python interpreter   
 * SD_PyRunFile     2011  * Take the file and path defined in qmBasic variable VAL and run in python interpreter   
 * SD_PyGetAtt      2100  * Return the (string) value of python attribute defined in qmBasic variable VAL   
 *
 * Embedded Python Error codes   *
 * defined in err.h
 * SD_PyEr_NotInit    -2001    * interperter not initiialized 
 * SD_PyEr_Dict       -2002    * PyDict_New() failed 
 * SD_PyEr_Builtin    -2003    * failed to set __builtins__ link to the built-in scope 
 * SD_PyEr_Excpt      -2004    * exception on PyRun_String 
 * SD_PyEr_FinalEr    -2005    *  error reported by GPL.BP Program PY_FINALIZE 
 * SD_PyEr_NOF        -2006    * could not open script file 
 

 * START-CODE
 */


#include "sd.h"
#include <linux/limits.h>
#include <libgen.h>            /* needed for basename function */ 
      

#include "sdext_python_inc.h"  /* NOTE! this file is created by the install script !!! */
#include "keys.h"

int PyDictCrte(char* dictname);
int PyDictClr(char* dictname );
int PyDictValSetS(char* dictname, char* key, char* value);
int PyDictValGetS(char* dictname, char* key);

int PyStrSet(char* strname, char* strvalue );
int PyStrGet(char* strname );

PyObject* lookup_dict_item(PyObject* dict, const char* key);
void obj_to_str(PyObject* pval);


/* python objects for embedded python, these need to hang around between calls! */

PyObject *global_dict, *main_module;  /* global PyObjects that must hang around between calls */

void sdext_py(int key, char* Arg, char* Arg2, char* Arg3 ){

  FILE *pyfd;         /* file descriptor for python script file */
  char *pyscriptname; /* script name from pyfd */
  char pyFilePath[PATH_MAX+1] = "";   /* max length a file path can be, defined in linux/limits.h */
  
  int myResult;

  PyObject *pval, *prun;

  char shutdown[] = "shutdown";
  char nullresult[] = "";
  
  process.status = 0;   /* setup status() value */
  myResult = 0;         /* init result response */
  /*Evaluate KEY */
  
  switch (key) {

    case SD_PyInit: /* Initialize python */
      if (!Py_IsInitialized()) {  /* only initialize if not already so */
        Py_Initialize();          /* There is no return value; it is a fatal error if the initialization fails. */
       /* "dictionaries that serve as namespaces for running code are generally required 
        to have a __builtins__ link to the built-in scope searched last for name lookups"
        from Programming Python 4th edition, Basic Embedding Techniques,
        Running Strings in Dictionaries       */
        if (Py_IsInitialized()) {     /* did initialization succeed? */
          // 1. Get the main module rem Borrowed reference
          main_module = PyImport_AddModule("__main__");  
          if (main_module == NULL) {
            PyErr_Print();
            myResult = SD_PyErr_MainMod;  /* could get main module bad news! */
          }else{
          // 2. Get the main (global) dictionary rem Borrowed reference
            global_dict = PyModule_GetDict(main_module);
            if (global_dict == NULL) {
              PyErr_Print();
              myResult = SD_PyErr_GlobDict;  /* could get __main__ dictionary bad news! */
            } 

          }


        }else{
          myResult =  SD_PyEr_NotInit; /* initialization failed */
        }
     
      } 
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SD_PyFinal: /* Finalize the python interpreter   */
      /* rem  global_dict, main_module are Borrowed Reference  */
                  
      if (Py_IsInitialized()) {  /* only finalize if previously initialized */
        myResult = Py_FinalizeEx();
      } else {
        myResult = 0; 
      }
      
      if (strcmp(Arg, shutdown) != 0){    /* test for shutdown, nothing to return */
        process.status = myResult;
        InitDescr(e_stack, INTEGER);
        (e_stack++)->data.value = (int32_t)myResult;
      }
      break;

    case SD_IsPyInit: /* Is python interpreter initialized?   */
                  
      myResult = Py_IsInitialized();

      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;



    case SD_PyRunStr:   /* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and run in python interpreter  */
                         
      if (Py_IsInitialized()) {
        prun = PyRun_String(Arg, Py_file_input, global_dict, global_dict);    /* run statements */
        if (prun == NULL){
        /* exception */
          myResult = SD_PyEr_Excpt;
          PyErr_Print();
        } else {
          Py_XDECREF(prun);
        }
      }else{
        myResult = SD_PyEr_NotInit;  
      }  
      
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break; 

    case SD_PyRunFile:   /* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and treat as a file to run in python interpreter   */
                            /* must move to pyFilePath[PATH_MAX]   */
	    snprintf(pyFilePath, PATH_MAX,"%s",Arg);

      if (Py_IsInitialized()) {
        pyfd = fopen(pyFilePath, "r");
        if (pyfd == NULL) {
          myResult = SD_PyEr_NOF;  /* failed to open file, could look at global variable errno ?*/
        }else{
          pyscriptname = basename(pyFilePath);  /* rem The basename() function may modify the string pointed to by path */
          prun = PyRun_File(pyfd,pyscriptname,Py_file_input, global_dict, global_dict);    /* run statements */
          if (prun == NULL){
          /* exception */
            myResult = SD_PyEr_Excpt;
            PyErr_Print();
          } else {
             Py_XDECREF(prun);
          }
          fclose(pyfd);
        }  
      }else{
        myResult = SD_PyEr_NotInit;  
      }  
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;      

    case SD_PyGetAtt:   /* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and treat as the python variable to return   */

         /* PyMapping_GetItemString - Fetches (indexes) a dictionary value by key
           "Besides allowing you to partition code string namespaces independent of any Python
            module files on the underlying system, this scheme provides a natural communication
            mechanism. Values that are stored in the new dictionary before code is run serve as
            inputs, and names assigned by the embedded code can later be fetched out of the dictionary
            to serve as code outputs." from Programming Python 4th edition, Basic Embedding Techniques,
             Running Strings in Dictionaries       */
       
      /* access the python dictionary entry for the object we are after */
      
      if (!Py_IsInitialized()) {
      /* we dont have a running python interpreter */
        process.status = SD_PyEr_NotInit;
        k_put_c_string(nullresult, e_stack);   /* sets descr as type string and place the python value into it */
        e_stack++;
        break;
      }
      pval =  PyMapping_GetItemString(global_dict, Arg);             /* get my result object */
      /* convet to string, note obj_to_str handles any errors   from PyMapping_GetItemString */
      obj_to_str(pval);
      e_stack++;
      break;
  }

  return;
}

/***************************************************************************************************************************/
/*  Python Objects Extension             
* 
* Some Notes:
* From Python Documentation https://docs.python.org/3/c-api/index.html
* Reference Counts:
* A safe approach is to always use the generic operations (functions whose name begins with PyObject_, PyNumber_, PySequence_ or PyMapping_). 
* These operations always create a new strong reference (i.e. increment the reference count) of the object they return.
* This leaves the caller with the responsibility to call Py_XDECREF() when they are done with the result; this soon becomes second nature.     
* So we favor Py_Mapping_ to Py_Dict_ function calls
*/
/***************************************************************************************************************************/

/***************************************************************************************************************************/
/*  Create Dictionary                                                                                              */
/***************************************************************************************************************************/
int PyDictCrte(char* dictname ){
  /* create new dictionary for use by SD */
  /* Arg is name of dictionary           */
 
  int myResult;
  PyObject* dict_lookup;
  PyObject* new_dict;     /* new dictionary we are creating */

  myResult = 0;

  /* does dicionary (or object with this name) already exist? */ 
  dict_lookup = lookup_dict_item(global_dict, dictname);
  if (dict_lookup == NULL) {
  /* does not exist, create it */
    new_dict = PyDict_New();
    if (new_dict == NULL) {
       // not created
      PyErr_Print();
      return SD_PyEr_Dict ;
    } else {
      // created, add it to the main_module name space
      myResult = PyMapping_SetItemString(global_dict, dictname, new_dict);
      if (myResult != 0){
        // failed to add to global dictionary namespace !?
        myResult =  SD_PyErr_NamSpcErr;
      }
      // free up ref  (global dict holds a ref to the new_dict, we should be safe to do this)
      Py_XDECREF(new_dict);
    }

  } else {
    // exists, this is an error
    myResult = SD_PyErr_DictExsts; 
    Py_XDECREF(dict_lookup);

  }

    return myResult;
  }


/***************************************************************************************************************************/
/*  Clear Dictionary                                                                                              */
/***************************************************************************************************************************/
int PyDictClr(char* dictname ){
  /* clear  dictionary key values, name remains in globals directory! */
  /* Note this appears to work the best with no memory listed as      */
  /* still reachable: 0 bytes in 0 blocks  using Valgrind             */
   
  PyObject* dict_lookup;
  
  /* does dicionary (or object with this name) exist? */ 
  dict_lookup = lookup_dict_item(global_dict, dictname);
  if (dict_lookup == NULL) {
    // does not exist!
    return SD_PyErr_ObjNOF;
  } else {
    
    // Is this object a dictionary?
    if (PyDict_Check(dict_lookup)) {
      // yes it is a dictionary, clear it's key : values
      PyDict_Clear(dict_lookup);
      Py_XDECREF(dict_lookup);
      return 0;  
    } else {
      // object was not a dictionary
      Py_XDECREF(dict_lookup);
      return SD_PyErr_NotDict;
    }

  }

}





/***************************************************************************************************************************/
/*  Set Dictionary String Value for key                                                                                               */
/***************************************************************************************************************************/  

int PyDictValSetS(char* dictname, char* key, char* value){
  /* set a dictionary key's value */  
  /**/

  int myResult;
  PyObject* dict_lookup;

  myResult = 0;

  /* does dicionary (or object with this name) exist? */ 
  dict_lookup = lookup_dict_item(global_dict, dictname);
  if (dict_lookup == NULL) {
    // does not exist!
    return SD_PyErr_ObjNOF;
  } else {
    
    // Is this object a dictionary?
    if (PyDict_Check(dict_lookup)) {
      // yes it is a dictionary

      PyObject *Pyvalue = PyUnicode_DecodeLatin1(value, (Py_ssize_t) strlen(value), "strict");
      if (Pyvalue == NULL){
        PyErr_Print();
        //Py_XDECREF(Pykey);
        Py_XDECREF(dict_lookup);
        return SD_PyErr_EnLatin;
      }  

      // Add the key-value pair to the dictionary
      
      //if (PyDict_SetItem(dict_lookup, Pykey, Pyvalue) != 0) {
      if (PyMapping_SetItemString(dict_lookup, key, Pyvalue) != 0) {
        // Failed to set dictionary key / value
        myResult = SD_PyErr_DictSet;
      }
      // Clean up
      //Py_XDECREF(Pykey);
      Py_XDECREF(Pyvalue);

    } else {
      // object was not a dictionary, report error
      myResult = SD_PyErr_NotDict;
    }

    Py_XDECREF(dict_lookup);
  }

  return myResult;
}

/***************************************************************************************************************************/
/*  Get Value as String for dictionary item key                                                                                      */
/* rem, value ends up in SD descriptor so we really only return a status code                                              */
/***************************************************************************************************************************/

int PyDictValGetS(char* dictname, char* key){
  /* get a dictionary key's value */  
  int myResult;
  PyObject* dict_lookup;

  myResult = 0;
  char nullresult[] = "";

  /* does dicionary (or object with this name) exist? */ 
  dict_lookup = lookup_dict_item(global_dict, dictname);
  if (dict_lookup == NULL) {
    // does not exist!
    k_put_c_string(nullresult, e_stack);   /* return empty string */
    return SD_PyErr_ObjNOF;
  } else {
    
    // Is this object a dictionary?
    if (PyDict_Check(dict_lookup)) {
      // yes it is a dictionary.
      // if key not found with PyMapping_GetItemString there seems to be 
      // a large amount of memory reported as: 
      // still reachable: xxxxx bytes in yyyy blocks  using Valgrind
      // theoretically this is memory waiting to be released as part of python finalize
      // but lets not take the chance.        
      // Create key and check it it exists in dict
      PyObject *Pykey = PyUnicode_DecodeLatin1(key, (Py_ssize_t) strlen(key), "strict");

      if (Pykey == NULL){
          PyErr_Print();
          Py_XDECREF(dict_lookup);
          k_put_c_string(nullresult, e_stack);   /* return empty string */
          return SD_PyErr_EnLatin;
      }
      if (PyDict_Contains(dict_lookup, Pykey)) {
        PyObject* value = PyMapping_GetItemString(dict_lookup, key);
        obj_to_str(value);
      } else {
      //  key is not found ??
      //  PyErr_Print(); // Print Python exception, if any
        k_put_c_string(nullresult, e_stack);   /* return empty string */
        myResult = SD_PyEr_Key;
      } 
      Py_XDECREF(Pykey);

    } else {
      // object was not a dictionary, report error
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      myResult = SD_PyErr_NotDict;
    }

    Py_XDECREF(dict_lookup);
  }

  return myResult;
}

/***************************************************************************************************************************/
/*  Create and or set String                                                                                               */
/***************************************************************************************************************************/
int PyStrSet(char* strname, char* strvalue ){
  /* Create and or set String  for use by SD */
  
  int myResult;
  PyObject* dict_lookup;
  PyObject* new_string;     /* new string we are creating */

  myResult = 0;

  /* create string object */
  new_string = PyUnicode_DecodeLatin1(strvalue, (Py_ssize_t) strlen(strvalue), "strict");
  if (new_string  == NULL) {
    // not created
   PyErr_Print();
   return SD_PyErr_EnLatin ;
 }

  /* does string object (or object with this name) already exist? */ 
  dict_lookup = lookup_dict_item(global_dict, strname);
  if (dict_lookup == NULL) {
  /* does not exist, add to global dict */
      myResult = PyMapping_SetItemString(global_dict, strname, new_string);
      if (myResult != 0){
        // failed to add to global dictionary namespace !?
        myResult =  SD_PyErr_NamSpcErr;
      }

  } else {
    // exists, is it a string object?
    if (PyUnicode_Check(dict_lookup)){
      // yes, update it
      myResult = PyMapping_SetItemString(global_dict, strname, new_string);
      if (myResult != 0){
        // failed to add to global dictionary namespace !?
        myResult =  SD_PyErr_NamSpcErr;
      }

    } else {
      // not a string
      myResult = SD_PyErr_NotStr; 
    }
    Py_XDECREF(dict_lookup);
  }
  // free up ref  (global dict holds a ref to the new_string, we should be safe to do this)
  Py_XDECREF(new_string);

  return myResult;
  }



/***************************************************************************************************************************/
/*  convert python object to string                                                                                        */
/*  this routine is responsible for ref count of passed python object pval                                                 */
/***************************************************************************************************************************/

void obj_to_str(PyObject* pval){
  /* pval is reference to obj
     this routine is responsible for ref count !!!*/
  PyObject *psval, *pbval;   
  char *pyResult;
  char nullresult[] = "";

  if (pval == NULL) {
    /* we most likely had a key error, clear it*/  
    PyErr_Clear();  
    /* most likely fetch of dictionary value was key error  */
    process.status = SD_PyEr_Key;
    k_put_c_string(nullresult, e_stack);   /* sets descr as type string and place the python value into it */
   
  } else {
  
    /* fetch of dictionary value succeeded, so what do we have here? */
    if (PyBytes_Check(pval)) {
        
      /* bytes object hopefully we encoded using latin, otherwise we will have issues with @FM @VM @SVM */
      pyResult = PyBytes_AsString(pval);   /* get the c string */
      k_put_c_string(pyResult, e_stack);   /* sets descr as type string and place the python value into it */
      
    }else{
        
      /* not a byte object */
      /* for now all non byte objects are converted to string then encoded as Latin, more work to be done here  */
      psval =  PyObject_Str(pval);              /* convert result object to string representation */
      
      if (psval == NULL) {
                                                /* error converting python object to string */
        process.status = SD_PyEr_ObToStr;
        k_put_c_string(nullresult, e_stack);    /* sets descr as type string and place the python value into it */
        Py_XDECREF(pval);
        return;      
      }
      
      /* encode our string object to bytes */
      pbval =  PyUnicode_AsLatin1String(psval); /* encode to bytes using Latin1 */
      
      if (pbval == NULL) {
                                              /* error encoding unicode python string to to Latin */
        process.status = SD_PyErr_UniToStr;
        k_put_c_string(nullresult, e_stack);   /* sets descr as type string and place the python value into it */
        Py_XDECREF(pval);
        Py_XDECREF(psval);
        return;
      }

      pyResult = PyBytes_AsString(pbval);  /* finally get the c string */
      k_put_c_string(pyResult, e_stack);   /* sets descr as type string and place the python value into it */
      Py_XDECREF(psval);
      Py_XDECREF(pbval);
    }

    Py_XDECREF(pval);

  }

  return;

}


/******************************************************************************************/
/* get dict value (python object) based on passed string key                              */
/* Rem this creates a strong ref and the caller is responsible to dec the ref count !!!!! */
/******************************************************************************************/
PyObject* lookup_dict_item(PyObject* dict, const char* key) {
  // Look up item using PyMapping_GetItemString
  // I repeat, this creates a strong ref and the caller is responsible to dec the ref count !!!!!

  PyObject* value = PyMapping_GetItemString(dict, key);
  
  // Handle the case where the key is not found
  if (value == NULL) {
      PyErr_Clear(); // Clear the exception set by PyMapping_GetItemString
      return NULL;
  }
  
  // Return the found value
  return value;
}
