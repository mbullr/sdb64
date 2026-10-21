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

#include "sdext_python_inc.h"  /* NOTE! this file is created by the install script !!! */
#include "sd.h"
#include <fcntl.h>
#include <linux/limits.h>
#include <libgen.h>            /* needed for basename function */ 
#include <pthread.h>
#include <unistd.h>

#include "keys.h"

int PyDictCrte(char* dictname);
int PyDictClr(char* dictname );
int PyDictValSetS(char* dictname, char* key, char* value);
int PyDictValGetS(char* dictname, char* key);
int PyDictDel(char* dictname, char* key);
int PyDictKeysS(char* dictname);
int PyDictValuesS(char* dictname);

int PyListGet(char* listname);
int PyListAppd(char* listname, char* objname);
int PyListClr(char* listname);

int PyStrSet(char* strname, char* strvalue );
int PyStrGet(char* strname );

int PyObjLen(char* objname);
int PyObjType(char* objname);

int PyDelObj(char* objname);

PyObject* List_To_String(PyObject* list, int sep_char);
// define my dyn array string field separators
#define fm_sep 1
#define vm_sep 2
#define svm_sep 3


PyObject* lookup_dict_item(PyObject* dict, const char* key);

void obj_to_str(PyObject* pval);



/* python objects for embedded python, these need to hang around between calls! */

PyObject *global_dict, *main_module;  /* global PyObjects that must hang around between calls */

/* pre define the Gui Module we will use, provide function to set this by user*/
# define PyGuiModuleSz 20
char PyGuiModule[PyGuiModuleSz] = "FreeSimpleGUI";

#define SD_EVENT_NAME_SIZE 64
#define SD_EVENT_PAYLOAD_SIZE 4096
#define SD_EVENT_QUEUE_LIMIT 1024

typedef struct SD_EVENT {
  char name[SD_EVENT_NAME_SIZE];
  char payload[SD_EVENT_PAYLOAD_SIZE];
  struct SD_EVENT *next;
} SD_EVENT;

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

static volatile bool python_event_pending = FALSE;
static bool sd_module_registered = FALSE;

static int sd_event_queue_init(void);
static void sd_event_queue_shutdown(void);
static int sd_event_enqueue(const char *name, const char *payload);
static SD_EVENT *sd_event_dequeue(void);
static void sd_event_drain_wakeup(void);
static int sd_py_gui_step(void);
static int sd_py_poll(void);
static int sd_py_update_key_value_in_window(PyObject *module, const char *window_name, const char *key_name, const char *new_text);
static int sd_py_update_multi_key_value_in_window(PyObject *module, const char *window_name, const char *key_list, const char *value_list);
static int sd_py_update_window_values_list(PyObject* module, const char *window_name, const char *key_name, const char *listname); 
static int sd_py_table_set(const char *list_name, const char *dyn_list);
void sd_py_table_get(const char *list_name, const char *dyn_list);

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

