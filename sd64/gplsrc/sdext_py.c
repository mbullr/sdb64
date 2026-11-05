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
 * rev 1.0-3 mab move #include "sdext_python_inc.h"  to first include
 *              the way python finalize was designed a user could cause a seqfault
 *              added cooperative scheuduler between VM and python 
 *              added sd_py GUI routines
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


#include "sdext_py.h"
#include <fcntl.h>
#include <linux/limits.h>
#include <libgen.h>            /* needed for basename function */ 
#include <pthread.h>
#include <unistd.h>
#include "sd.h"
#include "keys.h"


/* python objects for embedded python, these need to hang around between calls! */

char PyGuiModule[PyGuiModuleSz] = "FreeSimpleGUI";
PyObject *global_dict, *main_module;  /* global PyObjects that must hang around between calls */

/* structures and functions for cooperative scheduler */


static int sd_event_queue_init(void);
static void sd_event_queue_shutdown(void);
static int sd_event_enqueue(const char *name, const char *payload);
static SD_EVENT *sd_event_dequeue(void);
static void sd_event_drain_wakeup(void);
static int sd_py_gui_step(void);
static int sd_py_poll(void);
static PyObject *sd_post_event(PyObject *self, PyObject *args);

static struct {
  SD_EVENT *head;
  SD_EVENT *tail;
  int count;
  int read_fd;
  int write_fd;
  bool shutting_down;
  pthread_mutex_t mutex;
} sd_event_queue = {
  NULL, NULL, 0, -1, -1, FALSE, PTHREAD_MUTEX_INITIALIZER
};

static PyMethodDef sd_methods[] = {
  {"post_event", sd_post_event, METH_VARARGS,
   "Queue an event for the SD BASIC program."},
  {NULL, NULL, 0, NULL}
};

static struct PyModuleDef sd_module = {
  PyModuleDef_HEAD_INIT, "sd", "SD Python integration", -1,
  sd_methods, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_sd(void)
{
  return PyModule_Create(&sd_module);
}

volatile bool python_event_pending = FALSE;
bool sd_module_registered = FALSE;

// return_status return a null value in the sd string descriptor pointed to by estack and inc the stack
 #define return_status(stat){\
   process.status = stat;\
   InitDescr(e_stack, INTEGER);\
   (e_stack++)->data.value = (int32_t)process.status;\
 }

/* **************************************************************************************************************** */
/*                                                                                                                  */
/* All important function that calls the function that implements the python function                               */
/* key - python function to call , defined in keys.h                                                                */
/* Arg   thru  Arg7   vary depending on called function                                                             */
/* somewhat defined in SYSCOM/SDPYFUNC.H                                                                            */
/* **************************************************************************************************************** */
int sdext_SD_PyInit(){
  int myResult;
  myResult = 0;
  
  if (!Py_IsInitialized()) {  /* only initialize if not already so */
    myResult = sd_event_queue_init();
    if (myResult != 0){
      return_status(myResult);
      return myResult;
    }

    if (!sd_module_registered) {
      if (PyImport_AppendInittab("sd", &PyInit_sd) == -1) {
        myResult = SD_PyErr_MainMod;
        return_status(myResult);
        return myResult;
      }
      sd_module_registered = TRUE;
    }

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
        } else {
          PyObject *sd_module = PyImport_ImportModule("sd");
          if (sd_module == NULL) {
            PyErr_Print();
            myResult = SD_PyErr_MainMod;
          } else {
            Py_DECREF(sd_module);
          }
        }

      }


    }else{
      myResult =  SD_PyEr_NotInit; /* initialization failed */
    }
 
  } 
  return_status(myResult);
  return myResult;
}


/* finalize python interpreter */
/* only do this if we are truely done for this sd session */
int sdext_SD_PyFinal(){
  int myResult;
  myResult = 0;
  /* rem  global_dict, main_module are Borrowed Reference, no decref reqd  */
  myResult = sdext_py_finalize();
      
  return_status(myResult);
  return myResult;
}

