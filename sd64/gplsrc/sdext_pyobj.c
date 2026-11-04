/* SDEXT_PYOBJ.C
 * python integration for SD via SDEXT (op_sdext.c) BASIC function
 * Python object functions
 * Copyright (c)2026 The SD Developers, All Rights Reserved
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
 * 
 * START-HISTORY:
 * rev 1.0-3 mab reorganize code into mult c files (sdext_py.c was getting too large
 *              added cooperative scheuduler between VM and python 
 *              added sd_py GUI routines   
 * START-DESCRIPTION:
 *
 *
 * END-DESCRIPTION
 * START-CODE
 */
 
#include "sdext_py.h"
#include <fcntl.h>
#include <linux/limits.h>
#include "sd.h"
#include "keys.h"

// define my dyn array string field separators in unicode for python use
#define fm_sep 0
#define vm_sep 1
#define svm_sep 2
static const char* unicode_sd_sep[] = {
        "\u00fe", // thorn (þ) fm
        "\u00fd", // y with acute (ý) vm
        "\u00fc"  // u with diaeresis (ü) svm
        };

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
* This leaves the caller with the responsibility to call Py_DECREF() when they are done with the result; this soon becomes second nature.     
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
      Py_DECREF(new_dict);
    }

  } else {
    // exists, this is an error
    myResult = SD_PyErr_ObjExsts; 
    Py_DECREF(dict_lookup);

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
      Py_DECREF(dict_lookup);
      return 0;  
    } else {
      // object was not a dictionary
      Py_DECREF(dict_lookup);
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
        //Py_DECREF(Pykey);
        Py_DECREF(dict_lookup);
        return SD_PyErr_EnLatin;
      }  

      // Add the key-value pair to the dictionary
      
      //if (PyDict_SetItem(dict_lookup, Pykey, Pyvalue) != 0) {
      if (PyMapping_SetItemString(dict_lookup, key, Pyvalue) != 0) {
        // Failed to set dictionary key / value
        myResult = SD_PyErr_DictSet;
      }
      // Clean up
      //Py_DECREF(Pykey);
      Py_DECREF(Pyvalue);

    } else {
      // object was not a dictionary, report error
      myResult = SD_PyErr_NotDict;
    }

    Py_DECREF(dict_lookup);
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
          Py_DECREF(dict_lookup);
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
      Py_DECREF(Pykey);

    } else {
      // object was not a dictionary, report error
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      myResult = SD_PyErr_NotDict;
    }

    Py_DECREF(dict_lookup);
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
        Py_DECREF(dict_lookup);
        return SD_PyErr_EnLatin;
      }  

      // Remove key-value pair from the dictionary
      
      if (PyMapping_DelItem(dict_lookup, PyKey) != 0) {
        // Failed to delete dictionary key / value
        myResult = SD_PyErr_DictDel;
      }
      // Clean up
      Py_DECREF(PyKey);

    } else {
      // object was not a dictionary, report error
      myResult = SD_PyErr_NotDict;
    }

    Py_DECREF(dict_lookup);
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
      Py_DECREF(keys_list);
      if (process.status != 0){
        myResult = process.status;    // pass error number back (set by List_To_String)
      }  
      /* rem keys_str ref released by obj_to_str */
    } else {
      // object was not a dictionary, report error
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      myResult = SD_PyErr_NotDict;
    }
    
    Py_DECREF(dict_lookup);
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
      Py_DECREF(values_list);
      if (process.status != 0){
        myResult = process.status;    // pass error number back (set by List_To_String)
      }  
      /* rem keys_str ref released by obj_to_str */
    } else {
      // object was not a dictionary, report error
      k_put_c_string(nullresult, e_stack);   /* return empty string */
      myResult = SD_PyErr_NotDict;
    }
    
    Py_DECREF(dict_lookup);
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
    Py_DECREF(dict_lookup);
  }
  // free up ref  (global dict holds a ref to the new_string, we should be safe to do this)
  Py_DECREF(new_string);

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

    Py_DECREF(dict_lookup);
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

  /* does list (or object with this name) exist? */ 
  dict_lookup = lookup_dict_item(global_dict, listname);
  if (dict_lookup != NULL) {
    // already exists!
    Py_DECREF(dict_lookup);
    return SD_PyErr_ObjExsts;

  } else {
    // nothing matches that name, create the list
    PyObject *list = PyList_New(0);
    // did we succeed?
    if (!list) {
      // nope
      return SD_PyErr_List; 
    } else {
      // created, add it to the main_module name space
      myResult = PyMapping_SetItemString(global_dict, listname, list);
      if (myResult != 0){
        // failed to add to global dictionary namespace !?
        myResult =  SD_PyErr_NamSpcErr;
      }
      // free up ref  (global dict holds a ref to the new list, we should be safe to do this)
      Py_DECREF(list);
    }

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

    Py_DECREF(dict_lookup);
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
    Py_DECREF(list_lookup);
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

    Py_DECREF(list_lookup);
    Py_DECREF(obj_lookup);
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

  Py_DECREF(list_lookup);
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

    Py_DECREF(list_lookup);
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
 
  Py_DECREF(obj_lookup);
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

  Py_DECREF(obj_lookup);
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
      Py_DECREF(dict_lookup);
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
    Py_DECREF(PyObj);
    Py_DECREF(dict_lookup);
  }

  return myResult;
}

