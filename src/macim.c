/* Implementation of Inline Input Method for MacOS X.
   Copyright (C) 2004, 2005, 2006, 2007, 2008
    Taiichi Hashimoto <taiichi2@mac.com>.

This file is part of GNU Emacs.

GNU Emacs is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include <config.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "lisp.h"
#include "charset.h"
#include "blockinput.h"

#include "macterm.h"

#ifndef MAC_OSX
#include <alloca.h>
#endif

#ifdef MAC_OSX
#undef mktime
#undef DEBUG
#undef free
#undef malloc
#undef realloc
/* Macros max and min defined in lisp.h conflict with those in
   precompiled header Carbon.h.  */
#undef max
#undef min
#undef init_process
#include <Carbon/Carbon.h>
#undef free
#define free unexec_free
#undef malloc
#define malloc unexec_malloc
#undef realloc
#define realloc unexec_realloc
#undef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#undef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#undef init_process
#define init_process emacs_init_process
/* USE_CARBON_EVENTS determines if the Carbon Event Manager is used to
   obtain events from the event queue.  If set to 0, WaitNextEvent is
   used instead.  */
#define USE_CARBON_EVENTS 1
#else /* not MAC_OSX */
#include <Quickdraw.h>
#include <ToolUtils.h>
#include <Sound.h>
#include <Events.h>
#include <Script.h>
#include <Resources.h>
#include <Fonts.h>
#include <TextUtils.h>
#include <LowMem.h>
#include <Controls.h>
#if defined (__MRC__) || (__MSL__ >= 0x6000)
#include <ControlDefinitions.h>
#endif
#include <Gestalt.h>

#if __profile__
#include <profiler.h>
#endif
#endif /* not MAC_OSX */

#include "systty.h"
#include "systime.h"
#include "atimer.h"
#include "keymap.h"

#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <sys/stat.h>

#include "keyboard.h"
#include "frame.h"
#include "dispextern.h"
#include "fontset.h"
#include "termhooks.h"
#include "termopts.h"
#include "termchar.h"
#include "gnu.h"
#include "disptab.h"
#include "buffer.h"
#include "window.h"
#include "intervals.h"
#include "composite.h"
#include "coding.h"

#ifdef USE_CARBON_EVENTS && defined (MAC_OSX) && USE_TSM

extern Lisp_Object Qcurrent_input_method;
static Lisp_Object Qmac_ignore_shortcut;
int mac_pass_key_to_system_on_read_only_buffer;

void init_input_method ();
int mac_store_change_input_method_event (unsigned long timestamp);

DEFUN ("mac-set-key-script", Fmac_set_key_script,
       Smac_set_key_script, 1, 1, 0,
       doc: /* change languge environment of MacOSX */)
     (code)
     Lisp_Object code;
{
  BLOCK_INPUT;
  KeyScript (XINT (code));
  UNBLOCK_INPUT;

  return Qnil;
}

DEFUN ("mac-get-current-key-script", Fmac_get_current_key_script,
       Smac_get_current_key_script, 0, 0, 0,
       doc: /* get current languge environment of MacOSX */)
     ()
{
  SInt16 current_key_script;

  BLOCK_INPUT;
  current_key_script = GetScriptManagerVariable (smKeyScript);
  UNBLOCK_INPUT;

  return make_number (current_key_script);
}

DEFUN ("mac-get-last-key-script", Fmac_get_last_key_script,
       Smac_get_last_key_script, 0, 0, 0,
       doc: /* get last languge environment of MacOSX */)
     ()
{
  SInt16 last_key_script;
  
  BLOCK_INPUT;
  last_key_script = GetScriptManagerVariable (smLastScript);
  UNBLOCK_INPUT;
  
  return make_number (last_key_script);
}


int
mac_store_change_input_method_event (unsigned long timestamp)
{
  Lisp_Object cim, him, dim;
  static SInt16 last_key_script = -1;
  SInt16 current_key_script, count = 0;

  BLOCK_INPUT;

  dim = Fsymbol_value (intern ("default-input-method"));

  if (STRINGP (dim) && strcmp(SDATA (dim), "MacOSX") == 0) {
    current_key_script = GetScriptManagerVariable (smKeyScript);
    
    cim = Fsymbol_value (Qcurrent_input_method);
    him = Fcar_safe (intern ("input-method-history"));

    if (NILP (cim) || NILP (him) ||
	(STRINGP (cim) && !strcmp(SDATA (cim), "MacOSX"))
	|| (STRINGP (him) && !strcmp(SDATA (him), "MacOSX")))
      {
	if (last_key_script != current_key_script
	    || (current_key_script && NILP (cim))
	    || (!current_key_script && !NILP (cim)))
	  {
	    struct input_event event;
	    
	    EVENT_INIT (event);
	    event.kind = MAC_CHANGE_INPUT_METHOD_EVENT;
	    event.arg = Qnil;
	    event.code = current_key_script;
	    event.timestamp = timestamp;
	    kbd_buffer_store_event (&event);
	    count++;
	  }
      
	last_key_script = current_key_script;
      }
    else
      if (current_key_script) KeyScript (smKeyRoman);
  }

  UNBLOCK_INPUT;

  return count;
}


int
mac_pass_key_to_system (int code, UInt32 modifiers)
{
  Lisp_Object buf = Qnil;
  SInt16 current_key_script;

  buf = Fsymbol_value (intern ("mac-ts-active-input-buf"));
  current_key_script = GetScriptManagerVariable (smKeyScript);
  
  if (current_key_script
      && (this_command_key_count || cursor_in_echo_area))
    return FALSE;
  else if (current_key_script
	   && !mac_pass_key_to_system_on_read_only_buffer
	   && !NILP (current_buffer->read_only)
	   && code != 0x20)
    return FALSE;
  else if (!Flength (buf))
    {
      Fsymbol_value (Qcurrent_input_method);
      Lisp_Object keys = Fsymbol_value (Qmac_ignore_shortcut);
      Lisp_Object m, k;

      while (!NILP (keys))
	{
	  m = XCAR (XCAR (keys));
	  k = XCDR (XCAR (keys));
	  keys = XCDR (keys);
	  
	  if (NUMBERP (m) && modifiers == XINT (m))
	    if (NILP (k)
		|| (NUMBERP (k) && code == XINT (k)))
	      return FALSE;
	}
      return TRUE;
    }
  else
    return TRUE;
}


void init_input_method ()
{
  Qmac_ignore_shortcut = intern ("mac-ignore-shortcut");
  staticpro (&Qmac_ignore_shortcut);

  DEFVAR_BOOL ("mac-pass-key-to-system-on-read-only-buffer",
	       &mac_pass_key_to_system_on_read_only_buffer,
    doc: /* If nil, don't pass key to system when buffer is read-only. */);
  mac_pass_key_to_system_on_read_only_buffer = 0;

  defsubr (&Smac_set_key_script);
  defsubr (&Smac_get_current_key_script);
  defsubr (&Smac_get_last_key_script);

}

#endif