/* Is python interpreter initialized?   */
int sdext_SD_IsPyInit(){ 
  int myResult;
  myResult = 0;
  myResult = Py_IsInitialized();

  return_status(myResult);
  return myResult;
}

/* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and run in python interpreter  */
int sdext_SD_PyRunStr(const char* Arg){
  int myResult;
  myResult = 0;
  if (Py_IsInitialized()) {
    PyObject *prun = PyRun_String(Arg, Py_file_input, global_dict, global_dict);    /* run statements */
    if (prun == NULL){
    /* exception */
      myResult = SD_PyEr_Excpt;
      PyErr_Print();
    } else {
      Py_DECREF(prun);
    }
  }else{
    myResult = SD_PyEr_NotInit;  
  }  
  
  return_status(myResult);
  return myResult;
} 


/* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and treat as a file to run in python interpreter   */
/* must move to pyFilePath[PATH_MAX]   */
int sdext_SD_PyRunFile(const char* Arg){
  int myResult;
  myResult = 0;
  FILE *pyfd;         /* file descriptor for python script file */
  char *pyscriptname; /* script name from pyfd */
  char pyFilePath[PATH_MAX+1] = "";   /* max length a file path can be, defined in linux/limits.h */
  
  snprintf(pyFilePath, PATH_MAX,"%s",Arg);
  
  if (Py_IsInitialized()) {
    pyfd = fopen(pyFilePath, "r");
    if (pyfd == NULL) {
      myResult = SD_PyEr_NOF;  /* failed to open file, could look at global variable errno ?*/
    }else{
      pyscriptname = basename(pyFilePath);  /* rem The basename() function may modify the string pointed to by path */
      PyObject *prun = PyRun_File(pyfd,pyscriptname,Py_file_input, global_dict, global_dict);    /* run statements */
      if (prun == NULL){
      /* exception */
        myResult = SD_PyEr_Excpt;
        PyErr_Print();
      } else {
         Py_DECREF(prun);
      }
      fclose(pyfd);
    }  
  }else{
    myResult = SD_PyEr_NotInit;  
  }  
  return_status(myResult);
  return myResult;
}

/* Take the string in Arg passed from op_sdme_ext (from SDME.EXT Arg value) and treat as the python variable to return   */
int sdext_SD_PyGetAtt(const char* Arg){
  PyObject *pval;
  char nullresult[] = "";
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
  } else {
  pval =  PyMapping_GetItemString(global_dict, Arg);             /* get my result object */
  /* convet to string, note obj_to_str handles any errors   from PyMapping_GetItemString and ref cnt for pval */
  obj_to_str(pval);
  e_stack++;
  }
  return process.status;
}

/* SD_PyGuiStep  Calls the python function "gui_step" found in the python script being run to enable build in GUI  */
/* This function is called in the Cooperative Scheduler loop of the BASIC program controlling the GUI window       */
/* see sd64/Documentation/SD GUI Python Cooperative Schedule.txt for more information                              */
int sdext_SD_PyGuiStep(){
  int myResult;
  myResult = 0;
  if (Py_IsInitialized()){
    myResult = sd_py_gui_step();
  }else{
    myResult = SD_PyEr_NotInit;
  }
  return_status(myResult);
  return myResult;
}


/* SD_PyPoll consumes the queued Python event                                                                      */
/* see sd64/Documentation/SD GUI Python Cooperative Schedule.txt for more information                              */
int sdext_SD_PyPoll(){
  char nullresult[] = "";
  if (Py_IsInitialized()){
    process.status = sd_py_poll();
  }else{
    process.status = SD_PyEr_NotInit;
    k_put_c_string(nullresult, e_stack);
    e_stack++;
  }
  return process.status;
}