/* FreeSimpleGui window update function               */
/* update single key / values in freesimplegui window */
/* key_name window key name                           */
/* new_text value to set                              */
/*   window.update[key_name] = new_text               */
/* 
 creates the window object for window named "window_name", looks up Key_name in `AllKeysDict`, gets the element, changes its value to new_text, and then calls `update()` on that element.
Sample call:
PyObject *mod = PyImport_ImportModule("FreeSimpleGUI");
if (mod != NULL) {
    sd_pyupdate_key_value_in_window(mod, "My Window", "my_key", "new text");
    Py_DECREF(mod);
} else {
    PyErr_Print();
    return SD_PyErr_NoFreeGui;
}
*/    
static int sd_py_update_key_value_in_window(PyObject *module, const char *window_name,
                              const char *key_name, const char *new_text)
{
    PyObject *pMainModule = PyImport_ImportModule("__main__");
    if (pMainModule == NULL) {
        PyErr_Print();
        return SD_PyErr_MainScope;
    }

    PyObject *pWindow = PyObject_GetAttrString(pMainModule,window_name);
    Py_DECREF(pMainModule);
    if (pWindow == NULL) {
        fprintf(stderr, "Window %s is not found\n",window_name);
        PyErr_Print();
        return SD_PyErr_GuiWinNOF;
    }

    // Extract 'AllKeysDict' from the Window object
    PyObject *pDict = PyObject_GetAttrString(pWindow, "AllKeysDict");
    if (pDict == NULL) {
        Py_DECREF(pWindow);
        fprintf(stderr, "GetAttrString: AllKeysDict not found\n");
        PyErr_Print();
        return SD_PyErr_ObjAttrNOF;
    }

    if (!PyDict_Check(pDict)) {
        fprintf(stderr, "AllKeysDict is not a dictionary\n");
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_NotDict;
    }

    PyObject *key = PyUnicode_FromString(key_name);
    if (key == NULL) {
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_CreStr;
    }

    PyObject *element = PyDict_GetItem(pDict, key);
    // rem PyDict_GetItem does not create a new reference. It returns a borrowed reference //
    Py_DECREF(key);

    if (element == NULL) {
        // PyErr_Format(PyExc_KeyError, "Key not found in AllKeysDict: %s", key_name);
        fprintf(stderr,"Key not found in AllKeysDict: %s\n", key_name);
        PyErr_Print();
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyEr_Key;
    }

    PyObject *value = PyUnicode_FromString(new_text);
    if (value == NULL) {
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_CreStr;
    }

    // Equivalent to: element.update(new_text)
    PyObject *result = PyObject_CallMethod(element, "update", "O", value);
    Py_DECREF(value);

    if (result == NULL) {
        fprintf(stderr,"Failed to update: %s\n", key_name);
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_CallMethod;
    }

    Py_DECREF(result);
    Py_DECREF(pDict);
    Py_DECREF(pWindow);

    return 0;
}
/*                                                  */
/* update mult key / values in freesimplegui window */
/* key_list vm marked list of keys                  */
/* value_list vm marked list of values              */
/*                                                  */
static int sd_py_update_multi_key_value_in_window(PyObject *module, const char *window_name,
                              const char *key_list, const char *value_list)
{
    char* key_name;
    char* new_value;
    int key_count;
    int value_count;
    int keyIdx;
    
    char* myVM = "\xFD";
    
    PyObject *pMainModule = PyImport_ImportModule("__main__");
    if (pMainModule == NULL) {
        PyErr_Print();
        return SD_PyErr_MainScope;
    }

    PyObject *pWindow = PyObject_GetAttrString(pMainModule,window_name);
    Py_DECREF(pMainModule);
    if (pWindow == NULL) {
        fprintf(stderr, "Window %s is not found\n",window_name);
        PyErr_Print();
        return SD_PyErr_GuiWinNOF;
    }

    // Extract 'AllKeysDict' from the Window object
    PyObject *pDict = PyObject_GetAttrString(pWindow, "AllKeysDict");
    if (pDict == NULL) {
        Py_DECREF(pWindow);
        fprintf(stderr, "GetAttrString: AllKeysDict not found\n");
        PyErr_Print();
        return SD_PyErr_ObjAttrNOF;
    }

    if (!PyDict_Check(pDict)) {
        fprintf(stderr, "AllKeysDict is not a dictionary\n");
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_NotDict;
    }
    
    // get key / value counts
    key_count = Dcount(key_list, myVM);
    value_count = Dcount(value_list, myVM);
    
    if (key_count == 0){
        fprintf(stderr, "String List Contains No Items\n");
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_NoItems;
    }
    
    if (key_count != value_count){
        fprintf(stderr, "Keys and Values count mismatch\n");
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_KeyValCnt; 
    }

    // now loop thru the key value pairs and update the window objects
    for (keyIdx = 0; keyIdx < key_count; keyIdx++) { 
        /* rem Extract allocates our buffer space must be freed when complete */
        /* rem we are a @vm list                                              */
        key_name  = Extract(key_list, 1,keyIdx+1, 0);
        new_value = Extract(value_list, 1,keyIdx+1, 0);
       
        PyObject *key = PyUnicode_FromString(key_name);
        if (key == NULL) {
            Py_DECREF(pDict);
            Py_DECREF(pWindow);
            free(key_name);
            free(new_value);
            return SD_PyErr_CreStr;
        }
    
        PyObject *element = PyDict_GetItem(pDict, key);
        // rem PyDict_GetItem does not create a new reference. It returns a borrowed reference //
        Py_DECREF(key);
    
        if (element == NULL) {
            // PyErr_Format(PyExc_KeyError, "Key not found in AllKeysDict: %s", key_name);
            fprintf(stderr,"Key not found in AllKeysDict: %s\n", key_name);
            Py_DECREF(pDict);
            Py_DECREF(pWindow);
            free(key_name);
            free(new_value);
            return SD_PyEr_Key;
        }
    
        PyObject *value = PyUnicode_FromString(new_value);
        if (value == NULL) {
            Py_DECREF(pDict);
            Py_DECREF(pWindow);
            free(key_name);
            free(new_value);
            return SD_PyErr_CreStr;
        }
    
        // Equivalent to: element.update(new_text)
        PyObject *result = PyObject_CallMethod(element, "update", "O", value);
        Py_DECREF(value);
    
        if (result == NULL) {
            fprintf(stderr,"Failed to update: %s\n", key_name);
            PyErr_Print();
            Py_DECREF(pDict);
            Py_DECREF(pWindow);
            free(key_name);
            free(new_value);
            return SD_PyErr_CallMethod;
        }
        
        Py_DECREF(result);
        free(key_name);
        free(new_value);
        
    }        
    
    Py_DECREF(pDict);
    Py_DECREF(pWindow);

    return 0;
}

