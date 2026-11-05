/* SDEXT_PYGUI.C
 * python integration for SD via SDEXT (op_sdext.c) BASIC function
 * Functions to support sd PYGUI via FreeSimpleGUI python module
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

/***************************************************************************************/
/*                 FreeSimpleGUI FUnctions                                             */
/***************************************************************************************/
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
    sd_pyupdate_key_value_in_window("My Window", "my_key", "new text");
    Py_DECREF(mod);
} else {
    PyErr_Print();
    return SD_PyErr_NoFreeGui;
}
*/    
int sd_py_update_key_value_in_window(const char *window_name,
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
int sd_py_update_multi_key_value_in_window(const char *window_name,
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

//static int sd_py_update_window_values_list(PyObject* module, const char *window_name, const char *key_name, const char *listname) 
int sd_py_update_window_values_list(const char *window_name, const char *key_name, const char *listname) 
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
    Py_DECREF(pUpdateMethod);
    Py_DECREF(pKwargs);        // Note !! This safely decrements the reference to pExistingList inside it
    Py_DECREF(pElement);
    return 0;
}

// FreeSimpleGui table list build function                                 //
// take the dynamic array in dyn_list and populate                         //
// Existing list list_name with it                                         //
// in such a manner as it will work to populate FreeSimpleGUI Table widget //
int sd_py_table_set(const char *list_name, const char *dyn_list)
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
      Py_DECREF(ptable_list);
      return SD_PyErr_NotList;
    }

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
        Py_DECREF(ptable_list);
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
          Py_DECREF(ptable_list);
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
          Py_DECREF(ptable_list);
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
          Py_DECREF(ptable_list);  
          PyErr_Print();
          return SD_PyErr_LstAppdEr;
        }  
      }
      Py_DECREF(plist);   // done with the list created for the current row's values

    //  and process next row
    
  }
Py_DECREF(ptable_list);  
return 0;  
}

void sd_py_table_get(const char *pylist_name){
// get list_name's values from the FreeSimpleGUI table list and populate the callers (sd's) data object pointed to by e_stack 
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
  char nullresult[] = "";
  
  // rtnNull return a null value in the sd string descriptor pointed to by estack and inc the stack
  #define rtnNull(){\
    k_put_c_string(nullresult, e_stack);\
    e_stack++;\
  }
  
  /* does list (or object with this name) exist? */ 
  PyObject *ptable_list = lookup_dict_item(global_dict, pylist_name);
  if (ptable_list == NULL) {
    rtnNull(); 
    process.status = SD_PyErr_ObjNOF;  
    return;
  } else {
    
    // exists, is it a list object?
    if (!PyList_Check(ptable_list)){
      // object was not a list object, report error
      Py_DECREF(ptable_list);
      rtnNull();
      process.status = SD_PyErr_NotList;
      return;
    }
   
  }
  
  // check for elements in list
    
  Py_ssize_t list_size = PyList_Size(ptable_list);
  if (list_size <= 0) {
      rtnNull();
      Py_DECREF(ptable_list); 
      process.status = SD_PyErr_NoItems;
      return;
  }
  
  // create an empty string object to hold our result
  // and vm string for separator 

  PyObject* result_str = PyUnicode_FromString("");
  if (!result_str) {
    rtnNull();
    Py_DECREF(ptable_list); 
    process.status = SD_PyErr_CreStr;
    return;
  }
  PyObject* sep_str = PyUnicode_FromString(unicode_sd_sep[vm_sep]); // create vm field separator character
  if (!sep_str) {
    Py_DECREF(result_str);
    rtnNull();
    Py_DECREF(ptable_list);
    process.status = SD_PyErr_CreStr;
    return;
  }
  
  // iterate over all the list items
  int first_pass;
  first_pass = 1;  
      
  for (Py_ssize_t i = 0; i < list_size; ++i) {
    // get list item i
    PyObject* pcol_list = PySequence_GetItem(ptable_list, i);
    if (!pcol_list) {
      Py_DECREF(sep_str);
      Py_DECREF(result_str);
      rtnNull();
      Py_DECREF(ptable_list);
      process.status = SD_PyErr_LstItem;
      return;
    }
    // convert the list item (which should be a list of column values) to a string separated by svm
    
    PyObject* pcol_str = List_To_String(pcol_list, svm_sep);  // rem sets process.status on error
    Py_DECREF(pcol_list);  // done with col_list
    if (!pcol_str) {
      Py_DECREF(sep_str);
      Py_DECREF(result_str);
      rtnNull();
      Py_DECREF(ptable_list);
      process.status = SD_PyErr_CreStr;
      return;
    }                                           

    if (!first_pass){   // only add separator after first pass
      // concatenate together, but we need to add in separator first!
      PyObject* temp_sep = PyUnicode_Concat(result_str, sep_str);

      if (!temp_sep) {
        Py_DECREF(pcol_str);
        Py_DECREF(sep_str);
        Py_DECREF(result_str);
        rtnNull();
        Py_DECREF(ptable_list);
        process.status = SD_PyErr_ConCat;
        return;
      }
      // release the prior version of result_str and point to new concatenated string object
      Py_DECREF(result_str);
      // copy the pointer 
      result_str = temp_sep;

    } 
      
    first_pass = 0;
    PyObject* temp_str = PyUnicode_Concat(result_str, pcol_str);
    Py_DECREF(result_str);
    Py_DECREF(pcol_str);

    if (!temp_str) {
        Py_DECREF(sep_str);
        rtnNull();
        Py_DECREF(ptable_list);
        process.status = SD_PyErr_ConCat;
        return;
    }

    result_str = temp_str; /* Rem does not change the reference count. It just copies the pointer. 
                              We Py_DECREF(result_str) before assignment, so the created object "should" be free
                              we then point result_str to temp_str, which is returned to caller on loop completion.
                              (caller's responsibility to dec) */
  }
  Py_DECREF(sep_str);
  /* convet to string, note obj_to_str handles any errors   from PyMapping_GetItemString */
  obj_to_str(result_str);
  e_stack++;
  Py_DECREF(ptable_list);
  return;
}