/* SD_PyGuiWinUpdate update PyGui Window's Gui Objects value                            */
/* mimics python statement window['YOUR_ELEMENT_KEY'].update(new_value)                 */
/* Arg: "My Window", Arg2: "my_key", Arg3: "new text"                                   */
int sdext_SD_PyGuiWinUpdate(const char *Arg, const char *Arg2, const char *Arg3){ 
  int myResult;
  myResult = 0;
  
  if (Py_IsInitialized()){
    // FreeSimpleGUI must have been imported in script as sg 
    PyObject *mod = PyImport_ImportModule(PyGuiModule);
    if (mod != NULL) {
      // Arg: "My Window", Arg2: "my_key", Arg3: "new text"
      myResult = sd_py_update_key_value_in_window(Arg, Arg2, Arg3);
      Py_DECREF(mod);
    } else {
      fprintf(stderr, "%s Module not found, is 'Import %s as sg' in your script?:\n",PyGuiModule,PyGuiModule);
      PyErr_Print();
      myResult = SD_PyErr_NoFreeGui;
    }
  } else {
    myResult = SD_PyEr_NotInit;
  }
  return_status(myResult);
  return myResult;
}


/* SD_PyGuiWinUpdateS  update multi PyGui Window's Gui Object values                                   */
/* Arg: "My Window", Arg2: vm list of keys, Arg3: vm list of values                                    */
int sdext_SD_PyGuiWinUpdateS(const char *Arg, const char *Arg2, const char *Arg3){ 
  int myResult;
  myResult = 0;

  if (Py_IsInitialized()){
    // FreeSimpleGUI must have been imported in script as sg 
    PyObject *mod = PyImport_ImportModule(PyGuiModule);
    if (mod != NULL) {
      // Arg: "My Window", Arg2: vm list of keys, Arg3: vm list of values
      myResult = sd_py_update_multi_key_value_in_window(Arg, Arg2, Arg3);
      Py_DECREF(mod);
    } else {
      fprintf(stderr, "%s Module not found, is 'Import %s as sg' in your script?:\n",PyGuiModule,PyGuiModule);
      PyErr_Print();
      myResult = SD_PyErr_NoFreeGui;
    }
  } else {
    myResult = SD_PyEr_NotInit;
  }
  return_status(myResult);
  return myResult;
}




/* SD_PyGuiWinUpdValues   update PyGui Window's Gui Object's values parameter                             */
/*                        necessary for dealing with objects with the Values attribute (Combo / Tables)   */
int sdext_SD_PyGuiWinUpdValues(const char *Arg, const char *Arg2, const char *Arg3){ 
  int myResult;
  myResult = 0;
  
  if (Py_IsInitialized()){
    // FreeSimpleGUI must have been imported in script as sg 
    PyObject *mod = PyImport_ImportModule(PyGuiModule);
    if (mod != NULL) {
      // Arg: "My Window", Arg2: "my_key", Arg3: "list object name"
      //sd_py_update_window_values_list(PyObject* module, const char *window_name, const char *key_name, const char *listname)
      myResult = sd_py_update_window_values_list(Arg, Arg2, Arg3);
      Py_DECREF(mod);
    } else {
      fprintf(stderr, "%s Module not found, is 'Import %s as sg' in your script?:\n",PyGuiModule,PyGuiModule);
      PyErr_Print();
      myResult = SD_PyErr_NoFreeGui;
    }
  } else {
    myResult = SD_PyEr_NotInit;
  }
  return_status(myResult);
  return myResult;
} 

/*     case SD_PyGuiTableSet                                         */
/* populate existing list with table data from dyn array             */
/* Arg: table list name, Arg2: dyn array for table data              */
int sdext_SD_PyGuiTableSet(const char *Arg, const char *Arg2){ 
  int myResult;
  myResult = 0;

  if (Py_IsInitialized()){
    // Arg: table list name, Arg2: dyn array for table data
    myResult = sd_py_table_set(Arg, Arg2);

  } else {
    myResult = SD_PyEr_NotInit;
  }
  return_status(myResult);
  return myResult;
}


/* SD_PyGuiTableGet get (read) Gui table list data into dyn array                             */
/* populates the sd data object pointed to by e_stack with k_put_c_string(c string, e_stack)  */
/* dyn array ends up with rows delimeted by @vm, columns by @svm                              */
/* for this to work the python script must move the table data from the Table object into a   */
/* the local list Arg                                                                         */
/* The usefulness of this function has been reduced by the ability to call functions          */
/*  SD_PyCallFunction and SD_PyCallFunctionS                                                  */