/* FreeSimpleGui window update function               */
/* update values for freesimplegui window object      */
/* module - FreeSimpleGui module object               */
/* window_name - window object name                   */
/* name of widget in window (key) to update           */
/* listname - name of Existing python List to update widget with     */

static int sd_py_update_window_values_list(PyObject* module, const char *window_name, const char *key_name, const char *listname) 
{
    char nullresult[] = "";

    PyObject *pMainModule = PyImport_ImportModule("__main__");
    if (pMainModule == NULL) {
        PyErr_Print();
        return SD_PyErr_MainScope;
    }

    PyObject *pWindow = PyObject_GetAttrString(pMainModule,window_name);
    Py_DECREF(pMainModule);
    if (pWindow == NULL) {
        fprintf(stderr, "Window %s is not found\n",window_name);
        PyErr_Print();
        return SD_PyErr_GuiWinNOF;
    }

    // Extract 'AllKeysDict' from the Window object
    PyObject *pDict = PyObject_GetAttrString(pWindow, "AllKeysDict");
    if (pDict == NULL) {
        Py_DECREF(pWindow);
        fprintf(stderr, "GetAttrString: AllKeysDict not found\n");
        PyErr_Print();
        return SD_PyErr_ObjAttrNOF;
    }

    if (!PyDict_Check(pDict)) {
        fprintf(stderr, "AllKeysDict is not a dictionary\n");
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        return SD_PyErr_NotDict;
    }


    /* does list (or object with this name) exist? */ 
    PyObject *pExistingList = lookup_dict_item(global_dict, listname);
    if (pExistingList == NULL) {
      // does not exist!
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      Py_DECREF(pDict);
      Py_DECREF(pWindow);
      return SD_PyErr_ObjNOF;
    } 

    // Safety check: ensure the passed object is actually a Python list
    if (!PyList_Check(pExistingList)) {
        fprintf(stderr, "Expected a valid list object.");
        Py_DECREF(pDict);
        Py_DECREF(pWindow);
        Py_DECREF(pExistingList);
        return SD_PyErr_NotList;
    }

    // 1. Look up the  element: window[key]
    PyObject* pKey = PyUnicode_FromString(key_name);
    if (pKey == NULL) {
      Py_DECREF(pDict);
      Py_DECREF(pWindow);
      Py_DECREF(pExistingList);
      return SD_PyErr_CreStr;
    }
    
    PyObject* pElement = PyObject_GetItem(pWindow, pKey);
    Py_DECREF(pKey); 

    if (pElement == NULL) {
        // PyErr_Format(PyExc_KeyError, "Key not found in AllKeysDict: %s", key_name);
      fprintf(stderr,"Key not found in AllKeysDict: %s\n", key_name);
      PyErr_Print();
      Py_DECREF(pDict);
      Py_DECREF(pWindow);
      Py_DECREF(pExistingList);
      return SD_PyEr_Key;
    }

    // 2. Create the kwargs dictionary for .update(values=...)
    PyObject* pKwargs = PyDict_New();
    
    // NOTE: PyDict_SetItemString INCREMENTS the reference count of pExistingList.
    PyDict_SetItemString(pKwargs, "values", pExistingList);

    // 3. Fetch the element's 'update' method
    PyObject* pUpdateMethod = PyObject_GetAttrString(pElement, "update");
    if (pUpdateMethod && PyCallable_Check(pUpdateMethod)) {
        
        // Positional args must be an empty tuple because we are using kwargs
        PyObject* pEmptyArgs = PyTuple_New(0);
        
        // 4. Execute: .update(values=pExistingList)
        PyObject* pResult = PyObject_Call(pUpdateMethod, pEmptyArgs, pKwargs);
        
        // Clean up call variables
        Py_DECREF(pEmptyArgs);
        if (pResult) {
            Py_DECREF(pResult); 
        } else {
            PyErr_Print(); // Handles Python traceback if .update() failed internally
        }
    } else {
        PyErr_Print();
    }

    // 5. Final cleanup of locally created objects
    Py_DECREF(pDict);
    Py_DECREF(pWindow);
    Py_DECREF(pExistingList);
    Py_XDECREF(pUpdateMethod);
    Py_DECREF(pKwargs);        // Note !! This safely decrements the reference to pExistingList inside it
    Py_DECREF(pElement);
    return 0;
}

