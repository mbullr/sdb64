/* SDME_PY.C
 * python integration for ScarletDME via SDME.EXT (op_sdme_ext.c) BASIC function
 *
 * to do- add STATUS() = 0 successful call, or  STATUS() = 1 unsuccessful call
 * 
 * START-HISTORY:
 * 15 Jul 2024 MAB add SDME_PY.C 
 * START-DESCRIPTION:
 *
 *
 * END-DESCRIPTION
 *
 * defined in SDME_EXT.h
 * embedded python key codes
 * SDMEE_PyInit        2000   initialize the python interpreter   
 * SDMEE_PyFinal       2001  * Finalize the python interpreter   
 * SDMEE_PyRunStr      2010  * Take the string in qmBasic variable VAL and run in python interpreter   
 * SDMEE_PyRunFile     2011  * Take the file and path defined in qmBasic variable VAL and run in python interpreter   
 * SDMEE_PyGetAtt      2100  * Return the (string) value of python attribute defined in qmBasic variable VAL   
 *
 * Embedded Python Error codes   *
 * SDMEE_PyEr_String     "<@@!>"  * text string preamble to signify string response from Eng_PyGetAtt is an error msg 
 * SDMEE_PyEr_NotInit    -2001    * interperter not initiialized 
 * SDMEE_PyEr_Dict       -2002    * PyDict_New() failed 
 * SDMEE_PyEr_Builtin    -2003    * failed to set __builtins__ link to the built-in scope 
 * SDMEE_PyEr_Excpt      -2004    * exception on PyRun_String 
 * SDMEE_PyEr_FinalEr    -2005    *  error reported by GPL.BP Program PY_FINALIZE 
 * SDMEE_PyEr_NOF        -2006    * could not open script file 
 

 * START-CODE
 */

#include "sd.h"
#include <linux/limits.h>
#include <libgen.h>            /* needed for basename function */         
#include <python3.12/Python.h>

#include "SDME_EXT.h"


/* python objects for embedded python, these need to hang around between calls! */
PyObject *pval, *psval, *pbval, *prun;
PyObject *pdict; 