int sdext_SD_PyGuiTableGet(const char *Arg){  
  char nullresult[] = "";
  /* get (read) Gui table list data in dyn array */
  
  if (!Py_IsInitialized()) {
  /* we dont have a running python interpreter */
    process.status = SD_PyEr_NotInit;
    k_put_c_string(nullresult, e_stack);   /* sets descr as type string and place empty string value into it */
    e_stack++;
  } else {
    sd_py_table_get(Arg);           
    // rem sd_py_table_get:
    // populates the sd data object pointed to by e_stack with k_put_c_string(c string, e_stack)  
    // inc the stack pointer e_stack++
    // sets process.status
  }
return process.status;
}


/* SD_PyCallFunction  call function pre defined in loaded (sdext_SD_PyRunFile) python script                         */
/* Arg: "Function Name", Arg2: "result_name", Arg3: "Function Arg1", Arg4: "Function Arg2, Arg5: "Function Arg3"     */
/*                                            Arg6: "Function Arg4", Arg7: "Function Arg5"                           */
int sdext_SD_PyCallFunction(const char *Arg, const char *Arg2, const char *Arg3, const char *Arg4, const char *Arg5, const char *Arg6, const char *Arg7){ 
  int myResult;
  myResult = 0;
  if (Py_IsInitialized()){
    /* rem  sd_py_call_function performs return_status(myResult)            */
    myResult = sd_py_call_function(Arg, Arg2, Arg3, Arg4, Arg5, Arg6, Arg7);
  }else{ 
    myResult = SD_PyEr_NotInit; 
    return_status(myResult);
  }
  return myResult;
}
      
/* SD_PyCallFunctionS  call function pre defined in loaded (sdext_SD_PyRunFile) python script                                             */
/* Arg: "Function Name", Arg2: "Function Arg1", Arg3: "Function Arg2, Arg4: "Function Arg3, Arg5: "Function Arg4", Arg6: "Function Arg5"  */
/* This version returns the python function result as a string to the vm                                                                  */
int sdext_SD_PyCallFunctionS(const char *Arg, const char *Arg2, const char *Arg3, const char *Arg4, const char *Arg5, const char *Arg6){ 
  char nullresult[] = "";
  if (Py_IsInitialized()){
    // Arg: "Function Name", Arg2: "Function Arg", Arg3: "Function Arg", Arg4: "Function Arg, Arg5: "Function Arg"
    // version returns cstring to vm
    process.status = sd_py_call_functionS(Arg, Arg2, Arg3, Arg4, Arg5, Arg6);
  }else{
    process.status = SD_PyEr_NotInit;
    k_put_c_string(nullresult, e_stack);   /* sets descr as type string and place empty string value into it */
    e_stack++; 
  }  
  return process.status;  
}

/* shut down python */

int sdext_py_finalize(void){
      if (Py_IsInitialized()) {  /* only finalize if previously initialized */
        sd_event_queue_shutdown();
        return Py_FinalizeEx();
      } else {
        return SD_PyEr_NotInit; 
      }
}

/* **************************************************************************************************************** */
/* Function to call python module function                                                                          */
/* Arg: "Function Name", result_name  "Python object name to recieve function results                               */
/*                       Arg1 -5 : "Function Arg"                                                                   */
/* REM we assume function is local to script OR imported by the prior execute script                                */
/* Either way the assumption is these are names of objects  available in __main__                                   */
/* Routine handles varible arg count by assuming first NULL arg segnifies the end of passed arguments AND sets      */
/*  the number of arguments to pass to the python function, kind of a kludge                                        */
/* **************************************************************************************************************** */

