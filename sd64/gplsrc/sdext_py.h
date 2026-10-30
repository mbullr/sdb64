#ifndef __SDPYD
#define __SDPYD
/*  PYTHON FUNCTIONS  */

#ifdef EMBED_PYTHON
#include "sdext_python_inc.h"  /* NOTE! this file is created by the install script !!! */

/* sdext_py.c         */
void sdext_py(int key, char* Arg, char* Arg2, char* Arg3, char* Arg4, char* Arg5, char* Arg6, char* Arg7);
int sdext_py_finalize(void);
int sd_py_call_function(const char *function_name, const char *Arg1, const char *ArgType1, const char *Arg2, const char *ArgType2,const char *Arg3, const char *ArgType3);

/* sdext_py_schdlr.c   */
#define SD_EVENT_NAME_SIZE 64
#define SD_EVENT_PAYLOAD_SIZE 4096
#define SD_EVENT_QUEUE_LIMIT 1024

typedef struct SD_EVENT {
  char name[SD_EVENT_NAME_SIZE];
  char payload[SD_EVENT_PAYLOAD_SIZE];
  struct SD_EVENT *next;
} SD_EVENT;

/* sdext_pyobj.c      */

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

int sd_py_add_multi_value_to_list(PyObject *list, const char *value_list);
void obj_to_str(PyObject* pval);
PyObject* lookup_dict_item(PyObject* dict, const char* key);
PyObject* List_To_String(PyObject* list, int sep_char);

    
/* sdext_pygui.c        */

/* pre define the Gui Module we will use, provide function to set this by user*/
# define PyGuiModuleSz 20
extern char PyGuiModule[PyGuiModuleSz];
extern PyObject *global_dict, *main_module;  /* global PyObjects that must hang around between calls */
    
int sd_py_update_key_value_in_window(const char *window_name, const char *key_name, const char *new_text);
int sd_py_update_multi_key_value_in_window(const char *window_name, const char *key_list, const char *value_list);
int sd_py_update_window_values_list(const char *window_name, const char *key_name, const char *listname); 
int sd_py_table_set(const char *list_name, const char *dyn_list);
void sd_py_table_get(const char *list_name);

    

#endif

#endif