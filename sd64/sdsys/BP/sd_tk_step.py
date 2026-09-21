import time
import tkinter as tk
import _tkinter


def gui_step(max_events=32, max_milliseconds=5):
    global _gui_closed, _close_event_sent

    if _gui_closed:
        return 0

    if not root.winfo_exists():
        _gui_closed = True
        return 0

    processed = 0
    deadline = time.monotonic() + (max_milliseconds / 1000.0)

    try:
        root.update_idletasks()

        while (processed < max_events and
               time.monotonic() < deadline):
            handled = root.tk.dooneevent(
                _tkinter.ALL_EVENTS | _tkinter.DONT_WAIT
            )
            if not handled:
                break
            processed += 1
    except tk.TclError:
        _gui_closed = True
        if not _close_event_sent:
            _close_event_sent = True
            sd.post_event("window", "closed")

    return processed