int sd_py_call_function(const char *function_name, const char *result_name, const char *Arg1, const char *Arg2, const char *Arg3,const char *Arg4, const char *Arg5){
 
  int argcnt;
  PyObject *pArg1, *pArg2, *pArg3, *pArg4, *pArg5, *pResult_Name, *pResult_Value;

  PyObject *pMainModule = PyImport_ImportModule("__main__");
  if (pMainModule == NULL) {
      PyErr_Print();
      return_status(SD_PyErr_MainScope);
      return SD_PyErr_MainScope ;
  }

  PyObject *pFunction = PyObject_GetAttrString(pMainModule,function_name);
  Py_DECREF(pMainModule);
  if (pFunction == NULL) {
      fprintf(stderr, "Function %s not found\n\r",function_name);
      PyErr_Print();
      return_status(SD_PyErr_ObjNOF);
      return SD_PyErr_ObjNOF ;
      
  }


  if (PyCallable_Check(pFunction)) {
    // get result_name
    if (result_name != NULL){
      pResult_Name = lookup_dict_item(global_dict, result_name);
      if (pResult_Name == NULL) {
      /* obj dpResultoes not exist, report error */
        Py_DECREF(pFunction);
        return_status(SD_PyErr_ObjNOF);
        return SD_PyErr_ObjNOF ;
       }
    }

    // figure out number of args
    // I know this is ugly but it is what I am doing
    argcnt = 0;   // default to no args
    if (Arg1 != NULL){
      argcnt = 1;
      pArg1 = lookup_dict_item(global_dict, Arg1);
      if (pArg1 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pResult_Name);
        return_status(SD_PyErr_ObjNOF);
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg2 != NULL){
      argcnt = 2;
      pArg2 = lookup_dict_item(global_dict, Arg2);
      if (pArg2 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pResult_Name);
        Py_DECREF(pArg1);
        return_status(SD_PyErr_ObjNOF);
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg3 != NULL){
      argcnt = 3;
      pArg3 = lookup_dict_item(global_dict, Arg3);
      if (pArg3 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pResult_Name);
        Py_DECREF(pArg1);
        Py_DECREF(pArg2);
        return_status(SD_PyErr_ObjNOF);
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg4 != NULL){
      argcnt = 4;
      pArg4 = lookup_dict_item(global_dict, Arg4);
      if (pArg4 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pResult_Name);
        Py_DECREF(pArg1);
        Py_DECREF(pArg2);
        Py_DECREF(pArg3);
        return_status(SD_PyErr_ObjNOF);
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg5 != NULL){
      argcnt = 5;
      pArg5 = lookup_dict_item(global_dict, Arg5);
      if (pArg5 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pResult_Name);
        Py_DECREF(pArg1);
        Py_DECREF(pArg2);
        Py_DECREF(pArg3);
        Py_DECREF(pArg4);
        return_status(SD_PyErr_ObjNOF);
        return SD_PyErr_ObjNOF ;
       }
    }
    
    // Execute the Python function
    // Pass the arguments followed by NULL to signal the end of arguments
    switch (argcnt){
        case 0:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, NULL);
           break;
        case 1:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1,NULL);
           Py_DECREF(pArg1);
           break;
        case 2:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           break;
        case 3:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, pArg3, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           Py_DECREF(pArg3);
           break;  
        case 4:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, pArg3, pArg4, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           Py_DECREF(pArg3);
           Py_DECREF(pArg4);
           break; 
        case 5:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, pArg3, pArg4, pArg5, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           Py_DECREF(pArg3);
           Py_DECREF(pArg4);
           Py_DECREF(pArg5);
           break;

    }
    Py_DECREF(pFunction);
    
    if (pResult_Value != NULL) {
      // replace exiting object with results of function call
      int result = PyDict_SetItemString(global_dict, result_name, pResult_Value);
      if (result < 0){
        PyErr_Print();
        fprintf(stderr, "Failed to update %s \n\r",result_name);
        return_status(SD_PyErr_DictSet);
      } else {
        return_status(0);
      }

      Py_DECREF(pResult_Value);


    } else {
        // Handle Python exceptions during execution
        PyErr_Print();
        fprintf(stderr, "Python function call failed\n\r");
        return_status(SD_PyErr_FuncFail);
    }
    
    Py_DECREF(pResult_Name);

  } else {
      fprintf(stderr, "Function %s is not callable\n\r",function_name);
      return_status(SD_PyErr_FuncFail);
  }

  return process.status;
}


