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

/* structures for cooperative scheduler */


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

/*                                                                                    */
/* All important function that calls the function that implements the python function */
/* key - python function to call , defined in keys.h    */
/* Arg   thru  Arg7   vary depending on called function */
/* somewhat defined in SYSCOM/SDPYFUNC.H                */
void sdext_py(int key, char* Arg, char* Arg2, char* Arg3, char* Arg4, char* Arg5, char* Arg6, char* Arg7){

  FILE *pyfd;         /* file descriptor for python script file */
  char *pyscriptname; /* script name from pyfd */
  char pyFilePath[PATH_MAX+1] = "";   /* max length a file path can be, defined in linux/limits.h */
  
  int myResult;

  PyObject *pval, *prun;
  char nullresult[] = "";
  
  process.status = 0;   /* setup status() value */
  myResult = 0;         /* init result response */
  /*Evaluate KEY */
  
  switch (key) {

    case SD_PyInit: /* Initialize python */
      if (!Py_IsInitialized()) {  /* only initialize if not already so */
        myResult = sd_event_queue_init();
        if (myResult != 0)
          break;

        if (!sd_module_registered) {
          if (PyImport_AppendInittab("sd", &PyInit_sd) == -1) {
            myResult = SD_PyErr_MainMod;
            break;
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
            }

            if (myResult == 0) {
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
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SD_PyFinal: /* Finalize the python interpreter   */
      /* rem  global_dict, main_module are Borrowed Reference  */
                  
      myResult = sdext_py_finalize();
      
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;

      break;

    case SD_IsPyInit: /* Is python interpreter initialized?   */
                  
      myResult = Py_IsInitialized();

      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SD_PyGuiStep:
      if (Py_IsInitialized())
        myResult = sd_py_gui_step();
      else
        myResult = SD_PyEr_NotInit;
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SD_PyPoll:
      if (Py_IsInitialized())
        (void)sd_py_poll();
      else {
        process.status = SD_PyEr_NotInit;
        k_put_c_string(nullresult, e_stack);
        e_stack++;
      }
      break;

    case SD_PyGuiWinUpdate:
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
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;
    
    case SD_PyGuiWinUpdateS:
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
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;

    case SD_PyGuiWinUpdValues:
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
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;  


    case SD_PyGuiTableSet:
      /* populate existing list with table data from dyn array */
      if (Py_IsInitialized()){
        // FreeSimpleGUI must have been imported in script as sg 

        // Arg: table list name, Arg2: dyn list for table data
        myResult = sd_py_table_set(Arg, Arg2);

      } else {
        myResult = SD_PyEr_NotInit;
      }
      process.status = myResult;
      InitDescr(e_stack, INTEGER);
      (e_stack++)->data.value = (int32_t)myResult;
      break;  

    case SD_PyGuiTableGet:   
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

    case SD_PyCallFunction:
      if (Py_IsInitialized()){
        // Arg: "Function Name", Arg2: "Function Arg", Arg3: "Function Arg", Arg4: "Function Arg, Arg5: "Function Arg"
        // static int sd_py_call_function(const char *function_name, const char *Arg1, const char *ArgType1, const char *Arg2, const char *ArgType2,const char *Arg3, const char *ArgType3)
        myResult = sd_py_call_function(Arg, Arg2, Arg3, Arg4, Arg5, Arg6, Arg7);
      }else{
        myResult = SD_PyEr_NotInit;  
      }  

      process.status = myResult;
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
          Py_DECREF(prun);
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
             Py_DECREF(prun);
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

/* shut down python */

int sdext_py_finalize(void){
      if (Py_IsInitialized()) {  /* only finalize if previously initialized */
        sd_event_queue_shutdown();
        return Py_FinalizeEx();
      } else {
        return SD_PyEr_NotInit; 
      }
}

/* Function to call python module function                                                                          */
/* Arg: "Function Name", Arg1: "Function Arg", ArgType1: "Function Arg Type"                                        */
/*                       Arg2: "Function Arg", ArgType2: "Function Arg Type"                                        */
/*                       Arg3: "Function Arg", ArgType3: "Function Arg Type"                                        */
/* Function Arg is the string name of the python object already define in the global dir                            */
/* Function Arg type is the python object type defined in keys.h                                                    */
/*   SD_Obj_Unkn         0   Unknown python object type                                                             */
/*   SD_Obj_Str          1   String (Unicode) python object type                                                    */
/*   SD_Obj_List         2   List python object type                                                                */
/*   SD_Obj_Dict         3   Dictionary  python object type                                                         */
/*   SD_Obj_Long         4   Long (int)  python object type                                                         */
/*   SD_Obj_Float        5   Float (floating point)  python object type                                             */

/* REM we assume function is local to prior executed script OR imported by the prior execute script                 */
/* Either way the assumption is these is a referece to the function available in __mani__                           */
/* Routine handles varible arg count by assing first NULL arg segnifies the end of passed arguments AND sets        */
/*  the number of arguments to pass to the python function, kind of a kludge                                        */
int sd_py_call_function(const char *function_name, const char *Arg1, const char *ArgType1, const char *Arg2, const char *ArgType2,const char *Arg3, const char *ArgType3){
return 0;
}

/* Functions for cooperative scheduler                                                                              */
/*  See ../Documentation/SD GUI Python Cooperative Schedule.txt for more information                                */

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