void sdme_py(int key, char* Arg){

  char *pyResult; 
  FILE *pyfd;         /* file descriptor for python script file */
  char *pyscriptname; /* script name from pyfd */
  char pyFilePath[PATH_MAX+1] init("");   /* max length a file path can be, defined in linux/limits.h */

  int myResult;
  char    CmdResp[SDMEE_ErrMsg_LEN +1] init("");  /*  buffer for error message  */
  
  
  /*Evaluate KEY */
  
  switch (key) {

    case SDMEE_PyInit: /* Initialize python */

      if (!Py_IsInitialized()) {  /* only initialize if not already so */
        Py_Initialize();          /* There is no return value; it is a fatal error if the initialization fails. */
       /* "dictionaries that serve as namespaces for running code are generally required 
        to have a __builtins__ link to the built-in scope searched last for name lookups"
        from Programming Python 4th edition, Basic Embedding Techniques,
        Running Strings in Dictionaries       */
        if (Py_IsInitialized()) {     /* did initialization succeed? */
          pdict = PyDict_New(); /* return a new empty dictionary, or NULL on failure, New reference. */
          if (pdict == NULL){
            myResult = SDMEE_PyEr_Dict;  /* could not create the dict object??*/
          }else{
            myResult =  PyDict_SetItemString(pdict, "__builtins__", PyEval_GetBuiltins());
            if (myResult < 0){
              myResult =  SDMEE_PyEr_Builtin;  /* failed to set __builtins__ link to the built-in scope */
            }
          }
        }else{
          myResult =  SDMEE_PyEr_NotInit; /* initialization failed */
        }
      
      } 
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SDMEE_PyFinal: /* Finalize the python interpreter   */
                  
      if (Py_IsInitialized()) {  /* only finalize if previously initialized */
        if (pdict!=NULL){        /* should never be NULL here, but never say never */
           Py_DECREF(pdict);        /* done with pdict */
        }
        myResult = Py_FinalizeEx();
      } else {
        myResult = 0; 
      }
 
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SDMEE_PyRunStr:   /* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and run in python interpreter  */
                         
      myResult = 0;
      if (Py_IsInitialized()) {
        prun = PyRun_String(Arg, Py_file_input, pdict, pdict);    /* run statements */
        if (prun == NULL){
        /* exception */
          myResult = SDMEE_PyEr_Excpt;
        } else {
          Py_DECREF(prun);
        }
      }else{
        myResult = SDMEE_PyEr_NotInit;  
      }  

      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break; 

    case SDMEE_PyRunFile:   /* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and treat as a file to run in python interpreter   */
                            /* must move to pyFilePath[PATH_MAX]   */
      myResult = 0;
	    snprintf(pyFilePath, PATH_MAX,"%s",Arg);

      if (Py_IsInitialized()) {
        pyfd = fopen(pyFilePath, "r");
        if (pyfd == NULL) {
          myResult = SDMEE_PyEr_NOF;  /* failed to open file, could look at global variable errno ?*/
        }else{
          pyscriptname = basename(pyFilePath);  /* rem The basename() function may modify the string pointed to by path */
          prun = PyRun_File(pyfd,pyscriptname,Py_file_input, pdict, pdict);    /* run statements */
          if (prun == NULL){
          /* exception */
            myResult = SDMEE_PyEr_Excpt;
          } else {
             Py_DECREF(prun);
          }
          fclose(pyfd);
        }  
      }else{
        myResult = SDMEE_PyEr_NotInit;  
      }  

      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;      

    case SDMEE_PyGetAtt:   /* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and treat as the python variable to return   */

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
        myResult = snprintf(CmdResp, SDMEE_ErrMsg_LEN,SDMEE_PyEr_String " Interpreter not initialized.");
        k_put_c_string(CmdResp, e_stack);   /* sets descr as type string and place the python value into it */
        e_stack++;
        break;
      }
      pval =  PyMapping_GetItemString(pdict, Arg);             /* get my result string */
      if (pval == NULL) {
      /* fetch of dictionary value failed for some reason */
        myResult = snprintf(CmdResp, SDMEE_ErrMsg_LEN,SDMEE_PyEr_String " Fetch of dict key %s failed.",  Arg);
        k_put_c_string(CmdResp, e_stack);   /* sets descr as type string and place the python value into it */
        e_stack++;
        break;
      } 
      
      /* fetch of dictionary value succeeded, so what do we have here? */
      if (PyBytes_Check(pval)) {
        /* bytes object hopefully we encoded using latin, otherwise we will have issues with @FM @VM @SVM */
        pyResult = PyBytes_AsString(pval);   /* get the c string */
        
      }else{
        /* not a byte object */
        /* for now all non byte objects are converted to string then encoded as Latin, more work to be done here  */
        psval =  PyObject_Str(pval);              /* convert result object to string representation */
        if (psval == NULL) {
                                                  /* error converting python object to string */
          myResult = snprintf(CmdResp, SDMEE_ErrMsg_LEN,SDMEE_PyEr_String " converting python object to string %s",  Arg);
          k_put_c_string(CmdResp, e_stack);    /* sets descr as type string and place the python value into it */
          Py_DECREF(pval);
          e_stack++;
          break;      
        }
        /* encode our string object to bytes */
        pbval =  PyUnicode_AsLatin1String(psval); /* encode to bytes using Latin1 */
        if (pbval == NULL) {
                                                 /* error encoding unicode python string to to Latin */
          myResult = snprintf(CmdResp, SDMEE_ErrMsg_LEN,SDMEE_PyEr_String " encoding unicode python string to to Latin %s",  Arg);
          k_put_c_string(CmdResp, e_stack);   /* sets descr as type string and place the python value into it */
          Py_DECREF(pval);
          Py_DECREF(psval);
          e_stack++;
          break;
        }
 
        pyResult = PyBytes_AsString(pbval);        /* finally get the c string */
        Py_DECREF(psval);
        Py_DECREF(pbval);
      }
      k_put_c_string(pyResult, e_stack);   /* sets descr as type string and place the python value into it */
      Py_DECREF(pval);
      e_stack++;
      break;  

  }

  return;
}