/* **************************************************************************************************************** */
/* Function to call python module function, returns cstring into VM result                                          */
/* Arg: "Function Name",   Arg1 -5 : "Function Arg"                                                                 */
/* REM we assume function is local to script OR imported by the prior execute script                                */
/* Either way the assumption is these are names of objects  available in __main__                                   */
/* Routine handles varible arg count by assuming first NULL arg segnifies the end of passed arguments AND sets      */
/*  the number of arguments to pass to the python function, kind of a kludge                                        */
/* **************************************************************************************************************** */

int sd_py_call_functionS(const char *function_name, const char *Arg1, const char *Arg2, const char *Arg3,const char *Arg4, const char *Arg5){
 
  char nullresult[] = "";
  
  // rtnNull return a null value in the sd string descriptor pointed to by estack and inc the stack
  #define rtnNull(){\
    k_put_c_string(nullresult, e_stack);\
    e_stack++;\
  }

  int argcnt;
  PyObject *pArg1, *pArg2, *pArg3, *pArg4, *pArg5, *pResult_Value;

  PyObject *pMainModule = PyImport_ImportModule("__main__");
  if (pMainModule == NULL) {
      PyErr_Print();
      rtnNull();
      return SD_PyErr_MainScope ;
  }

  PyObject *pFunction = PyObject_GetAttrString(pMainModule,function_name);
  Py_DECREF(pMainModule);
  if (pFunction == NULL) {
      fprintf(stderr, "Function %s not found\n\r",function_name);
      PyErr_Print();
      rtnNull();
      return SD_PyErr_ObjNOF ;
      
  }


  if (PyCallable_Check(pFunction)) {
 
    // figure out number of args
    // I know this is ugly but it is what I am doing
    argcnt = 0;   // default to no args
    if (Arg1 != NULL){
      argcnt = 1;
      pArg1 = lookup_dict_item(global_dict, Arg1);
      if (pArg1 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        rtnNull();
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg2 != NULL){
      argcnt = 2;
      pArg2 = lookup_dict_item(global_dict, Arg2);
      if (pArg2 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pArg1);
        rtnNull();
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg3 != NULL){
      argcnt = 3;
      pArg3 = lookup_dict_item(global_dict, Arg3);
      if (pArg3 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pArg1);
        Py_DECREF(pArg2);
        rtnNull();
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg4 != NULL){
      argcnt = 4;
      pArg4 = lookup_dict_item(global_dict, Arg4);
      if (pArg4 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pArg1);
        Py_DECREF(pArg2);
        Py_DECREF(pArg3);
        rtnNull();
        return SD_PyErr_ObjNOF ;
       }
    }
    if (Arg5 != NULL){
      argcnt = 5;
      pArg5 = lookup_dict_item(global_dict, Arg5);
      if (pArg5 == NULL) {
      /* obj does not exist, report error */
        Py_DECREF(pFunction);
        Py_DECREF(pArg1);
        Py_DECREF(pArg2);
        Py_DECREF(pArg3);
        Py_DECREF(pArg4);
        rtnNull();
        return SD_PyErr_ObjNOF ;
       }
    }
    
    // Execute the Python function
    // Pass the arguments followed by NULL to signal the end of arguments
    switch (argcnt){
        case 0:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, NULL);
           break;
        case 1:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1,NULL);
           Py_DECREF(pArg1);
           break;
        case 2:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           break;
        case 3:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, pArg3, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           Py_DECREF(pArg3);
           break;  
        case 4:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, pArg3, pArg4, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           Py_DECREF(pArg3);
           Py_DECREF(pArg4);
           break; 
        case 5:
           pResult_Value = PyObject_CallFunctionObjArgs(pFunction, pArg1, pArg2, pArg3, pArg4, pArg5, NULL);
           Py_DECREF(pArg1);
           Py_DECREF(pArg2);
           Py_DECREF(pArg3);
           Py_DECREF(pArg4);
           Py_DECREF(pArg5);
           break;

    }
    Py_DECREF(pFunction);
    
    if (pResult_Value != NULL) {
      // return result as string to VM
      // rem obj_to_str handles PResult_Value ref count
      obj_to_str(pResult_Value);
      e_stack++;
      return 0;


    } else {
        // Handle Python exceptions during execution
        PyErr_Print();
        fprintf(stderr, "Python function call failed\n\r");
        rtnNull();
        return SD_PyErr_FuncFail;
    }
    
   
  } else {
      fprintf(stderr, "Function %s is not callable\n\r",function_name);
      rtnNull();
      return SD_PyErr_FuncFail;
  }
}