/* routine to add vm string list (dyn array in sd terms)   */
/* to python list object                                   */
/* list - python list object !! assumes caller has validated !!! */
/* value_list - vm list of values */
static int sd_py_add_multi_value_to_list(PyObject *list, const char *value_list)
{
    char* vm_value;
    int value_count;
    int valIdx;
    int myResult;
    
    char* myVM = "\xFD";

    // value count
    value_count = Dcount(value_list, myVM);
    
    if (value_count == 0){
        fprintf(stderr, "String List Contains No Items\n");
        return SD_PyErr_NoItems;
    }
    // now loop thru the value list and append to list object
    for (valIdx = 0; valIdx < value_count; valIdx++) { 
        /* rem Extract allocates our buffer space must be freed when complete */
        /* rem we are a @vm list                                              */

        vm_value = Extract(value_list, 1,valIdx+1, 0);
       
        PyObject *pvalue = PyUnicode_FromString(vm_value);
        if (pvalue == NULL) {
            free(vm_value);
            return SD_PyErr_CreStr;
        }
    
        myResult = PyList_Append(list,pvalue);
        if (myResult != 0){
          // failed to append
          free(vm_value);
          Py_DECREF(pvalue);
          PyErr_Print();
          return SD_PyErr_LstAppdEr;
        }
        
        free(vm_value);
        Py_DECREF(pvalue);
        
    }        
    return 0;
}

// FreeSimpleGui table list build function                                 //
// take the dynamic array in dyn_list and populate                         //
// Existing list list_name with it                                         //
// in such a manner as it will work to populate FreeSimpleGUI Table widget //
static int sd_py_table_set(const char *list_name, const char *dyn_list)
{
// rem converting dyn array to list of lists:
// if a row (or cell) has set to an empty space ("")
// dyn_list:
// col1 <svm>   col2  <svm>   col3 <vm>   <--- row 1
// col1 <svm>   col2  <svm>   col3 <vm>   <--- row 2
// col1 <svm>   col2  <svm>   col3        <--- row 3
//
// to table list of lists
// [ [col1 ,    col2  ,   col3]   <--- row 1
//   [col1 ,    col2  ,   col3]   <--- row 2
//   [col1 ,    col2  ,   col3] ] <--- row 3

  int myResult;
  PyObject* ptable_list;

  int rowct;
  int colct;
  int rowidx;
  int colidx;
  char *rowdata;
  char *colcell;

  char* myVM = "\xFD";
  char* mySVM = "\xFC";

  //test if pylist_name is a list object (and defined in our gobal dic???)
  /* get a list of List objects items */  

  myResult = 0;

  /* does list (or object with this name) exist? */ 
  ptable_list = lookup_dict_item(global_dict, list_name);
  if (ptable_list == NULL) {
    return SD_PyErr_ObjNOF;
  } else {
    
    // exists, is it a list object?
    if (!PyList_Check(ptable_list)){
      // object was not a list object, report error
      Py_XDECREF(ptable_list);
      return SD_PyErr_NotList;
    }

    Py_XDECREF(ptable_list);   // safe to do here, still has ref in global dict
  }
    
  rowct = Dcount(dyn_list, myVM);

  // now loop thru the rows and append cols to list object
  for (rowidx = 0; rowidx < rowct; rowidx++) {

    rowdata = Extract(dyn_list, 1,rowidx+1, 0);
    colct = Dcount(rowdata, mySVM);

      // create "row" list
      PyObject *plist = PyList_New(0);
      if (!plist) {
        free(rowdata);
        return SD_PyErr_List;
      }

      // now extrax each row's col values and append cols values to list object
      for (colidx = 0; colidx < colct; colidx++) { 
        colcell = Extract(rowdata, 1, 1, colidx+1);

        // create python string from cell data 
        PyObject *pvalue = PyUnicode_FromString(colcell);
        if (pvalue == NULL) {
          free(rowdata);
          free(colcell);
          Py_DECREF(plist);
          PyErr_Print();
          return SD_PyErr_CreStr;
        }
        
        myResult = PyList_Append(plist,pvalue);
        if (myResult != 0){
        // failed to append
          free(rowdata);
          free(colcell);
          Py_DECREF(pvalue);
          Py_DECREF(plist);
          PyErr_Print();
          return SD_PyErr_LstAppdEr;
        }
        
        // we have added the cell data to our list, free it and it's python string 
        // and get next col cell for this row
        free(colcell);
        Py_DECREF(pvalue);  // rem successful append inc pvalue ref count so get ride of 1
      }
      
      //  if we ended up with and list of col values (the row data was not empty)
      //   append to pylist_name
      if (PyList_Size(plist) > 0){
        myResult = PyList_Append(ptable_list,plist);
        if (myResult != 0){
        // failed to append
          free(rowdata);
          Py_DECREF(plist);   
          PyErr_Print();
          return SD_PyErr_LstAppdEr;
        }  
      }
      Py_DECREF(plist);   // done with the list created for the current row's values

    //  and process next row
    
  }
return 0;  
}

