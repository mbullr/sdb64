# rem all imports happen within the _setup.py script

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