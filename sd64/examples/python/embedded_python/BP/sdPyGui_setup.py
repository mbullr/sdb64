# sdPyGui_setup2.py
# Notes:
# All imports happen within the _setup.py script
# We should define windows within a function to ensure objects used during creation (the layout) get garbage collected.
# Because we are running embedded within the sd app and it is possible the sd process could be alive for long periods of
#  time, we should clean up (set to None) any objects created either within the _setup.py or _step.py script.
# Also at some point the sd Python function PY_PYGUISTEP()  (sd_py_gui_step in sdext_py.c) should be passed the name of #  the function defined in the _step.py script (in this example gui_step).  Currently it is hardcoded.
import sys
# sys.path.append('/home/xyz/python_stuff')   <=== directory that contains FreeSimpleGUI.py. Required for Ubuntu / Debian
#                                             On Debian, Python environment is externally managed
#                                             FreeSimpleGUI must be either locally installed and add path or use a virtual environment.
#                                            (Create an isolated environment using python3 -m venv .venv, to activate it, and run pip safely inside it).

import FreeSimpleGUI as sg
import json
import sd

_gui_closed = False
_close_event_sent = False
values = {} 
PUP_window = None

SDME_FM  = chr(254)
SDME_VM  = chr(253)
SDME_SVM = chr(252)
#
def my_popup(my_title, my_message):
    my_layout = [
                  [sg.Text(my_message)],
                  [sg.Button('Yes', key='-YES-'), sg.Button('No', key='-NO-')]
                  ]
    return sg.Window(my_title, my_layout, location=(100, 200), modal=True, finalize=True)
    
def my_main_window():
    layout = [ 
            [sg.Text('User Name', size=(12)), sg.Input(key='-UNAME-', size=(25))],
            [sg.Text('DOB', size=(12)), sg.Input(key='-DOB-', size=(25))],
            [sg.Text('Account', size=(12)), sg.Input(key='-ACCOUNT-', size=(25))],
            [sg.Multiline("Initial text\n", size=(40, 5), key="-TEXTBX-", autoscroll=True)],
            [sg.Button('Ok', key='-OK-'), sg.Button('PopUP', key='-PUP-')]
            ]
    return sg.Window('Gui Test', layout, element_justification='r',location=(100, 200), finalize=True)
    
_gui_closed = False
_close_event_sent = False
values = {} 
PUP_window = None

def gui_step(max_milliseconds=5):
    # note we must declare as global otherwise our sd functions will not find in global dict 
    global _gui_closed, _close_event_sent, values, PUP_window

    if _gui_closed:
        return 0

    if not main_window.TKroot.winfo_exists():
        _gui_closed = True
        return 0

    #event, values = window.read(timeout=max_milliseconds)
    window, event, values = sg.read_all_windows(timeout=max_milliseconds)
    
    if event == '__TIMEOUT__':
    # in an actual app we would not pass this event, for now we are doing it to track returns to VM
        sd.post_event(event, event) 
    
    # closing window logic
    elif event == sg.WIN_CLOSED:
        # main window?, if so close everything  down
        if window == main_window: 
            window.close()
            _gui_closed = True
            _close_event_sent = True
            # if the popup is still open, make sure it closes!
            if PUP_window: PUP_window.close()
            # finally tell the VM we are done
            sd.post_event("main_window", "closed")
            return 0
            
        # if it is our popup, close it and toss its object (otherwise we cannot re create it)
        elif window == PUP_window:
            window.close()
            PUP_window = None

        # other window, just close it
        else:
            window.close()
            
            
    # main window event logic
    elif window == main_window:
        if event == '-OK-':
        # payload = json.dumps(values)
            payload = "check values dictionary"
            sd.post_event(event,payload)
        elif event == '-PUP-':
            if PUP_window == None:
                PUP_window = my_popup('The PopUp','This PopUp Is For YOU!')
                PUP_Visable = True
                
    elif window == PUP_window:
        sd.post_event("Pup_window",event)
            
    return 0    
    
    
    
main_window = my_main_window()
#
# rem print(window.key_dict.keys()) to list keys in window
# dict_keys(['-UNAME-', '-DOB-', '-ACCOUNT-', 'Ok'])
# • Replace all text: Pass a new string into the update() method to clear the old content and show the new text.
# • Append new text: Use window["key"].update("additional text", append=True) to add new lines to the bottom without deleting # existing text.

main_window.TKroot.update_idletasks()
main_window.TKroot.update()