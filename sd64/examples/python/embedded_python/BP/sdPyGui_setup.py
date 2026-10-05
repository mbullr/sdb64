# sdPyGuiTest.py
import sys
# sys.path.append('/home/xyz/python_stuff')   <=== directory that contains FreeSimpleGUI.py. Required for Ubuntu / Debian
#                                             On Debian, Python environment is externally managed
#                                             FreeSimpleGUI must be either locally installed and add path or use a virtual environment.
#                                            (Create an isolated environment using python3 -m venv .venv, to activate it, and run pip safely inside it).
import FreeSimpleGUI as sg
import json
import sd


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
    
    
layout = [ 
            [sg.Text('User Name', size=(12)), sg.Input(key='-UNAME-', size=(25))],
            [sg.Text('DOB', size=(12)), sg.Input(key='-DOB-', size=(25))],
            [sg.Text('Account', size=(12)), sg.Input(key='-ACCOUNT-', size=(25))],
            [sg.Multiline("Initial text\n", size=(40, 5), key="-TEXTBX-", autoscroll=True)],
            [sg.Button('Ok', key='-OK-'), sg.Button('PopUP', key='-PUP-')]
            ]

main_window = sg.Window('Gui Test', layout, element_justification='r',location=(100, 200), finalize=True)
#
# rem print(window.key_dict.keys()) to list keys in window
# dict_keys(['-UNAME-', '-DOB-', '-ACCOUNT-', 'Ok'])
# • Replace all text: Pass a new string into the update() method to clear the old content and show the new text.
# • Append new text: Use window["key"].update("additional text", append=True) to add new lines to the bottom without deleting # existing text.

main_window.TKroot.update_idletasks()
main_window.TKroot.update()