void sd_py_table_get(const char *list_name, const char *dyn_list){
// get list_name's values from the FreeSimpleGUI table list and populate the dyn array dyn_list wit the data 
// rem sd_py_table_get:
// populates the sd data object pointed to by e_stack with k_put_c_string(c string, e_stack)  
// inc the stack pointer e_stack++
// sets process.status 
// psudo code:
// create empty python string object pystring_data
// access the existing table list
//
// for each row in table
//   convert row data (its list) to a string array with List_To_String
//   append this python string to pystring_data
//
// convert pystring_data to a c string using obj_to_str()
//
// rem: List_To_String(PyObject* list, int sep_char) function already exists to create python string from list object with
// items separated by specified field mark character
// and  obj_to_str(python_string_object) which will convert the string to a c string and place in sd variable
//

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

/*                                                                                    */
/* All important function that calls the function that implements the python function */
/* key - python function to call , defined in keys.h    */
/* Arg   Arg2  Arg3   vary depending on called function */
/* somewhat defined in SYSCOM/SDPYFUNC.H                */
void sdext_py(int key, char* Arg, char* Arg2, char* Arg3 ){

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
          myResult = sd_py_update_key_value_in_window(mod, Arg, Arg2, Arg3);
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
          myResult = sd_py_update_multi_key_value_in_window(mod, Arg, Arg2, Arg3);
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
          myResult = sd_py_update_window_values_list(mod, Arg, Arg2, Arg3);
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
      sd_py_table_get(Arg, Arg2);           
      // rem sd_py_table_get:
      // populates the sd data object pointed to by e_stack with k_put_c_string(c string, e_stack)  
      // inc the stack pointer e_stack++
      // sets process.status
      }

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
* Functions defined here are actually called from op_sdpyobj() the function that implements Basic function SD_PYOBJ    
* result = SD_PYOBJ(ARG3,ARG2,ARG1,py_funcition id)   where function ids are defined in keys.h    
* 
* Some Notes:
* From Python Documentation https://docs.python.org/3/c-api/index.html
* Introduction
*  Objects, Types and Reference Counts
*    Reference Counts:
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
    myResult = SD_PyErr_ObjExsts; 
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
/*  Delete Dictionary key  (and value)                                                                                              */
/***************************************************************************************************************************/  

int PyDictDel(char* dictname, char* key){
  /* delete dictionary key */  

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

      PyObject *PyKey = PyUnicode_DecodeLatin1(key, (Py_ssize_t) strlen(key), "strict");
      if (PyKey == NULL){
        PyErr_Print();
        Py_XDECREF(dict_lookup);
        return SD_PyErr_EnLatin;
      }  

      // Remove key-value pair from the dictionary
      
      if (PyMapping_DelItem(dict_lookup, PyKey) != 0) {
        // Failed to delete dictionary key / value
        myResult = SD_PyErr_DictDel;
      }
      // Clean up
      Py_XDECREF(PyKey);

    } else {
      // object was not a dictionary, report error
      myResult = SD_PyErr_NotDict;
    }

    Py_XDECREF(dict_lookup);
  }

  return myResult;
}

/***************************************************************************************************************************/
/*  Get dictionary keys as string                                                                                          */
/* rem, value ends up in SD descriptor so we really only return a status code                                              */
/***************************************************************************************************************************/