/* **************************************************************************************************************** */
/* Functions for cooperative scheduler                                                                              */
/*  See ../Documentation/SD GUI Python Cooperative Schedule.txt for more information                                */
/* **************************************************************************************************************** */
int sd_python_event_fd(void)
{
  return sd_event_queue.read_fd;
}

void sd_python_event_ready(void)
{
  python_event_pending = TRUE;
}

void sd_python_event_clear(void)
{
  python_event_pending = FALSE;
}

void sd_python_event_drain(void)
{
  sd_event_drain_wakeup();
}

int sd_python_event_count(void)
{
  int count;

  pthread_mutex_lock(&sd_event_queue.mutex);
  count = sd_event_queue.count;
  pthread_mutex_unlock(&sd_event_queue.mutex);

  return count;
}

static int sd_event_queue_init(void)
{
  int fds[2];

  if (sd_event_queue.read_fd >= 0)
    return 0;

  if (pipe(fds) != 0)
    return SD_PyErr_EventInit;

  if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0 ||
      fcntl(fds[1], F_SETFL, O_NONBLOCK) != 0) {
    close(fds[0]);
    close(fds[1]);
    return SD_PyErr_EventInit;
  }

  pthread_mutex_lock(&sd_event_queue.mutex);
  sd_event_queue.read_fd = fds[0];
  sd_event_queue.write_fd = fds[1];
  sd_event_queue.shutting_down = FALSE;
  sd_event_queue.count = 0;
  python_event_pending = FALSE;
  pthread_mutex_unlock(&sd_event_queue.mutex);

  return 0;
}

static int sd_event_enqueue(const char *name, const char *payload)
{
  SD_EVENT *event;
  char notification = 'x';

  if (name == NULL || strlen(name) >= SD_EVENT_NAME_SIZE ||
      (payload != NULL && strlen(payload) >= SD_EVENT_PAYLOAD_SIZE))
    return SD_PyErr_EventTooBig;

  event = calloc(1, sizeof(SD_EVENT));
  if (event == NULL)
    return SD_Mem_Err;

  snprintf(event->name, sizeof(event->name), "%s", name);
  if (payload != NULL)
    snprintf(event->payload, sizeof(event->payload), "%s", payload);

  pthread_mutex_lock(&sd_event_queue.mutex);

  if (sd_event_queue.shutting_down) {
    pthread_mutex_unlock(&sd_event_queue.mutex);
    free(event);
    return SD_PyErr_EventClosed;
  }

  if (sd_event_queue.count >= SD_EVENT_QUEUE_LIMIT) {
    pthread_mutex_unlock(&sd_event_queue.mutex);
    free(event);
    return SD_PyErr_EventFull;
  }

  if (sd_event_queue.tail == NULL)
    sd_event_queue.head = event;
  else
    sd_event_queue.tail->next = event;
  sd_event_queue.tail = event;
  sd_event_queue.count++;

  pthread_mutex_unlock(&sd_event_queue.mutex);

  sd_python_event_ready();
  if (sd_event_queue.write_fd >= 0)
    (void)write(sd_event_queue.write_fd, &notification, 1);

  return 0;
}

static SD_EVENT *sd_event_dequeue(void)
{
  SD_EVENT *event;

  pthread_mutex_lock(&sd_event_queue.mutex);
  event = sd_event_queue.head;
  if (event != NULL) {
    sd_event_queue.head = event->next;
    event->next = NULL;
    if (sd_event_queue.head == NULL)
      sd_event_queue.tail = NULL;
    sd_event_queue.count--;
  }
  pthread_mutex_unlock(&sd_event_queue.mutex);

  return event;
}

