/*
AutoHotkey

Copyright 2003-2005 Chris Mallett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef hook_h
#define hook_h

#include "stdafx.h" // pre-compiled headers
#include "hotkey.h"

// WM_USER is the lowest number that can be a user-defined message.  Anything above that is also valid.
// NOTE: Any msg about WM_USER will be kept buffered (unreplied-to) whenever the script is uninterruptible.
// If this is a problem, try making the msg have an ID less than WM_USER via a technique such as that used
// for AHK_USER_MENU (perhaps WM_COMMNOTIFY can be "overloaded" to contain more than one type of msg):
enum UserMessages {AHK_HOOK_HOTKEY = WM_USER, AHK_HOTSTRING, AHK_USER_MENU, AHK_DIALOG, AHK_NOTIFYICON
	, AHK_RETURN_PID, AHK_EXIT_BY_RELOAD, AHK_EXIT_BY_SINGLEINSTANCE
	, AHK_GUI_ACTION = WM_USER + 100  // Allow some room in between for more "exit" type msgs to be added in the future (see below comment).
	, AHK_HOOK_TEST_MSG};
// NOTE: TRY NEVER TO CHANGE the specific numbers of the above messages, since some users might be
// using the Post/SendMessage commands to automate AutoHotkey itself.  Here is the original order
// that should be maintained:
// AHK_HOOK_HOTKEY = WM_USER, AHK_HOTSTRING, AHK_USER_MENU, AHK_DIALOG, AHK_NOTIFYICON, AHK_RETURN_PID

// UPDATE to the below comment: After a careful review, it seems best to buffer a user's selection of
// a custom menu item if the current quasi-thread is uninterruptible.  This is exactly the same way
// hotkeys are buffered.  By making a thread truly uninterruptible, behavior is more consistent with
// what most users would want, though it should be noted that this also makes it impossible for the
// OnExit subroutine to be interrupted by a custom menu item, even one that is designed to simply
// exit the script (by means of aborting the on-exit subroutine).  Another reason to buffer the
// selection of a custom menu item is that we don't want to interrupt the current thread if is
// right in the middle of executing a single line, e.g. doing something with the deref buffer
// (during which time an interruption might destroy the buffer's contents) or trying to open the
// clipboard or save data to it.  This particular weakness may be resolved when/if a dedicated
// thread is created to maintain the hook(s), thus allowing critical operations such as opening
// the clipboard to use a true Sleep() rather than MsgSleep().
// Since WM_COMMNOTIFY is never generated by the Win32 API, and since we want AHK_USER_MENU to be
// an ID less than WM_HOTKEY so that it doesn't get filtered out when the script is uninterruptible,
// the following trick is used to map our user-defined messages onto WM_COMMNOTIFY by sacrificing the
// wParam part of the message (using it as an indicator of what the message really is).
// Another reserved msg that might be fairly safe is WM_MDIICONARRANGE, but it's far less preferable:
#define TRANSLATE_AHK_MSG(msg, wparam) \
	if (msg == WM_COMMNOTIFY)\
	{\
		msg = (UINT)wparam;\
		wparam = 0;\
	} // In the above, wparam is made zero to help catch bugs.

#define AHK_GUI_CLOSE     -1
#define AHK_GUI_ESCAPE    -2
#define AHK_GUI_SIZE      -3
#define AHK_GUI_DROPFILES -4

// And these macros are kept here so that all this trickery is centrally located and thus more maintainable:
#define ASK_INSTANCE_TO_CLOSE(window, reason) PostMessage(window, WM_COMMNOTIFY, reason, 0);
#define POST_AHK_USER_MENU(menu, gui_index) PostMessage(NULL, AHK_USER_MENU, gui_index, menu);
#define POST_AHK_GUI_ACTION(window, control_index, gui_event) PostMessage(window, AHK_GUI_ACTION, control_index, gui_event);
#define POST_AHK_DIALOG(timeout) PostMessage(g_hWnd, WM_COMMNOTIFY, AHK_DIALOG, (LPARAM)timeout);
// Notes about POST_AHK_USER_MENU: a gui_index value >= 0 is passed with the message if it came from a
// GUI's menu bar.  This is done because it's good way to pass the info, but also so that its value will
// be in sync with the timestamp of the message (in case the message is stuck in the queue for a long time).
// No pointer is passed in this case since they might become invalid between the time the msg is posted vs.
// processed.

// Notes about POST_AHK_DIALOG above:
// Post a special msg that will attempt to force it to the foreground after it has been displayed,
// since the dialog often will flash in the task bar instead of becoming foreground.
// It's enough just to queue up a single message that dialog's message pump will forward to our
// main window proc once the dialog window has been displayed.  This avoids the overhead of creating
// and destroying the timer (although the timer may be needed anyway if any timed subroutines are
// enabled).  My only concern about this is that on some OS's, or on slower CPUs, the message may be
// received too soon (before the dialog window actually exists) resulting in our window proc not
// being able to ensure that it's the foreground window.  That seems unlikely, however, since
// MessageBox() and the other dialog invocating API calls (for FileSelectFile/Folder) likely
// ensures its window really exists before dispatching messages.

// Notes about the below: It is important to call MsgSleep() immediately after posting the message
// in case a dialog's message pump is running, in which case the message would otherwise be lost
// due to the dialog's message pump being unable to dispatch thread messages (those with a NULL window),
// resulting in the loss of such messages:
#define HANDLE_USER_MENU(menu_id, gui_index) \
{\
	POST_AHK_USER_MENU(menu_id, gui_index) \
	MsgSleep(-1);\
}



enum DualNumpadKeys	{PAD_DECIMAL, PAD_NUMPAD0, PAD_NUMPAD1, PAD_NUMPAD2, PAD_NUMPAD3
, PAD_NUMPAD4, PAD_NUMPAD5, PAD_NUMPAD6, PAD_NUMPAD7, PAD_NUMPAD8, PAD_NUMPAD9
, PAD_DELETE, PAD_INSERT, PAD_END, PAD_DOWN, PAD_NEXT, PAD_LEFT, PAD_CLEAR
, PAD_RIGHT, PAD_HOME, PAD_UP, PAD_PRIOR, PAD_TOTAL_COUNT};


// Some reasoning behind the below data structures: Could build a new array for [sc][sc] and [vk][vk]
// (since only two keys are allowed in a ModifierVK/SC combination, only 2 dimensions are needed).
// But this would be a 512x512 array of shorts just for the SC part, which is 512K.  Instead, what we
// do is check whenever a key comes in: if it's a suffix and if a non-standard modifier key of any kind
// is currently down: consider action.  Most of the time, an action be found because the user isn't
// likely to be holding down a ModifierVK/SC, while pressing another key, unless it's modifying that key.
// Nor is he likely to have more than one ModifierVK/SC held down at a time.  It's still somewhat
// inefficient because have to look up the right prefix in a loop.  But most suffixes probably won't
// have more than one ModifierVK/SC anyway, so the lookup will usually find a match on the first
// iteration.

struct vk_hotkey
{
	vk_type vk;
	HotkeyIDType id_with_flags;
};
struct sc_hotkey
{
	sc_type sc;
	HotkeyIDType id_with_flags;
};



// User is likely to use more modifying vks than we do sc's, since sc's are rare:
#define MAX_MODIFIER_VKS_PER_SUFFIX 50
#define MAX_MODIFIER_SCS_PER_SUFFIX 16
// Style reminder: Any POD structs (those without any methods) don't use the "m" prefix
// for member variables because there's no need: the variables are always prefixed by
// the struct that owns them, so there's never any ambiguity:
struct key_type
{
	vk_hotkey ModifierVK[MAX_MODIFIER_VKS_PER_SUFFIX];
	sc_hotkey ModifierSC[MAX_MODIFIER_SCS_PER_SUFFIX];
	UCHAR nModifierVK;
	UCHAR nModifierSC;
//	vk_type toggleable_vk;  // If this key is CAPS/NUM/SCROLL-lock, its virtual key value is stored here.
	ToggleValueType *pForceToggle;  // Pointer to a global variable for toggleable keys only.  NULL for others.
	modLR_type as_modifiersLR; // If this key is a modifier, this will have the corresponding bit(s) for that key.
	HotkeyIDType hotkey_to_fire_upon_release; // A up-event hotkey queued by a prior down-event.
	bool used_as_prefix;  // whether a given virtual key or scan code is even used by a hotkey.
	bool used_as_suffix;  // whether a given virtual key or scan code is even used by a hotkey.
	bool used_as_key_up;  // Whether this suffix also has an enabled key-up hotkey.
	UCHAR no_suppress;
	bool is_down; // this key is currently down.
	bool it_put_alt_down;  // this key resulted in ALT being pushed down (due to alt-tab).
	bool it_put_shift_down;  // this key resulted in SHIFT being pushed down (due to shift-alt-tab).
	bool down_performed_action; // the last key-down resulted in an action (modifiers matched those of a valid hotkey)
	bool hotkey_down_was_suppressed; // Whether the down-event for a key was suppressed (thus its up-event should be too).
	// The values for "was_just_used" (zero is the inialized default, meaning it wasn't just used):
	char was_just_used; // a non-modifier key of any kind was pressed while this prefix key was down.
	// And these are the values for the above (besides 0):
	#define AS_PREFIX 1
	#define AS_PREFIX_FOR_HOTKEY 2
	bool sc_takes_precedence; // used only by the scan code array: this scan code should take precedence over vk.
}; // Keep the macro below in sync with the above.

#define RESET_KEYTYPE_ATTRIB(item) \
{\
	item.nModifierVK = 0;\
	item.nModifierSC = 0;\
	item.used_as_prefix = false;\
	item.used_as_suffix = false;\
	item.used_as_key_up = false;\
	item.no_suppress = false;\
	item.sc_takes_precedence = false;\
}

#define RESET_KEYTYPE_STATE(item) \
{\
	item.is_down = false;\
	item.it_put_alt_down = false;\
	item.it_put_shift_down = false;\
	item.down_performed_action = false;\
	item.was_just_used = 0;\
}



// Since index zero is a placeholder for the invalid virtual key or scan code, add one to each MAX value
// to compute the number of elements actually needed to accomodate 0 up to and including VK_MAX or SC_MAX:
#define VK_ARRAY_COUNT (VK_MAX + 1)
#define SC_ARRAY_COUNT (SC_MAX + 1)

#define INPUT_BUFFER_SIZE 16384

enum InputStatusType {INPUT_OFF, INPUT_IN_PROGRESS, INPUT_TIMED_OUT, INPUT_TERMINATED_BY_MATCH
	, INPUT_TERMINATED_BY_ENDKEY, INPUT_LIMIT_REACHED};

// Bitwise flags for the end-key arrays:
#define END_KEY_ENABLED 0x01
#define END_KEY_WITH_SHIFT 0x02
#define END_KEY_WITHOUT_SHIFT 0x04

struct input_type
{
	InputStatusType status;
	UCHAR *EndVK; // A sparse array that indicates which VKs terminate the input.
	UCHAR *EndSC; // A sparse array that indicates which SCs terminate the input.
	vk_type EndingVK; // The hook puts the terminating key into one of these if that's how it was terminated.
	sc_type EndingSC;
	bool EndedBySC;  // Whether the Ending key was one handled by VK or SC.
	bool EndingRequiredShift;  // Whether the key that terminated the input was one that needed the SHIFT key.
	char **match; // Array of strings, each string is a match-phrase which if entered, terminates the input.
	UINT MatchCount; // The number of strings currently in the array.
	UINT MatchCountMax; // The maximum number of strings that the match array can contain.
	#define INPUT_ARRAY_BLOCK_SIZE 1024  // The increment by which the above array expands.
	char *MatchBuf; // The is the buffer whose contents are pointed to by the match array.
	UINT MatchBufSize; // The capacity of the above above buffer.
	bool BackspaceIsUndo;
	bool CaseSensitive;
	bool IgnoreAHKInput; // Whether input from any AHK script is ignored for the purpose of finding a match.
	bool TranscribeModifiedKeys; // Whether the input command will attempt to transcribe modified keys such as ^c.
	bool Visible;
	bool FindAnywhere;
	char *buffer; // Stores the user's actual input.
	int BufferLength; // The current length of what the user entered.
	int BufferLengthMax; // The maximum allowed length of the input.
	input_type::input_type() // A simple constructor to initialize the fields that need it.
		: status(INPUT_OFF), match(NULL), MatchBuf(NULL), MatchBufSize(0), buffer(NULL)
	{}
};


//-------------------------------------------

struct KeyHistoryItem
{
	vk_type vk;
	sc_type sc;
	char event_type; // space=none, i=ignored, s=suppressed, h=hotkey, etc.
	bool key_up;
	float elapsed_time;  // Time since prior key or mouse button, in seconds.
	// It seems better to store the foreground window's title rather than its HWND since keystrokes
	// might result in a window closing (being destroyed), in which case the displayed key history
	// would not be able to display the title at the time the history is displayed, which would
	// be undesirable.
	// To save mem, could point this into a shared buffer instead, but if that buffer were to run
	// out of space (perhaps due to the target window changing frequently), window logging would
	// no longer be possible without adding complexity to the logging function.  Seems best
	// to keep it simple:
	char target_window[100];
};


//-------------------------------------------


LRESULT CALLBACK LowLevelKeybdProc(int code, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam);

HookType RemoveAllHooks();
HookType RemoveKeybdHook();
HookType RemoveMouseHook();
HookType GetActiveHooks();
HookType ChangeHookState(Hotkey *aHK[], int aHK_count, HookType aWhichHook, HookType aWhichHookAlways
	, bool aWarnIfHooksAlreadyInstalled);
void ResetHook(bool aAllModifiersUp = false, HookType aWhichHook = (HOOK_KEYBD | HOOK_MOUSE)
	, bool aResetKVKandKSC = false);

char *GetHookStatus(char *aBuf, size_t aBufSize);

#endif