int PyDictKeysS(char* dictname){
  /* get a dictionary's keys in field marked list */  
  int myResult;
  PyObject* dict_lookup;
  PyObject* keys_list; 
  PyObject* keys_str;

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
      // get a pyobject list of keys
      keys_list = PyDict_Keys(dict_lookup);
      keys_str  = List_To_String(keys_list,fm_sep);
      obj_to_str(keys_str);
      Py_XDECREF(keys_list);
      if (process.status != 0){
        myResult = process.status;    // pass error number back (set by List_To_String)
      }  
      /* rem keys_str ref released by obj_to_str */
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
/* Get dictionary values as string                                                                                          */
/* rem, value ends up in SD descriptor so we really only return a status code                                              */
/***************************************************************************************************************************/

int PyDictValuesS(char* dictname){
  /* get a dictionary's values in field marked list */  
  int myResult;
  PyObject* dict_lookup;
  PyObject* values_list; 
  PyObject* values_str;

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
      // get a pyobject list of values
      values_list = PyDict_Values(dict_lookup);
      values_str  = List_To_String(values_list,fm_sep);
      obj_to_str(values_str);
      Py_XDECREF(values_list);
      if (process.status != 0){
        myResult = process.status;    // pass error number back (set by List_To_String)
      }  
      /* rem keys_str ref released by obj_to_str */
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
/* Get String                                                                                                              */
/* rem, value ends up in SD descriptor so we really only return a status code                                              */
/***************************************************************************************************************************/

int PyStrGet(char* strname ){
  /* get a dictionary key's value */  
  int myResult;
  PyObject* dict_lookup;

  myResult = 0;
  char nullresult[] = "";

  /* does string (or object with this name) exist? */ 
  dict_lookup = lookup_dict_item(global_dict, strname);
  if (dict_lookup == NULL) {
    // does not exist!
    k_put_c_string(nullresult, e_stack);   /* return empty string */
    return SD_PyErr_ObjNOF;
  } else {
    
    // exists, is it a unicode object?
    if (PyUnicode_Check(dict_lookup)){
        // yes, access and convert to string
        PyObject* value = PyMapping_GetItemString(global_dict, strname);
        obj_to_str(value);
    } else {
      // object was not a unicode object, report error
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      myResult = SD_PyErr_NotStr;
    }

    Py_XDECREF(dict_lookup);
  }

  return myResult;
}

/***************************************************************************************************************************/
/* Create List                                                                                                            */
/* rem, value ends up in SD descriptor so we really only return a status code                                              */
/***************************************************************************************************************************/

int PyListCrte(char* listname ){
  /* create List object */  
  int myResult;
  PyObject* dict_lookup;

  myResult = 0;
  char nullresult[] = "";

  /* does list (or object with this name) exist? */ 
  dict_lookup = lookup_dict_item(global_dict, listname);
  if (dict_lookup != NULL) {
    // already exists!
    k_put_c_string(nullresult, e_stack);   /* return empty string */
    Py_XDECREF(dict_lookup);
    return SD_PyErr_ObjExsts;

  } else {
    // nothing matches that name, create the list
    PyObject *list = PyList_New(0);
    // did we succeed?
    if (!list) {
      // nope
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      Py_XDECREF(dict_lookup);
      return SD_PyErr_List; 
    } else {
      // created, add it to the main_module name space
      myResult = PyMapping_SetItemString(global_dict, listname, list);
      if (myResult != 0){
        // failed to add to global dictionary namespace !?
        myResult =  SD_PyErr_NamSpcErr;
      }
      // free up ref  (global dict holds a ref to the new list, we should be safe to do this)
      Py_XDECREF(list);
    }

    Py_XDECREF(dict_lookup);
  }

  return myResult;
}


/***************************************************************************************************************************/
/* Get List                                                                                                            */
/* rem, value ends up in SD descriptor so we really only return a status code                                              */
/***************************************************************************************************************************/

int PyListGet(char* listname ){
  /* get a list of List objects items */  
  int myResult;
  PyObject* dict_lookup;

  myResult = 0;
  char nullresult[] = "";

  /* does list (or object with this name) exist? */ 
  dict_lookup = lookup_dict_item(global_dict, listname);
  if (dict_lookup == NULL) {
    // does not exist!
    k_put_c_string(nullresult, e_stack);   /* return empty string */
    return SD_PyErr_ObjNOF;
  } else {
    
    // exists, is it a list object?
    if (PyList_Check(dict_lookup)){
        // yes, access list items and convert to string
        PyObject* list_str  = List_To_String(dict_lookup,fm_sep);
        obj_to_str(list_str);
        if (process.status != 0){
          myResult = process.status;    // pass error number back (set by List_To_String)
        } 
    } else {
      // object was not a list object, report error
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      myResult = SD_PyErr_NotList;
    }

    Py_XDECREF(dict_lookup);
  }

  return myResult;
}

/***************************************************************************************************************************/
/* List append                                                                                                           */
/* Append  pyobject to list                                          */
/***************************************************************************************************************************/

int PyListAppd(char* listname, char* objname){

  int myResult;
  PyObject* list_lookup;
  PyObject* obj_lookup;
  myResult = 0;

  /* does listobject (or object with this name) already exist? */ 
  list_lookup = lookup_dict_item(global_dict, listname);
  if (list_lookup == NULL) {
  /* does not exist, report error */
    return SD_PyErr_ObjNOF;
  }
 
  /* does object to append exist? */ 
  obj_lookup = lookup_dict_item(global_dict, objname);
  if (obj_lookup == NULL) {
  /* does not exist, report error */
    Py_XDECREF(list_lookup);
    return SD_PyErr_ObjNOF;
  
  } else {
    // both exists, is it a list object?
    if (PyList_Check(list_lookup)){
      // yes, its a list, append object
      myResult = PyList_Append(list_lookup,obj_lookup);
      if (myResult != 0){
        // failed to append
        PyErr_Print();
        myResult =  SD_PyErr_LstAppdEr;
      }

    } else {
      // not a list
      myResult = SD_PyErr_NotList; 
    }

    Py_XDECREF(list_lookup);
    Py_XDECREF(obj_lookup);
  }


  return myResult;
}

/***************************************************************************************************************************/
/* List append dyn array                                                                                                          */
/* Append  sd dyn array items  to list  (vm separated)                                         */
/***************************************************************************************************************************/

int PyListAppdS(char* listname, char* value_list){

  int myResult;
  PyObject* list_lookup;
  myResult = 0;

  /* does listobject (or object with this name) already exist? */ 
  list_lookup = lookup_dict_item(global_dict, listname);
  if (list_lookup == NULL) {
  /* does not exist, report error */
    return SD_PyErr_ObjNOF;
  }
 
  //  is it a list object?
  if (PyList_Check(list_lookup)){
    // yes, its a list, append vm list to list
    myResult = sd_py_add_multi_value_to_list(list_lookup,value_list);
    if (myResult != 0){
      // failed to append
      PyErr_Print();
      myResult =  SD_PyErr_LstAppdEr;
    }
  } else {
    // not a list
    myResult = SD_PyErr_NotList; 
  }

  Py_XDECREF(list_lookup);
  return myResult;
}

/***************************************************************************************************************************/
/* List Clear                                                                                                              */
/* Deletes objects in list                                                                                                 */
/***************************************************************************************************************************/

int PyListClr(char* listname){

  int myResult;
  PyObject* list_lookup;
  myResult = 0;
 
  /* does the list object to append exist? */ 
  list_lookup = lookup_dict_item(global_dict, listname);
  if (list_lookup == NULL) {
  /* does not exist, report error */
    return SD_PyErr_ObjNOF;
  
  } else {
    // exists, is it a list object?
    if (PyList_Check(list_lookup)){
      // yes, its a list, clear the  object
          //PySequence_DelSlice(list_lookup, 0, PySequence_Length(list_lookup));
      // This mimics `del list[:]` by replacing the entire slice with NULL
      myResult = PyList_SetSlice(list_lookup, 0, PY_SSIZE_T_MAX, NULL);

      if (myResult != 0){
        // failed to clear
        PyErr_Print();
        myResult =  SD_PyErr_LstClrEr;
      }

    } else {
      // not a list
      myResult = SD_PyErr_NotList; 
    }

    Py_XDECREF(list_lookup);
  }

  return myResult;
}

/***************************************************************************************************************************/
/*  Return Python Object Length (size)                                                                                     */
/* Note the max size we can return is 32 bit (its what SD can handle as integer)                                           */
/***************************************************************************************************************************/  
int PyObjLen(char* objname){
 
  PyObject* obj_lookup;
  Py_ssize_t obj_lenght;
  int myResult;
  myResult = 0;

  /* does object  exist? */ 
  obj_lookup = lookup_dict_item(global_dict, objname);
  if (obj_lookup == NULL) {
  /* does not exist, report error */
  return SD_PyErr_ObjNOF;
  }    

  obj_lenght = PyObject_Length(obj_lookup);
  if (obj_lenght > INT_MAX){
    myResult = SD_INT_OVERFLW ;
  }else{
    myResult = (int) obj_lenght;
  } 
 
  Py_XDECREF(obj_lookup);
  return myResult;
}


/***************************************************************************************************************************/
/*  Return Python Object Type                                                                                             */
/***************************************************************************************************************************/  
int PyObjType(char* objname){

  int myResult;
  PyObject* obj_lookup;
  myResult = 0;

  /* does object  exist? */ 
  obj_lookup = lookup_dict_item(global_dict, objname);
  if (obj_lookup == NULL) {
   /* does not exist, report error */
   return SD_PyErr_ObjNOF;
  }    
  
  if (PyUnicode_Check(obj_lookup)) {
    myResult = SD_Obj_Str;

  } else if (PyDict_Check(obj_lookup)) {
    myResult = SD_Obj_Dict;

  } else if (PyList_Check(obj_lookup)) {
    myResult = SD_Obj_List;

  } else if (PyLong_Check(obj_lookup)) {
    myResult = SD_Obj_Long;

  } else if (PyFloat_Check(obj_lookup)) {
    myResult = SD_Obj_Float;

  } else {
    myResult = SD_Obj_Unkn;
  }

  Py_XDECREF(obj_lookup);
  return myResult;
}

/***************************************************************************************************************************/
/*  Delete Python Object (Remove from global dictionary)                                                                                             */
/***************************************************************************************************************************/  

int PyDelObj(char* objname){
  /* delete Python Object */  

  int myResult;
  PyObject* dict_lookup;

  myResult = 0;

  /* does the object with this name exist? */ 
  dict_lookup = lookup_dict_item(global_dict, objname);
  if (dict_lookup == NULL) {
    // does not exist!
    return SD_PyErr_ObjNOF;
  } else {
  
    PyObject *PyObj = PyUnicode_DecodeLatin1(objname, (Py_ssize_t) strlen(objname), "strict");
    if (PyObj == NULL){
      PyErr_Print();
      Py_XDECREF(dict_lookup);
      return SD_PyErr_EnLatin;
    }  

    // Remove from global dictionary
    // This should remove all references to the object, resulting in its deletion
    // via python garbage collection
    
    if (PyMapping_DelItem(global_dict, PyObj) != 0) {
      // Failed to remove object reference in global dictionary
      myResult = SD_PyErr_DelObj;
    }
    // Clean up
    Py_XDECREF(PyObj);
    Py_XDECREF(dict_lookup);
  }

  return myResult;
}

/***************************************************************************************************************************/
/*  convert python list object to python string  object with defined separator                                                            */
/***************************************************************************************************************************/
PyObject* List_To_String(PyObject* list, int sep_char){
// rem:
//#define fm_sep 1
//#define vm_sep 2
//#define svm_sep 3

// returns string object, caller is responsible to dec ref when finished with it!
//
    int first_pass;
    first_pass = 1;
// check for valid seperator 
    if (sep_char < fm_sep || sep_char > svm_sep){    
        process.status = SD_PyErr_NoItems;
        return NULL;
    }
// check for elements in list
    
    Py_ssize_t list_size = PyList_Size(list);
    if (list_size == 0) {
        process.status = SD_PyErr_NoItems;
        return NULL;
    }

    // create an empty string object to hold our result
    // and vm string for separator 

    PyObject* result_str = PyUnicode_FromString("");
    if (!result_str) {
        process.status = SD_PyErr_CreStr;
        return NULL;
    }

    PyObject* sep_str = NULL;

    switch (sep_char){
      case fm_sep:
        sep_str = PyUnicode_FromString("þ"); // field marker character latin (small letter thorn is the character "þ")
        break;

      case vm_sep:
        sep_str = PyUnicode_FromString("ý"); // value marker character latin (Latin Small Letter Y with Acute)
        break;

      case svm_sep:
        sep_str = PyUnicode_FromString("ü"); // sub value marker character latin (latin small letter U with diaeresis)
        break;
    }
   
    if (!sep_str) {
      Py_XDECREF(result_str);
      process.status = SD_PyErr_CreStr;
      return NULL;    
    }

    // iterate over all the list items
    for (Py_ssize_t i = 0; i < list_size; ++i) {
	    // get list item i
        PyObject* item = PySequence_GetItem(list, i);
        if (!item) {
          Py_XDECREF(sep_str);
          Py_XDECREF(result_str);
          process.status = SD_PyErr_LstItem;
          return NULL;
        }
        // create string object from list item 
        PyObject* item_str = PyObject_Str(item);
        Py_XDECREF(item);
        if (!item_str) {
          Py_XDECREF(sep_str);
          Py_XDECREF(result_str);
          process.status = SD_PyErr_CreStr;
          return NULL;
        }

        if (!first_pass){   // only add separator after first pass
          // concatenate together, but we need to add in separator first!
          PyObject* temp_sep = PyUnicode_Concat(result_str, sep_str);

          if (!temp_sep) {
            Py_XDECREF(item_str);
            Py_XDECREF(sep_str);
            Py_XDECREF(result_str);
            process.status = SD_PyErr_ConCat;
            return NULL;
          }
          // release the prior version of result_str and point to new concatinated string object
          Py_XDECREF(result_str);
          // copy the pointer 
          result_str = temp_sep;

        } 
        
        first_pass = 0;
        PyObject* temp_str = PyUnicode_Concat(result_str, item_str);
        Py_XDECREF(result_str);
        Py_XDECREF(item_str);

        if (!temp_str) {
            Py_XDECREF(sep_str);
            process.status = SD_PyErr_ConCat;
            return NULL;
        }

        result_str = temp_str; /* Rem does not change the reference count. It just copies the pointer. 
                                We Py_XDECREF(result_str) before assignment, so the created object "should" be free
									              we then point result_str to temp_str, which is returned to caller on loop completion.
									              (caller's responsibility to dec) */
    }
    Py_XDECREF(sep_str);
    return result_str;
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
      // REM The returned pointer points directly to the internal buffer of the PyBytesObject managed by the Python runtime.
      // SO DO NOT FREE IT, decref of pval will handle it
      k_put_c_string(pyResult, e_stack);   /* sets descr as type string and place the python value into it */
      
    }else{
      fprintf(stderr,"not bytes\n");
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
      fprintf(stderr,"Result: %s\n",pyResult);
      
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