static void sd_event_drain_wakeup(void)
{
  char buffer[64];

  if (sd_event_queue.read_fd < 0)
    return;

  while (read(sd_event_queue.read_fd, buffer, sizeof(buffer)) > 0)
    ;
}

static void sd_event_queue_shutdown(void)
{
  SD_EVENT *event;
  SD_EVENT *next;

  pthread_mutex_lock(&sd_event_queue.mutex);
  sd_event_queue.shutting_down = TRUE;
  event = sd_event_queue.head;
  sd_event_queue.head = NULL;
  sd_event_queue.tail = NULL;
  sd_event_queue.count = 0;
  pthread_mutex_unlock(&sd_event_queue.mutex);

  while (event != NULL) {
    next = event->next;
    free(event);
    event = next;
  }

  if (sd_event_queue.read_fd >= 0)
    close(sd_event_queue.read_fd);
  if (sd_event_queue.write_fd >= 0)
    close(sd_event_queue.write_fd);
  sd_event_queue.read_fd = -1;
  sd_event_queue.write_fd = -1;
  sd_python_event_clear();
}

static PyObject *sd_post_event(PyObject *self, PyObject *args)
{
  PyObject *py_name;
  PyObject *py_payload = Py_None;
  const char *name;
  const char *payload = "";
  int result;

  if (!PyArg_ParseTuple(args, "O|O:post_event", &py_name, &py_payload))
    return NULL;
  if (!PyUnicode_Check(py_name)) {
    PyErr_SetString(PyExc_TypeError, "event name must be a string");
    return NULL;
  }

  name = PyUnicode_AsUTF8(py_name);
  if (name == NULL)
    return NULL;

  if (py_payload != Py_None) {
    if (!PyUnicode_Check(py_payload)) {
      PyErr_SetString(PyExc_TypeError,
                      "event payload must be a string or None");
      return NULL;
    }
    payload = PyUnicode_AsUTF8(py_payload);
    if (payload == NULL)
      return NULL;
  }

  result = sd_event_enqueue(name, payload);
  if (result == SD_PyErr_EventFull)
    PyErr_SetString(PyExc_BufferError, "SD event queue is full");
  else if (result == SD_PyErr_EventClosed)
    PyErr_SetString(PyExc_RuntimeError, "SD event queue is closed");
  else if (result != 0)
    PyErr_Format(PyExc_RuntimeError, "could not enqueue SD event: %d", result);
  else
    Py_RETURN_NONE;

  return NULL;
}

static int sd_py_gui_step(void)
{
  PyObject *gui_step;
  PyObject *result;

  gui_step = PyMapping_GetItemString(global_dict, "gui_step");
  if (gui_step == NULL)
    return SD_PyErr_GuiError;

  result = PyObject_CallNoArgs(gui_step);
  Py_DECREF(gui_step);
  if (result == NULL) {
    PyErr_Print();
    return SD_PyErr_GuiError;
  }

  Py_DECREF(result);
  return 0;
}

static int sd_py_poll(void)
{
  SD_EVENT *event;
  char result[SD_EVENT_NAME_SIZE + SD_EVENT_PAYLOAD_SIZE + 2];

  sd_event_drain_wakeup();
  event = sd_event_dequeue();
  if (event == NULL) {
    k_put_c_string("", e_stack);
    e_stack++;
    process.status = SD_PyErr_EventEmpty;
    sd_python_event_clear();
    return SD_PyErr_EventEmpty;
  }

  if (snprintf(result, sizeof(result), "%s%c%s",
               event->name, FIELD_MARK, event->payload) >=
      (int)sizeof(result)) {
    free(event);
    k_put_c_string("", e_stack);
    e_stack++;
    process.status = SD_PyErr_EventTooBig;
    return SD_PyErr_EventTooBig;
  }

  free(event);
  k_put_c_string(result, e_stack);
  e_stack++;
  process.status = 0;

  if (sd_python_event_count() == 0)
    sd_python_event_clear();

  return 0;
}
