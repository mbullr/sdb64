import tkinter as tk
import sd

root = tk.Tk()
root.title("SD Tkinter Demo")
root.geometry("420x180")

_gui_closed = False
_close_event_sent = False


def post_event(name, payload=""):
    if not _gui_closed:
        sd.post_event(name, payload)


def on_save():
    post_event("button", "save")


def on_cancel():
    post_event("button", "cancel")


def on_close():
    global _gui_closed, _close_event_sent

    if _close_event_sent:
        return

    _close_event_sent = True
    _gui_closed = True
    sd.post_event("window", "closed")
    root.destroy()


root.protocol("WM_DELETE_WINDOW", on_close)

frame = tk.Frame(root)
frame.pack(expand=True, fill="both", padx=20, pady=20)

tk.Label(frame, text="SD Tkinter cooperative demo").pack(pady=10)

buttons = tk.Frame(frame)
buttons.pack()
tk.Button(buttons, text="Save", command=on_save).pack(side="left", padx=5)
tk.Button(buttons, text="Cancel", command=on_cancel).pack(side="left", padx=5)

root.update_idletasks()
root.update()