/* routine to add vm string list (dyn array in sd terms)   */
/* to python list object                                   */
/* list - python list object !! assumes caller has validated !!! */
/* value_list - vm list of values */
int sd_py_add_multi_value_to_list(PyObject *list, const char *value_list)
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
    process.status = 0;
    
// check for valid seperator 
    if (sep_char < fm_sep || sep_char > svm_sep){    
        process.status = SD_PyErr_UniSepIdx;
        return NULL;
    }
// check for elements in list
    
    Py_ssize_t list_size = PyList_Size(list);
    if (list_size <= 0) {
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

    PyObject* sep_str = PyUnicode_FromString(unicode_sd_sep[sep_char]); // create field separator character
     if (!sep_str) {
      Py_DECREF(result_str);
      process.status = SD_PyErr_CreStr;
      return NULL;    
    }

    // iterate over all the list items
    for (Py_ssize_t i = 0; i < list_size; ++i) {
        // get list item i
        PyObject* item = PySequence_GetItem(list, i);
        if (!item) {
          Py_DECREF(sep_str);
          Py_DECREF(result_str);
          process.status = SD_PyErr_LstItem;
          return NULL;
        }
        // create string object from list item 
        PyObject* item_str = PyObject_Str(item);
        Py_DECREF(item);
        if (!item_str) {
          Py_DECREF(sep_str);
          Py_DECREF(result_str);
          process.status = SD_PyErr_CreStr;
          return NULL;
        }

        if (!first_pass){   // only add separator after first pass
          // concatenate together, but we need to add in separator first!
          PyObject* temp_sep = PyUnicode_Concat(result_str, sep_str);

          if (!temp_sep) {
            Py_DECREF(item_str);
            Py_DECREF(sep_str);
            Py_DECREF(result_str);
            process.status = SD_PyErr_ConCat;
            return NULL;
          }
          // release the prior version of result_str and point to new concatinated string object
          Py_DECREF(result_str);
          // copy the pointer 
          result_str = temp_sep;

        } 
        
        first_pass = 0;
        PyObject* temp_str = PyUnicode_Concat(result_str, item_str);
        Py_DECREF(result_str);
        Py_DECREF(item_str);

        if (!temp_str) {
            Py_DECREF(sep_str);
            process.status = SD_PyErr_ConCat;
            return NULL;
        }

        result_str = temp_str; /* Rem does not change the reference count. It just copies the pointer. 
                                We Py_DECREF(result_str) before assignment, so the created object "should" be free
                                                  we then point result_str to temp_str, which is returned to caller on loop completion.
                                                  (caller's responsibility to dec) */
    }
    Py_DECREF(sep_str);
    return result_str;
}

/***************************************************************************************************************************/
/*  convert python object to string                                                                                        */
/*  this routine is responsible for ref count of passed python object pval                                                 */
/*  caller is responsible for e_stack (must inc)                                                                           */
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
      /* not a byte object */
      /* for now all non byte objects are converted to string then encoded as Latin, more work to be done here  */
      psval =  PyObject_Str(pval);              /* convert result object to string representation */
      
      if (psval == NULL) {
                                                /* error converting python object to string */
        process.status = SD_PyEr_ObToStr;
        k_put_c_string(nullresult, e_stack);    /* sets descr as type string and place the python value into it */
        Py_DECREF(pval);
        return;      
      }
      
      /* encode our string object to bytes */
      pbval =  PyUnicode_AsLatin1String(psval); /* encode to bytes using Latin1 */
      
      if (pbval == NULL) {
                                              /* error encoding unicode python string to to Latin */
        process.status = SD_PyErr_UniToStr;
        k_put_c_string(nullresult, e_stack);   /* sets descr as type string and place the python value into it */
        Py_DECREF(pval);
        Py_DECREF(psval);
        return;
      }

      pyResult = PyBytes_AsString(pbval);  /* finally get the c string */
      //fprintf(stderr,"Result: %s\n",pyResult);
      
      k_put_c_string(pyResult, e_stack);   /* sets descr as type string and place the python value into it */
      Py_DECREF(psval);
      Py_DECREF(pbval);
    }

    Py_DECREF(pval);

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
