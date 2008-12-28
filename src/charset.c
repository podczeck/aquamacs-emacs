/* Basic character set support.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007,
     2008  Free Software Foundation, Inc.
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
     2005, 2006, 2007, 2008
     National Institute of Advanced Industrial Science and Technology (AIST)
     Registration Number H14PRO021

   Copyright (C) 2003, 2004
     National Institute of Advanced Industrial Science and Technology (AIST)
     Registration Number H13PRO009

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include "lisp.h"
#include "character.h"
#include "charset.h"
#include "coding.h"
#include "disptab.h"
#include "buffer.h"

/*** GENERAL NOTES on CODED CHARACTER SETS (CHARSETS) ***

  A coded character set ("charset" hereafter) is a meaningful
  collection (i.e. language, culture, functionality, etc.) of
  characters.  Emacs handles multiple charsets at once.  In Emacs Lisp
  code, a charset is represented by a symbol.  In C code, a charset is
  represented by its ID number or by a pointer to a struct charset.

  The actual information about each charset is stored in two places.
  Lispy information is stored in the hash table Vcharset_hash_table as
  a vector (charset attributes).  The other information is stored in
  charset_table as a struct charset.

*/

/* List of all charsets.  This variable is used only from Emacs
   Lisp.  */
Lisp_Object Vcharset_list;

/* Hash table that contains attributes of each charset.  Keys are
   charset symbols, and values are vectors of charset attributes.  */
Lisp_Object Vcharset_hash_table;

/* Table of struct charset.  */
struct charset *charset_table;

static int charset_table_size;
static int charset_table_used;

Lisp_Object Qcharsetp;

/* Special charset symbols.  */
Lisp_Object Qascii;
Lisp_Object Qeight_bit;
Lisp_Object Qiso_8859_1;
Lisp_Object Qunicode;
Lisp_Object Qemacs;

/* The corresponding charsets.  */
int charset_ascii;
int charset_eight_bit;
int charset_iso_8859_1;
int charset_unicode;
int charset_emacs;

/* The other special charsets.  */
int charset_jisx0201_roman;
int charset_jisx0208_1978;
int charset_jisx0208;

/* Value of charset attribute `charset-iso-plane'.  */
Lisp_Object Qgl, Qgr;

/* Charset of unibyte characters.  */
int charset_unibyte;

/* List of charsets ordered by the priority.  */
Lisp_Object Vcharset_ordered_list;

/* Sub-list of Vcharset_ordered_list that contains all non-preferred
   charsets.  */
Lisp_Object Vcharset_non_preferred_head;

/* Incremented everytime we change Vcharset_ordered_list.  This is
   unsigned short so that it fits in Lisp_Int and never matches
   -1.  */
unsigned short charset_ordered_list_tick;

/* List of iso-2022 charsets.  */
Lisp_Object Viso_2022_charset_list;

/* List of emacs-mule charsets.  */
Lisp_Object Vemacs_mule_charset_list;

struct charset *emacs_mule_charset[256];

/* Mapping table from ISO2022's charset (specified by DIMENSION,
   CHARS, and FINAL-CHAR) to Emacs' charset.  */
int iso_charset_table[ISO_MAX_DIMENSION][ISO_MAX_CHARS][ISO_MAX_FINAL];

Lisp_Object Vcharset_map_path;

/* If nonzero, don't load charset maps.  */
int inhibit_load_charset_map;

Lisp_Object Vcurrent_iso639_language;

/* Defined in chartab.c */
extern void
map_char_table_for_charset P_ ((void (*c_function) (Lisp_Object, Lisp_Object),
				Lisp_Object function, Lisp_Object table,
				Lisp_Object arg, struct charset *charset,
				unsigned from, unsigned to));

#define CODE_POINT_TO_INDEX(charset, code)				\
  ((charset)->code_linear_p						\
   ? (code) - (charset)->min_code					\
   : (((charset)->code_space_mask[(code) >> 24] & 0x8)			\
      && ((charset)->code_space_mask[((code) >> 16) & 0xFF] & 0x4)	\
      && ((charset)->code_space_mask[((code) >> 8) & 0xFF] & 0x2)	\
      && ((charset)->code_space_mask[(code) & 0xFF] & 0x1))		\
   ? (((((code) >> 24) - (charset)->code_space[12])			\
       * (charset)->code_space[11])					\
      + (((((code) >> 16) & 0xFF) - (charset)->code_space[8])		\
	 * (charset)->code_space[7])					\
      + (((((code) >> 8) & 0xFF) - (charset)->code_space[4])		\
	 * (charset)->code_space[3])					\
      + (((code) & 0xFF) - (charset)->code_space[0])			\
      - ((charset)->char_index_offset))					\
   : -1)


/* Convert the character index IDX to code-point CODE for CHARSET.
   It is assumed that IDX is in a valid range.  */

#define INDEX_TO_CODE_POINT(charset, idx)				     \
  ((charset)->code_linear_p						     \
   ? (idx) + (charset)->min_code					     \
   : (idx += (charset)->char_index_offset,				     \
      (((charset)->code_space[0] + (idx) % (charset)->code_space[2])	     \
       | (((charset)->code_space[4]					     \
	   + ((idx) / (charset)->code_space[3] % (charset)->code_space[6]))  \
	  << 8)								     \
       | (((charset)->code_space[8]					     \
	   + ((idx) / (charset)->code_space[7] % (charset)->code_space[10])) \
	  << 16)							     \
       | (((charset)->code_space[12] + ((idx) / (charset)->code_space[11]))  \
	  << 24))))

/* Structure to hold mapping tables for a charset.  Used by temacs
   invoked for dumping.  */

static struct
{
  /* The current charset for which the following tables are setup.  */
  struct charset *current;

  /* 1 iff the following table is used for encoder.  */
  short for_encoder;

  /* When the following table is used for encoding, mininum and
     maxinum character of the current charset.  */
  int min_char, max_char;

  /* A Unicode character correspoinding to the code indice 0 (i.e. the
     minimum code-point) of the current charset, or -1 if the code
     indice 0 is not a Unicode character.  This is checked when
     table.encoder[CHAR] is zero.  */
  int zero_index_char;

  union {
    /* Table mapping code-indices (not code-points) of the current
       charset to Unicode characters.  If decoder[CHAR] is -1, CHAR
       doesn't belong to the current charset.  */
    int decoder[0x10000];
    /* Table mapping Unicode characters to code-indices of the current
       charset.  The first 0x10000 elements are for BMP (0..0xFFFF),
       and the last 0x10000 are for SMP (0x10000..0x1FFFF) or SIP
       (0x20000..0x2FFFF).  Note that there is no charset map that
       uses both SMP and SIP.  */
    unsigned short encoder[0x20000];
  } table;
} *temp_charset_work;

#define SET_TEMP_CHARSET_WORK_ENCODER(C, CODE)			\
  do {								\
    if ((CODE) == 0)						\
      temp_charset_work->zero_index_char = (C);			\
    else if ((C) < 0x20000)					\
      temp_charset_work->table.encoder[(C)] = (CODE);		\
    else							\
      temp_charset_work->table.encoder[(C) - 0x10000] = (CODE);	\
  } while (0)

#define GET_TEMP_CHARSET_WORK_ENCODER(C)				  \
  ((C) == temp_charset_work->zero_index_char ? 0			  \
   : (C) < 0x20000 ? (temp_charset_work->table.encoder[(C)]		  \
		      ? (int) temp_charset_work->table.encoder[(C)] : -1) \
   : temp_charset_work->table.encoder[(C) - 0x10000]			  \
   ? temp_charset_work->table.encoder[(C) - 0x10000] : -1)

#define SET_TEMP_CHARSET_WORK_DECODER(C, CODE)	\
  (temp_charset_work->table.decoder[(CODE)] = (C))

#define GET_TEMP_CHARSET_WORK_DECODER(CODE)	\
  (temp_charset_work->table.decoder[(CODE)])


/* Set to 1 to warn that a charset map is loaded and thus a buffer
   text and a string data may be relocated.  */
int charset_map_loaded;

struct charset_map_entries
{
  struct {
    unsigned from, to;
    int c;
  } entry[0x10000];
  struct charset_map_entries *next;
};

/* Load the mapping information of CHARSET from ENTRIES for
   initializing (CONTROL_FLAG == 0), decoding (CONTROL_FLAG == 1), and
   encoding (CONTROL_FLAG == 2).

   If CONTROL_FLAG is 0, setup CHARSET->min_char, CHARSET->max_char,
   and CHARSET->fast_map.

   If CONTROL_FLAG is 1, setup the following tables according to
   CHARSET->method and inhibit_load_charset_map.

   CHARSET->method       | inhibit_lcm == 0   | inhibit_lcm == 1
   ----------------------+--------------------+---------------------------
   CHARSET_METHOD_MAP    | CHARSET->decoder   | temp_charset_work->decoder
   ----------------------+--------------------+---------------------------
   CHARSET_METHOD_OFFSET | Vchar_unify_table  | temp_charset_work->decoder

   If CONTROL_FLAG is 2, setup the following tables.

   CHARSET->method       | inhibit_lcm == 0   | inhibit_lcm == 1
   ----------------------+--------------------+---------------------------
   CHARSET_METHOD_MAP    | CHARSET->encoder   | temp_charset_work->encoder
   ----------------------+--------------------+--------------------------
   CHARSET_METHOD_OFFSET | CHARSET->deunifier | temp_charset_work->encoder
*/

static void
load_charset_map (charset, entries, n_entries, control_flag)
  struct charset *charset;
  struct charset_map_entries *entries;
  int n_entries;
  int control_flag;
{
  Lisp_Object vec, table;
  unsigned max_code = CHARSET_MAX_CODE (charset);
  int ascii_compatible_p = charset->ascii_compatible_p;
  int min_char, max_char, nonascii_min_char;
  int i;
  unsigned char *fast_map = charset->fast_map;

  if (n_entries <= 0)
    return;

  if (control_flag)
    {
      if (! inhibit_load_charset_map)
	{
	  if (control_flag == 1)
	    {
	      if (charset->method == CHARSET_METHOD_MAP)
		{
		  int n = CODE_POINT_TO_INDEX (charset, max_code) + 1;

		  vec = CHARSET_DECODER (charset)
		    = Fmake_vector (make_number (n), make_number (-1));
		}
	      else
		{
		  char_table_set_range (Vchar_unify_table,
					charset->min_char, charset->max_char,
					Qnil);
		}
	    }
	  else
	    {
	      table = Fmake_char_table (Qnil, Qnil);
	      if (charset->method == CHARSET_METHOD_MAP)
		CHARSET_ENCODER (charset) = table;
	      else
		CHARSET_DEUNIFIER (charset) = table;
	    }
	}
      else
	{
	  if (! temp_charset_work)
	    temp_charset_work = malloc (sizeof (*temp_charset_work));
	  if (control_flag == 1)
	    {
	      memset (temp_charset_work->table.decoder, -1,
		      sizeof (int) * 0x10000);
	      temp_charset_work->for_encoder = 0;
	    }
	  else
	    {
	      memset (temp_charset_work->table.encoder, 0,
		      sizeof (unsigned short) * 0x20000);
	      temp_charset_work->zero_index_char = -1;
	    }
	  temp_charset_work->current = charset;
	  temp_charset_work->for_encoder = (control_flag == 2);
	  control_flag += 2;
	}
      charset_map_loaded = 1;
    }

  min_char = max_char = entries->entry[0].c;
  nonascii_min_char = MAX_CHAR;
  for (i = 0; i < n_entries; i++)
    {
      unsigned from, to;
      int from_index, to_index;
      int from_c, to_c;
      int idx = i % 0x10000;

      if (i > 0 && idx == 0)
	entries = entries->next;
      from = entries->entry[idx].from;
      to = entries->entry[idx].to;
      from_c = entries->entry[idx].c;
      from_index = CODE_POINT_TO_INDEX (charset, from);
      if (from == to)
	{
	  to_index = from_index;
	  to_c = from_c;
	}
      else
	{
	  to_index = CODE_POINT_TO_INDEX (charset, to);
	  to_c = from_c + (to_index - from_index);
	}
      if (from_index < 0 || to_index < 0)
	continue;

      if (to_c > max_char)
	max_char = to_c;
      else if (from_c < min_char)
	min_char = from_c;

      if (control_flag == 1)
	{
	  if (charset->method == CHARSET_METHOD_MAP)
	    for (; from_index <= to_index; from_index++, from_c++)
	      ASET (vec, from_index, make_number (from_c));
	  else
	    for (; from_index <= to_index; from_index++, from_c++)
	      CHAR_TABLE_SET (Vchar_unify_table,
			      CHARSET_CODE_OFFSET (charset) + from_index,
			      make_number (from_c));
	}
      else if (control_flag == 2)
	{
	  if (charset->method == CHARSET_METHOD_MAP
	      && CHARSET_COMPACT_CODES_P (charset))
	    for (; from_index <= to_index; from_index++, from_c++)
	      {
		unsigned code = INDEX_TO_CODE_POINT (charset, from_index);

		if (NILP (CHAR_TABLE_REF (table, from_c)))
		  CHAR_TABLE_SET (table, from_c, make_number (code));
	      }
	  else
	    for (; from_index <= to_index; from_index++, from_c++)
	      {
		if (NILP (CHAR_TABLE_REF (table, from_c)))
		  CHAR_TABLE_SET (table, from_c, make_number (from_index));
	      }
	}
      else if (control_flag == 3)
	for (; from_index <= to_index; from_index++, from_c++)
	  SET_TEMP_CHARSET_WORK_DECODER (from_c, from_index);
      else if (control_flag == 4)
	for (; from_index <= to_index; from_index++, from_c++)
	  SET_TEMP_CHARSET_WORK_ENCODER (from_c, from_index);
      else			/* control_flag == 0 */
	{
	  if (ascii_compatible_p)
	    {
	      if (! ASCII_BYTE_P (from_c))
		{
		  if (from_c < nonascii_min_char)
		    nonascii_min_char = from_c;
		}
	      else if (! ASCII_BYTE_P (to_c))
		{
		  nonascii_min_char = 0x80;
		}
	    }

	  for (; from_c <= to_c; from_c++)
	    CHARSET_FAST_MAP_SET (from_c, fast_map);
	}
    }

  if (control_flag == 0)
    {
      CHARSET_MIN_CHAR (charset) = (ascii_compatible_p
				    ? nonascii_min_char : min_char);
      CHARSET_MAX_CHAR (charset) = max_char;
    }
  else if (control_flag == 4)
    {
      temp_charset_work->min_char = min_char;
      temp_charset_work->max_char = max_char;
    }
}


/* Read a hexadecimal number (preceded by "0x") from the file FP while
   paying attention to comment charcter '#'.  */

static INLINE unsigned
read_hex (fp, eof)
     FILE *fp;
     int *eof;
{
  int c;
  unsigned n;

  while ((c = getc (fp)) != EOF)
    {
      if (c == '#')
	{
	  while ((c = getc (fp)) != EOF && c != '\n');
	}
      else if (c == '0')
	{
	  if ((c = getc (fp)) == EOF || c == 'x')
	    break;
	}
    }
  if (c == EOF)
    {
      *eof = 1;
      return 0;
    }
  *eof = 0;
  n = 0;
  if (c == 'x')
    while ((c = getc (fp)) != EOF && isxdigit (c))
      n = ((n << 4)
	   | (c <= '9' ? c - '0' : c <= 'F' ? c - 'A' + 10 : c - 'a' + 10));
  else
    while ((c = getc (fp)) != EOF && isdigit (c))
      n = (n * 10) + c - '0';
  if (c != EOF)
    ungetc (c, fp);
  return n;
}


/* Return a mapping vector for CHARSET loaded from MAPFILE.
   Each line of MAPFILE has this form
	0xAAAA 0xCCCC
   where 0xAAAA is a code-point and 0xCCCC is the corresponding
   character code, or this form
	0xAAAA-0xBBBB 0xCCCC
   where 0xAAAA and 0xBBBB are code-points specifying a range, and
   0xCCCC is the first character code of the range.

   The returned vector has this form:
	[ CODE1 CHAR1 CODE2 CHAR2 .... ]
   where CODE1 is a code-point or a cons of code-points specifying a
   range.  */

extern void add_to_log P_ ((char *, Lisp_Object, Lisp_Object));

static void
load_charset_map_from_file (charset, mapfile, control_flag)
     struct charset *charset;
     Lisp_Object mapfile;
     int control_flag;
{
  unsigned min_code = CHARSET_MIN_CODE (charset);
  unsigned max_code = CHARSET_MAX_CODE (charset);
  int fd;
  FILE *fp;
  int eof;
  Lisp_Object suffixes;
  struct charset_map_entries *head, *entries;
  int n_entries;

  suffixes = Fcons (build_string (".map"),
		    Fcons (build_string (".TXT"), Qnil));

  fd = openp (Vcharset_map_path, mapfile, suffixes, NULL, Qnil);
  if (fd < 0
      || ! (fp = fdopen (fd, "r")))
    {
      add_to_log ("Failure in loading charset map: %S", mapfile, Qnil);
      return;
    }

  head = entries = ((struct charset_map_entries *)
		    alloca (sizeof (struct charset_map_entries)));
  n_entries = 0;
  eof = 0;
  while (1)
    {
      unsigned from, to;
      int c;
      int idx;

      from = read_hex (fp, &eof);
      if (eof)
	break;
      if (getc (fp) == '-')
	to = read_hex (fp, &eof);
      else
	to = from;
      c = (int) read_hex (fp, &eof);

      if (from < min_code || to > max_code || from > to || c > MAX_CHAR)
	continue;

      if (n_entries > 0 && (n_entries % 0x10000) == 0)
	{
	  entries->next = ((struct charset_map_entries *)
			   alloca (sizeof (struct charset_map_entries)));
	  entries = entries->next;
	}
      idx = n_entries % 0x10000;
      entries->entry[idx].from = from;
      entries->entry[idx].to = to;
      entries->entry[idx].c = c;
      n_entries++;
    }
  fclose (fp);
  close (fd);

  load_charset_map (charset, head, n_entries, control_flag);
}

static void
load_charset_map_from_vector (charset, vec, control_flag)
     struct charset *charset;
     Lisp_Object vec;
     int control_flag;
{
  unsigned min_code = CHARSET_MIN_CODE (charset);
  unsigned max_code = CHARSET_MAX_CODE (charset);
  struct charset_map_entries *head, *entries;
  int n_entries;
  int len = ASIZE (vec);
  int i;

  if (len % 2 == 1)
    {
      add_to_log ("Failure in loading charset map: %V", vec, Qnil);
      return;
    }

  head = entries = ((struct charset_map_entries *)
		    alloca (sizeof (struct charset_map_entries)));
  n_entries = 0;
  for (i = 0; i < len; i += 2)
    {
      Lisp_Object val, val2;
      unsigned from, to;
      int c;
      int idx;

      val = AREF (vec, i);
      if (CONSP (val))
	{
	  val2 = XCDR (val);
	  val = XCAR (val);
	  CHECK_NATNUM (val);
	  CHECK_NATNUM (val2);
	  from = XFASTINT (val);
	  to = XFASTINT (val2);
	}
      else
	{
	  CHECK_NATNUM (val);
	  from = to = XFASTINT (val);
	}
      val = AREF (vec, i + 1);
      CHECK_NATNUM (val);
      c = XFASTINT (val);

      if (from < min_code || to > max_code || from > to || c > MAX_CHAR)
	continue;

      if (n_entries > 0 && (n_entries % 0x10000) == 0)
	{
	  entries->next = ((struct charset_map_entries *)
			   alloca (sizeof (struct charset_map_entries)));
	  entries = entries->next;
	}
      idx = n_entries % 0x10000;
      entries->entry[idx].from = from;
      entries->entry[idx].to = to;
      entries->entry[idx].c = c;
      n_entries++;
    }

  load_charset_map (charset, head, n_entries, control_flag);
}


/* Load a mapping table for CHARSET.  CONTROL-FLAG tells what kind of
   map it is (see the comment of load_charset_map for the detail).  */

static void
load_charset (charset, control_flag)
     struct charset *charset;
     int control_flag;
{
  Lisp_Object map;

  if (inhibit_load_charset_map
      && temp_charset_work
      && charset == temp_charset_work->current
      && (control_flag == 2 == temp_charset_work->for_encoder))
    return;

  if (CHARSET_METHOD (charset) == CHARSET_METHOD_MAP)
    map = CHARSET_MAP (charset);
  else if (CHARSET_UNIFIED_P (charset))
    map = CHARSET_UNIFY_MAP (charset);
  if (STRINGP (map))
    load_charset_map_from_file (charset, map, control_flag);
  else
    load_charset_map_from_vector (charset, map, control_flag);
}


DEFUN ("charsetp", Fcharsetp, Scharsetp, 1, 1, 0,
       doc: /* Return non-nil if and only if OBJECT is a charset.*/)
     (object)
     Lisp_Object object;
{
  return (CHARSETP (object) ? Qt : Qnil);
}


void map_charset_for_dump P_ ((void (*c_function) (Lisp_Object, Lisp_Object),
			       Lisp_Object function, Lisp_Object arg,
			       unsigned from, unsigned to));

void
map_charset_for_dump (c_function, function, arg, from, to)
     void (*c_function) (Lisp_Object, Lisp_Object);
     Lisp_Object function, arg;
     unsigned from, to;
{
  int from_idx = CODE_POINT_TO_INDEX (temp_charset_work->current, from);
  int to_idx = CODE_POINT_TO_INDEX (temp_charset_work->current, to);
  Lisp_Object range;
  int c, stop;
  struct gcpro gcpro1;

  range = Fcons (Qnil, Qnil);
  GCPRO1 (range);

  c = temp_charset_work->min_char;
  stop = (temp_charset_work->max_char < 0x20000
	  ? temp_charset_work->max_char : 0xFFFF);
	  
  while (1)
    {
      int index = GET_TEMP_CHARSET_WORK_ENCODER (c);

      if (index >= from_idx && index <= to_idx)
	{
	  if (NILP (XCAR (range)))
	    XSETCAR (range, make_number (c));
	}
      else if (! NILP (XCAR (range)))
	{
	  XSETCDR (range, make_number (c - 1));
	  if (c_function)
	    (*c_function) (arg, range);
	  else
	    call2 (function, range, arg);
	  XSETCAR (range, Qnil);
	}
      if (c == stop)
	{
	  if (c == temp_charset_work->max_char)
	    {
	      if (! NILP (XCAR (range)))
		{
		  XSETCDR (range, make_number (c));
		  if (c_function)
		    (*c_function) (arg, range);
		  else
		    call2 (function, range, arg);
		}
	      break;
	    }
	  c = 0x1FFFF;
	  stop = temp_charset_work->max_char;
	}
      c++;
    }
}

void
map_charset_chars (c_function, function, arg,
		   charset, from, to)
     void (*c_function) P_ ((Lisp_Object, Lisp_Object));
     Lisp_Object function, arg;
     struct charset *charset;
     unsigned from, to;
{
  Lisp_Object range;
  int partial;

  partial = (from > CHARSET_MIN_CODE (charset)
	     || to < CHARSET_MAX_CODE (charset));

  if (CHARSET_METHOD (charset) == CHARSET_METHOD_OFFSET)
    {
      int from_idx = CODE_POINT_TO_INDEX (charset, from);
      int to_idx = CODE_POINT_TO_INDEX (charset, to);
      int from_c = from_idx + CHARSET_CODE_OFFSET (charset);
      int to_c = to_idx + CHARSET_CODE_OFFSET (charset);

      if (CHARSET_UNIFIED_P (charset))
	{
	  if (! CHAR_TABLE_P (CHARSET_DEUNIFIER (charset)))
	    load_charset (charset, 2);
	  if (CHAR_TABLE_P (CHARSET_DEUNIFIER (charset)))
	    map_char_table_for_charset (c_function, function,
					CHARSET_DEUNIFIER (charset), arg,
					partial ? charset : NULL, from, to);
	  else
	    map_charset_for_dump (c_function, function, arg, from, to);
	}

      range = Fcons (make_number (from_c), make_number (to_c));
      if (NILP (function))
	(*c_function) (arg, range);
      else
	call2 (function, range, arg);
    }
  else if (CHARSET_METHOD (charset) == CHARSET_METHOD_MAP)
    {
      if (! CHAR_TABLE_P (CHARSET_ENCODER (charset)))
	load_charset (charset, 2);
      if (CHAR_TABLE_P (CHARSET_ENCODER (charset)))
	map_char_table_for_charset (c_function, function,
				    CHARSET_ENCODER (charset), arg,
				    partial ? charset : NULL, from, to);
      else
	map_charset_for_dump (c_function, function, arg, from, to);
    }
  else if (CHARSET_METHOD (charset) == CHARSET_METHOD_SUBSET)
    {
      Lisp_Object subset_info;
      int offset;

      subset_info = CHARSET_SUBSET (charset);
      charset = CHARSET_FROM_ID (XFASTINT (AREF (subset_info, 0)));
      offset = XINT (AREF (subset_info, 3));
      from -= offset;
      if (from < XFASTINT (AREF (subset_info, 1)))
	from = XFASTINT (AREF (subset_info, 1));
      to -= offset;
      if (to > XFASTINT (AREF (subset_info, 2)))
	to = XFASTINT (AREF (subset_info, 2));
      map_charset_chars (c_function, function, arg, charset, from, to);
    }
  else				/* i.e. CHARSET_METHOD_SUPERSET */
    {
      Lisp_Object parents;

      for (parents = CHARSET_SUPERSET (charset); CONSP (parents);
	   parents = XCDR (parents))
	{
	  int offset;
	  unsigned this_from, this_to;

	  charset = CHARSET_FROM_ID (XFASTINT (XCAR (XCAR (parents))));
	  offset = XINT (XCDR (XCAR (parents)));
	  this_from = from - offset;
	  this_to = to - offset;
	  if (this_from < CHARSET_MIN_CODE (charset))
	    this_from = CHARSET_MIN_CODE (charset);
	  if (this_to > CHARSET_MAX_CODE (charset))
	    this_to = CHARSET_MAX_CODE (charset);
	  map_charset_chars (c_function, function, arg, charset,
			     this_from, this_to);
	}
    }
}

DEFUN ("map-charset-chars", Fmap_charset_chars, Smap_charset_chars, 2, 5, 0,
       doc: /* Call FUNCTION for all characters in CHARSET.
FUNCTION is called with an argument RANGE and the optional 3rd
argument ARG.

RANGE is a cons (FROM .  TO), where FROM and TO indicate a range of
characters contained in CHARSET.

The optional 4th and 5th arguments FROM-CODE and TO-CODE specify the
range of code points of target characters.  */)
     (function, charset, arg, from_code, to_code)
       Lisp_Object function, charset, arg, from_code, to_code;
{
  struct charset *cs;
  unsigned from, to;

  CHECK_CHARSET_GET_CHARSET (charset, cs);
  if (NILP (from_code))
    from = CHARSET_MIN_CODE (cs);
  else
    {
      CHECK_NATNUM (from_code);
      from = XINT (from_code);
      if (from < CHARSET_MIN_CODE (cs))
	from = CHARSET_MIN_CODE (cs);
    }
  if (NILP (to_code))
    to = CHARSET_MAX_CODE (cs);
  else
    {
      CHECK_NATNUM (to_code);
      to = XINT (to_code);
      if (to > CHARSET_MAX_CODE (cs))
	to = CHARSET_MAX_CODE (cs);
    }
  map_charset_chars (NULL, function, arg, cs, from, to);
  return Qnil;
}


/* Define a charset according to the arguments.  The Nth argument is
   the Nth attribute of the charset (the last attribute `charset-id'
   is not included).  See the docstring of `define-charset' for the
   detail.  */

DEFUN ("define-charset-internal", Fdefine_charset_internal,
       Sdefine_charset_internal, charset_arg_max, MANY, 0,
       doc: /* For internal use only.
usage: (define-charset-internal ...)  */)
     (nargs, args)
     int nargs;
     Lisp_Object *args;
{
  /* Charset attr vector.  */
  Lisp_Object attrs;
  Lisp_Object val;
  unsigned hash_code;
  struct Lisp_Hash_Table *hash_table = XHASH_TABLE (Vcharset_hash_table);
  int i, j;
  struct charset charset;
  int id;
  int dimension;
  int new_definition_p;
  int nchars;

  if (nargs != charset_arg_max)
    return Fsignal (Qwrong_number_of_arguments,
		    Fcons (intern ("define-charset-internal"),
			   make_number (nargs)));

  attrs = Fmake_vector (make_number (charset_attr_max), Qnil);

  CHECK_SYMBOL (args[charset_arg_name]);
  ASET (attrs, charset_name, args[charset_arg_name]);

  val = args[charset_arg_code_space];
  for (i = 0, dimension = 0, nchars = 1; i < 4; i++)
    {
      int min_byte, max_byte;

      min_byte = XINT (Faref (val, make_number (i * 2)));
      max_byte = XINT (Faref (val, make_number (i * 2 + 1)));
      if (min_byte < 0 || min_byte > max_byte || max_byte >= 256)
	error ("Invalid :code-space value");
      charset.code_space[i * 4] = min_byte;
      charset.code_space[i * 4 + 1] = max_byte;
      charset.code_space[i * 4 + 2] = max_byte - min_byte + 1;
      nchars *= charset.code_space[i * 4 + 2];
      charset.code_space[i * 4 + 3] = nchars;
      if (max_byte > 0)
	dimension = i + 1;
    }

  val = args[charset_arg_dimension];
  if (NILP (val))
    charset.dimension = dimension;
  else
    {
      CHECK_NATNUM (val);
      charset.dimension = XINT (val);
      if (charset.dimension < 1 || charset.dimension > 4)
	args_out_of_range_3 (val, make_number (1), make_number (4));
    }

  charset.code_linear_p
    = (charset.dimension == 1
       || (charset.code_space[2] == 256
	   && (charset.dimension == 2
	       || (charset.code_space[6] == 256
		   && (charset.dimension == 3
		       || charset.code_space[10] == 256)))));

  if (! charset.code_linear_p)
    {
      charset.code_space_mask = (unsigned char *) xmalloc (256);
      bzero (charset.code_space_mask, 256);
      for (i = 0; i < 4; i++)
	for (j = charset.code_space[i * 4]; j <= charset.code_space[i * 4 + 1];
	     j++)
	  charset.code_space_mask[j] |= (1 << i);
    }

  charset.iso_chars_96 = charset.code_space[2] == 96;

  charset.min_code = (charset.code_space[0]
		      | (charset.code_space[4] << 8)
		      | (charset.code_space[8] << 16)
		      | (charset.code_space[12] << 24));
  charset.max_code = (charset.code_space[1]
		      | (charset.code_space[5] << 8)
		      | (charset.code_space[9] << 16)
		      | (charset.code_space[13] << 24));
  charset.char_index_offset = 0;

  val = args[charset_arg_min_code];
  if (! NILP (val))
    {
      unsigned code;

      if (INTEGERP (val))
	code = XINT (val);
      else
	{
	  CHECK_CONS (val);
	  CHECK_NUMBER_CAR (val);
	  CHECK_NUMBER_CDR (val);
	  code = (XINT (XCAR (val)) << 16) | (XINT (XCDR (val)));
	}
      if (code < charset.min_code
	  || code > charset.max_code)
	args_out_of_range_3 (make_number (charset.min_code),
			     make_number (charset.max_code), val);
      charset.char_index_offset = CODE_POINT_TO_INDEX (&charset, code);
      charset.min_code = code;
    }

  val = args[charset_arg_max_code];
  if (! NILP (val))
    {
      unsigned code;

      if (INTEGERP (val))
	code = XINT (val);
      else
	{
	  CHECK_CONS (val);
	  CHECK_NUMBER_CAR (val);
	  CHECK_NUMBER_CDR (val);
	  code = (XINT (XCAR (val)) << 16) | (XINT (XCDR (val)));
	}
      if (code < charset.min_code
	  || code > charset.max_code)
	args_out_of_range_3 (make_number (charset.min_code),
			     make_number (charset.max_code), val);
      charset.max_code = code;
    }

  charset.compact_codes_p = charset.max_code < 0x10000;

  val = args[charset_arg_invalid_code];
  if (NILP (val))
    {
      if (charset.min_code > 0)
	charset.invalid_code = 0;
      else
	{
	  XSETINT (val, charset.max_code + 1);
	  if (XINT (val) == charset.max_code + 1)
	    charset.invalid_code = charset.max_code + 1;
	  else
	    error ("Attribute :invalid-code must be specified");
	}
    }
  else
    {
      CHECK_NATNUM (val);
      charset.invalid_code = XFASTINT (val);
    }

  val = args[charset_arg_iso_final];
  if (NILP (val))
    charset.iso_final = -1;
  else
    {
      CHECK_NUMBER (val);
      if (XINT (val) < '0' || XINT (val) > 127)
	error ("Invalid iso-final-char: %d", XINT (val));
      charset.iso_final = XINT (val);
    }

  val = args[charset_arg_iso_revision];
  if (NILP (val))
    charset.iso_revision = -1;
  else
    {
      CHECK_NUMBER (val);
      if (XINT (val) > 63)
	args_out_of_range (make_number (63), val);
      charset.iso_revision = XINT (val);
    }

  val = args[charset_arg_emacs_mule_id];
  if (NILP (val))
    charset.emacs_mule_id = -1;
  else
    {
      CHECK_NATNUM (val);
      if ((XINT (val) > 0 && XINT (val) <= 128) || XINT (val) >= 256)
	error ("Invalid emacs-mule-id: %d", XINT (val));
      charset.emacs_mule_id = XINT (val);
    }

  charset.ascii_compatible_p = ! NILP (args[charset_arg_ascii_compatible_p]);

  charset.supplementary_p = ! NILP (args[charset_arg_supplementary_p]);

  charset.unified_p = 0;

  bzero (charset.fast_map, sizeof (charset.fast_map));

  if (! NILP (args[charset_arg_code_offset]))
    {
      val = args[charset_arg_code_offset];
      CHECK_NUMBER (val);

      charset.method = CHARSET_METHOD_OFFSET;
      charset.code_offset = XINT (val);

      i = CODE_POINT_TO_INDEX (&charset, charset.min_code);
      charset.min_char = i + charset.code_offset;
      i = CODE_POINT_TO_INDEX (&charset, charset.max_code);
      charset.max_char = i + charset.code_offset;
      if (charset.max_char > MAX_CHAR)
	error ("Unsupported max char: %d", charset.max_char);

      i = (charset.min_char >> 7) << 7;
      for (; i < 0x10000 && i <= charset.max_char; i += 128)
	CHARSET_FAST_MAP_SET (i, charset.fast_map);
      i = (i >> 12) << 12;
      for (; i <= charset.max_char; i += 0x1000)
	CHARSET_FAST_MAP_SET (i, charset.fast_map);
    }
  else if (! NILP (args[charset_arg_map]))
    {
      val = args[charset_arg_map];
      ASET (attrs, charset_map, val);
      charset.method = CHARSET_METHOD_MAP;
    }
  else if (! NILP (args[charset_arg_subset]))
    {
      Lisp_Object parent;
      Lisp_Object parent_min_code, parent_max_code, parent_code_offset;
      struct charset *parent_charset;

      val = args[charset_arg_subset];
      parent = Fcar (val);
      CHECK_CHARSET_GET_CHARSET (parent, parent_charset);
      parent_min_code = Fnth (make_number (1), val);
      CHECK_NATNUM (parent_min_code);
      parent_max_code = Fnth (make_number (2), val);
      CHECK_NATNUM (parent_max_code);
      parent_code_offset = Fnth (make_number (3), val);
      CHECK_NUMBER (parent_code_offset);
      val = Fmake_vector (make_number (4), Qnil);
      ASET (val, 0, make_number (parent_charset->id));
      ASET (val, 1, parent_min_code);
      ASET (val, 2, parent_max_code);
      ASET (val, 3, parent_code_offset);
      ASET (attrs, charset_subset, val);

      charset.method = CHARSET_METHOD_SUBSET;
      /* Here, we just copy the parent's fast_map.  It's not accurate,
	 but at least it works for quickly detecting which character
	 DOESN'T belong to this charset.  */
      for (i = 0; i < 190; i++)
	charset.fast_map[i] = parent_charset->fast_map[i];

      /* We also copy these for parents.  */
      charset.min_char = parent_charset->min_char;
      charset.max_char = parent_charset->max_char;
    }
  else if (! NILP (args[charset_arg_superset]))
    {
      val = args[charset_arg_superset];
      charset.method = CHARSET_METHOD_SUPERSET;
      val = Fcopy_sequence (val);
      ASET (attrs, charset_superset, val);

      charset.min_char = MAX_CHAR;
      charset.max_char = 0;
      for (; ! NILP (val); val = Fcdr (val))
	{
	  Lisp_Object elt, car_part, cdr_part;
	  int this_id, offset;
	  struct charset *this_charset;

	  elt = Fcar (val);
	  if (CONSP (elt))
	    {
	      car_part = XCAR (elt);
	      cdr_part = XCDR (elt);
	      CHECK_CHARSET_GET_ID (car_part, this_id);
	      CHECK_NUMBER (cdr_part);
	      offset = XINT (cdr_part);
	    }
	  else
	    {
	      CHECK_CHARSET_GET_ID (elt, this_id);
	      offset = 0;
	    }
	  XSETCAR (val, Fcons (make_number (this_id), make_number (offset)));

	  this_charset = CHARSET_FROM_ID (this_id);
	  if (charset.min_char > this_charset->min_char)
	    charset.min_char = this_charset->min_char;
	  if (charset.max_char < this_charset->max_char)
	    charset.max_char = this_charset->max_char;
	  for (i = 0; i < 190; i++)
	    charset.fast_map[i] |= this_charset->fast_map[i];
	}
    }
  else
    error ("None of :code-offset, :map, :parents are specified");

  val = args[charset_arg_unify_map];
  if (! NILP (val) && !STRINGP (val))
    CHECK_VECTOR (val);
  ASET (attrs, charset_unify_map, val);

  CHECK_LIST (args[charset_arg_plist]);
  ASET (attrs, charset_plist, args[charset_arg_plist]);

  charset.hash_index = hash_lookup (hash_table, args[charset_arg_name],
				    &hash_code);
  if (charset.hash_index >= 0)
    {
      new_definition_p = 0;
      id = XFASTINT (CHARSET_SYMBOL_ID (args[charset_arg_name]));
      HASH_VALUE (hash_table, charset.hash_index) = attrs;
    }
  else
    {
      charset.hash_index = hash_put (hash_table, args[charset_arg_name], attrs,
				     hash_code);
      if (charset_table_used == charset_table_size)
	{
	  struct charset *new_table
	    = (struct charset *) xmalloc (sizeof (struct charset)
					  * (charset_table_size + 16));
	  bcopy (charset_table, new_table,
		 sizeof (struct charset) * charset_table_size);
	  charset_table_size += 16;
	  charset_table = new_table;
	}
      id = charset_table_used++;
      new_definition_p = 1;
    }

  ASET (attrs, charset_id, make_number (id));
  charset.id = id;
  charset_table[id] = charset;

  if (charset.method == CHARSET_METHOD_MAP)
    {
      load_charset (&charset, 0);
      charset_table[id] = charset;
    }

  if (charset.iso_final >= 0)
    {
      ISO_CHARSET_TABLE (charset.dimension, charset.iso_chars_96,
			 charset.iso_final) = id;
      if (new_definition_p)
	Viso_2022_charset_list = nconc2 (Viso_2022_charset_list,
					 Fcons (make_number (id), Qnil));
      if (ISO_CHARSET_TABLE (1, 0, 'J') == id)
	charset_jisx0201_roman = id;
      else if (ISO_CHARSET_TABLE (2, 0, '@') == id)
	charset_jisx0208_1978 = id;
      else if (ISO_CHARSET_TABLE (2, 0, 'B') == id)
	charset_jisx0208 = id;
    }

  if (charset.emacs_mule_id >= 0)
    {
      emacs_mule_charset[charset.emacs_mule_id] = CHARSET_FROM_ID (id);
      if (charset.emacs_mule_id < 0xA0)
	emacs_mule_bytes[charset.emacs_mule_id] = charset.dimension + 1;
      else
	emacs_mule_bytes[charset.emacs_mule_id] = charset.dimension + 2;
      if (new_definition_p)
	Vemacs_mule_charset_list = nconc2 (Vemacs_mule_charset_list,
					   Fcons (make_number (id), Qnil));
    }

  if (new_definition_p)
    {
      Vcharset_list = Fcons (args[charset_arg_name], Vcharset_list);
      if (charset.supplementary_p)
	Vcharset_ordered_list = nconc2 (Vcharset_ordered_list,
					Fcons (make_number (id), Qnil));
      else
	{
	  Lisp_Object tail;

	  for (tail = Vcharset_ordered_list; CONSP (tail); tail = XCDR (tail))
	    {
	      struct charset *cs = CHARSET_FROM_ID (XINT (XCAR (tail)));

	      if (cs->supplementary_p)
		break;
	    }
	  if (EQ (tail, Vcharset_ordered_list))
	    Vcharset_ordered_list = Fcons (make_number (id),
					   Vcharset_ordered_list);
	  else if (NILP (tail))
	    Vcharset_ordered_list = nconc2 (Vcharset_ordered_list,
					    Fcons (make_number (id), Qnil));
	  else
	    {
	      val = Fcons (XCAR (tail), XCDR (tail));
	      XSETCDR (tail, val);
	      XSETCAR (tail, make_number (id));
	    }
	}
      charset_ordered_list_tick++;
    }

  return Qnil;
}


/* Same as Fdefine_charset_internal but arguments are more convenient
   to call from C (typically in syms_of_charset).  This can define a
   charset of `offset' method only.  Return the ID of the new
   charset.  */

static int
define_charset_internal (name, dimension, code_space, min_code, max_code,
			 iso_final, iso_revision, emacs_mule_id,
			 ascii_compatible, supplementary,
			 code_offset)
     Lisp_Object name;
     int dimension;
     unsigned char *code_space;
     unsigned min_code, max_code;
     int iso_final, iso_revision, emacs_mule_id;
     int ascii_compatible, supplementary;
     int code_offset;
{
  Lisp_Object args[charset_arg_max];
  Lisp_Object plist[14];
  Lisp_Object val;
  int i;

  args[charset_arg_name] = name;
  args[charset_arg_dimension] = make_number (dimension);
  val = Fmake_vector (make_number (8), make_number (0));
  for (i = 0; i < 8; i++)
    ASET (val, i, make_number (code_space[i]));
  args[charset_arg_code_space] = val;
  args[charset_arg_min_code] = make_number (min_code);
  args[charset_arg_max_code] = make_number (max_code);
  args[charset_arg_iso_final]
    = (iso_final < 0 ? Qnil : make_number (iso_final));
  args[charset_arg_iso_revision] = make_number (iso_revision);
  args[charset_arg_emacs_mule_id]
    = (emacs_mule_id < 0 ? Qnil : make_number (emacs_mule_id));
  args[charset_arg_ascii_compatible_p] = ascii_compatible ? Qt : Qnil;
  args[charset_arg_supplementary_p] = supplementary ? Qt : Qnil;
  args[charset_arg_invalid_code] = Qnil;
  args[charset_arg_code_offset] = make_number (code_offset);
  args[charset_arg_map] = Qnil;
  args[charset_arg_subset] = Qnil;
  args[charset_arg_superset] = Qnil;
  args[charset_arg_unify_map] = Qnil;

  plist[0] = intern (":name");
  plist[1] = args[charset_arg_name];
  plist[2] = intern (":dimension");
  plist[3] = args[charset_arg_dimension];
  plist[4] = intern (":code-space");
  plist[5] = args[charset_arg_code_space];
  plist[6] = intern (":iso-final-char");
  plist[7] = args[charset_arg_iso_final];
  plist[8] = intern (":emacs-mule-id");
  plist[9] = args[charset_arg_emacs_mule_id];
  plist[10] = intern (":ascii-compatible-p");
  plist[11] = args[charset_arg_ascii_compatible_p];
  plist[12] = intern (":code-offset");
  plist[13] = args[charset_arg_code_offset];

  args[charset_arg_plist] = Flist (14, plist);
  Fdefine_charset_internal (charset_arg_max, args);

  return XINT (CHARSET_SYMBOL_ID (name));
}


DEFUN ("define-charset-alias", Fdefine_charset_alias,
       Sdefine_charset_alias, 2, 2, 0,
       doc: /* Define ALIAS as an alias for charset CHARSET.  */)
     (alias, charset)
     Lisp_Object alias, charset;
{
  Lisp_Object attr;

  CHECK_CHARSET_GET_ATTR (charset, attr);
  Fputhash (alias, attr, Vcharset_hash_table);
  Vcharset_list = Fcons (alias, Vcharset_list);
  return Qnil;
}


DEFUN ("charset-plist", Fcharset_plist, Scharset_plist, 1, 1, 0,
       doc: /* Return the property list of CHARSET.  */)
     (charset)
     Lisp_Object charset;
{
  Lisp_Object attrs;

  CHECK_CHARSET_GET_ATTR (charset, attrs);
  return CHARSET_ATTR_PLIST (attrs);
}


DEFUN ("set-charset-plist", Fset_charset_plist, Sset_charset_plist, 2, 2, 0,
       doc: /* Set CHARSET's property list to PLIST.  */)
     (charset, plist)
     Lisp_Object charset, plist;
{
  Lisp_Object attrs;

  CHECK_CHARSET_GET_ATTR (charset, attrs);
  CHARSET_ATTR_PLIST (attrs) = plist;
  return plist;
}


DEFUN ("unify-charset", Funify_charset, Sunify_charset, 1, 3, 0,
       doc: /* Unify characters of CHARSET with Unicode.
This means reading the relevant file and installing the table defined
by CHARSET's `:unify-map' property.

Optional second arg UNIFY-MAP is a file name string or a vector.  It has
the same meaning as the `:unify-map' attribute in the function
`define-charset' (which see).

Optional third argument DEUNIFY, if non-nil, means to de-unify CHARSET.  */)
     (charset, unify_map, deunify)
     Lisp_Object charset, unify_map, deunify;
{
  int id;
  struct charset *cs;

  CHECK_CHARSET_GET_ID (charset, id);
  cs = CHARSET_FROM_ID (id);
  if (NILP (deunify)
      ? CHARSET_UNIFIED_P (cs) && ! NILP (CHARSET_DEUNIFIER (cs))
      : ! CHARSET_UNIFIED_P (cs))
    return Qnil;

  CHARSET_UNIFIED_P (cs) = 0;
  if (NILP (deunify))
    {
      if (CHARSET_METHOD (cs) != CHARSET_METHOD_OFFSET
	  || CHARSET_CODE_OFFSET (cs) < 0x110000)
	error ("Can't unify charset: %s", SDATA (SYMBOL_NAME (charset)));
      if (NILP (unify_map))
	unify_map = CHARSET_UNIFY_MAP (cs);
      else
	{
	  if (! STRINGP (unify_map) && ! VECTORP (unify_map))
	    signal_error ("Bad unify-map", unify_map);
	  CHARSET_UNIFY_MAP (cs) = unify_map;
	}
      if (NILP (Vchar_unify_table))
	Vchar_unify_table = Fmake_char_table (Qnil, Qnil);
      char_table_set_range (Vchar_unify_table,
			    cs->min_char, cs->max_char, charset);
      CHARSET_UNIFIED_P (cs) = 1;
    }
  else if (CHAR_TABLE_P (Vchar_unify_table))
    {
      int min_code = CHARSET_MIN_CODE (cs);
      int max_code = CHARSET_MAX_CODE (cs);
      int min_char = DECODE_CHAR (cs, min_code);
      int max_char = DECODE_CHAR (cs, max_code);

      char_table_set_range (Vchar_unify_table, min_char, max_char, Qnil);
    }

  return Qnil;
}

DEFUN ("get-unused-iso-final-char", Fget_unused_iso_final_char,
       Sget_unused_iso_final_char, 2, 2, 0,
       doc: /*
Return an unused ISO final char for a charset of DIMENSION and CHARS.
DIMENSION is the number of bytes to represent a character: 1 or 2.
CHARS is the number of characters in a dimension: 94 or 96.

This final char is for private use, thus the range is `0' (48) .. `?' (63).
If there's no unused final char for the specified kind of charset,
return nil.  */)
     (dimension, chars)
     Lisp_Object dimension, chars;
{
  int final_char;

  CHECK_NUMBER (dimension);
  CHECK_NUMBER (chars);
  if (XINT (dimension) != 1 && XINT (dimension) != 2 && XINT (dimension) != 3)
    args_out_of_range_3 (dimension, make_number (1), make_number (3));
  if (XINT (chars) != 94 && XINT (chars) != 96)
    args_out_of_range_3 (chars, make_number (94), make_number (96));
  for (final_char = '0'; final_char <= '?'; final_char++)
    if (ISO_CHARSET_TABLE (XINT (dimension), XINT (chars), final_char) < 0)
      break;
  return (final_char <= '?' ? make_number (final_char) : Qnil);
}

static void
check_iso_charset_parameter (dimension, chars, final_char)
     Lisp_Object dimension, chars, final_char;
{
  CHECK_NATNUM (dimension);
  CHECK_NATNUM (chars);
  CHECK_NATNUM (final_char);

  if (XINT (dimension) > 3)
    error ("Invalid DIMENSION %d, it should be 1, 2, or 3", XINT (dimension));
  if (XINT (chars) != 94 && XINT (chars) != 96)
    error ("Invalid CHARS %d, it should be 94 or 96", XINT (chars));
  if (XINT (final_char) < '0' || XINT (final_char) > '~')
    error ("Invalid FINAL-CHAR %c, it should be `0'..`~'", XINT (chars));
}


DEFUN ("declare-equiv-charset", Fdeclare_equiv_charset, Sdeclare_equiv_charset,
       4, 4, 0,
       doc: /* Declare an equivalent charset for ISO-2022 decoding.

On decoding by an ISO-2022 base coding system, when a charset
specified by DIMENSION, CHARS, and FINAL-CHAR is designated, behave as
if CHARSET is designated instead.  */)
     (dimension, chars, final_char, charset)
     Lisp_Object dimension, chars, final_char, charset;
{
  int id;
  int chars_flag;

  CHECK_CHARSET_GET_ID (charset, id);
  check_iso_charset_parameter (dimension, chars, final_char);
  chars_flag = XINT (chars) == 96;
  ISO_CHARSET_TABLE (XINT (dimension), chars_flag, XINT (final_char)) = id;
  return Qnil;
}


/* Return information about charsets in the text at PTR of NBYTES
   bytes, which are NCHARS characters.  The value is:

	0: Each character is represented by one byte.  This is always
	   true for a unibyte string.  For a multibyte string, true if
	   it contains only ASCII characters.

	1: No charsets other than ascii, control-1, and latin-1 are
	   found.

	2: Otherwise.
*/

int
string_xstring_p (string)
     Lisp_Object string;
{
  const unsigned char *p = SDATA (string);
  const unsigned char *endp = p + SBYTES (string);

  if (SCHARS (string) == SBYTES (string))
    return 0;

  while (p < endp)
    {
      int c = STRING_CHAR_ADVANCE (p);

      if (c >= 0x100)
	return 2;
    }
  return 1;
}


/* Find charsets in the string at PTR of NCHARS and NBYTES.

   CHARSETS is a vector.  If Nth element is non-nil, it means the
   charset whose id is N is already found.

   It may lookup a translation table TABLE if supplied.  */

static void
find_charsets_in_text (ptr, nchars, nbytes, charsets, table, multibyte)
     const unsigned char *ptr;
     EMACS_INT nchars, nbytes;
     Lisp_Object charsets, table;
     int multibyte;
{
  const unsigned char *pend = ptr + nbytes;

  if (nchars == nbytes)
    {
      if (multibyte)
	ASET (charsets, charset_ascii, Qt);
      else
	while (ptr < pend)
	  {
	    int c = *ptr++;

	    if (!NILP (table))
	      c = translate_char (table, c);
	    if (ASCII_BYTE_P (c))
	      ASET (charsets, charset_ascii, Qt);
	    else
	      ASET (charsets, charset_eight_bit, Qt);
	  }
    }
  else
    {
      while (ptr < pend)
	{
	  int c = STRING_CHAR_ADVANCE (ptr);
	  struct charset *charset;

	  if (!NILP (table))
	    c = translate_char (table, c);
	  charset = CHAR_CHARSET (c);
	  ASET (charsets, CHARSET_ID (charset), Qt);
	}
    }
}

DEFUN ("find-charset-region", Ffind_charset_region, Sfind_charset_region,
       2, 3, 0,
       doc: /* Return a list of charsets in the region between BEG and END.
BEG and END are buffer positions.
Optional arg TABLE if non-nil is a translation table to look up.

If the current buffer is unibyte, the returned list may contain
only `ascii', `eight-bit-control', and `eight-bit-graphic'.  */)
     (beg, end, table)
     Lisp_Object beg, end, table;
{
  Lisp_Object charsets;
  EMACS_INT from, from_byte, to, stop, stop_byte;
  int i;
  Lisp_Object val;
  int multibyte = ! NILP (current_buffer->enable_multibyte_characters);

  validate_region (&beg, &end);
  from = XFASTINT (beg);
  stop = to = XFASTINT (end);

  if (from < GPT && GPT < to)
    {
      stop = GPT;
      stop_byte = GPT_BYTE;
    }
  else
    stop_byte = CHAR_TO_BYTE (stop);

  from_byte = CHAR_TO_BYTE (from);

  charsets = Fmake_vector (make_number (charset_table_used), Qnil);
  while (1)
    {
      find_charsets_in_text (BYTE_POS_ADDR (from_byte), stop - from,
			     stop_byte - from_byte, charsets, table,
			     multibyte);
      if (stop < to)
	{
	  from = stop, from_byte = stop_byte;
	  stop = to, stop_byte = CHAR_TO_BYTE (stop);
	}
      else
	break;
    }

  val = Qnil;
  for (i = charset_table_used - 1; i >= 0; i--)
    if (!NILP (AREF (charsets, i)))
      val = Fcons (CHARSET_NAME (charset_table + i), val);
  return val;
}

DEFUN ("find-charset-string", Ffind_charset_string, Sfind_charset_string,
       1, 2, 0,
       doc: /* Return a list of charsets in STR.
Optional arg TABLE if non-nil is a translation table to look up.

If STR is unibyte, the returned list may contain
only `ascii', `eight-bit-control', and `eight-bit-graphic'. */)
     (str, table)
     Lisp_Object str, table;
{
  Lisp_Object charsets;
  int i;
  Lisp_Object val;

  CHECK_STRING (str);

  charsets = Fmake_vector (make_number (charset_table_used), Qnil);
  find_charsets_in_text (SDATA (str), SCHARS (str), SBYTES (str),
			 charsets, table,
			 STRING_MULTIBYTE (str));
  val = Qnil;
  for (i = charset_table_used - 1; i >= 0; i--)
    if (!NILP (AREF (charsets, i)))
      val = Fcons (CHARSET_NAME (charset_table + i), val);
  return val;
}



/* Return a unified character code for C (>= 0x110000).  VAL is a
   value of Vchar_unify_table for C; i.e. it is nil, an integer, or a
   charset symbol.  */
int
maybe_unify_char (c, val)
     int c;
     Lisp_Object val;
{
  struct charset *charset;

  if (INTEGERP (val))
    return XINT (val);
  if (NILP (val))
    return c;

  CHECK_CHARSET_GET_CHARSET (val, charset);
  load_charset (charset, 1);
  if (! inhibit_load_charset_map)
    {
      val = CHAR_TABLE_REF (Vchar_unify_table, c);
      if (! NILP (val))
	c = XINT (val);
    }
  else
    {
      int code_index = c - CHARSET_CODE_OFFSET (charset);
      int unified = GET_TEMP_CHARSET_WORK_DECODER (code_index);

      if (unified > 0)
	c = unified;
    }
  return c;
}


/* Return a character correponding to the code-point CODE of
   CHARSET.  */

int
decode_char (charset, code)
     struct charset *charset;
     unsigned code;
{
  int c, char_index;
  enum charset_method method = CHARSET_METHOD (charset);

  if (code < CHARSET_MIN_CODE (charset) || code > CHARSET_MAX_CODE (charset))
    return -1;

  if (method == CHARSET_METHOD_SUBSET)
    {
      Lisp_Object subset_info;

      subset_info = CHARSET_SUBSET (charset);
      charset = CHARSET_FROM_ID (XFASTINT (AREF (subset_info, 0)));
      code -= XINT (AREF (subset_info, 3));
      if (code < XFASTINT (AREF (subset_info, 1))
	  || code > XFASTINT (AREF (subset_info, 2)))
	c = -1;
      else
	c = DECODE_CHAR (charset, code);
    }
  else if (method == CHARSET_METHOD_SUPERSET)
    {
      Lisp_Object parents;

      parents = CHARSET_SUPERSET (charset);
      c = -1;
      for (; CONSP (parents); parents = XCDR (parents))
	{
	  int id = XINT (XCAR (XCAR (parents)));
	  int code_offset = XINT (XCDR (XCAR (parents)));
	  unsigned this_code = code - code_offset;

	  charset = CHARSET_FROM_ID (id);
	  if ((c = DECODE_CHAR (charset, this_code)) >= 0)
	    break;
	}
    }
  else
    {
      char_index = CODE_POINT_TO_INDEX (charset, code);
      if (char_index < 0)
	return -1;

      if (method == CHARSET_METHOD_MAP)
	{
	  Lisp_Object decoder;

	  decoder = CHARSET_DECODER (charset);
	  if (! VECTORP (decoder))
	    {
	      load_charset (charset, 1);
	      decoder = CHARSET_DECODER (charset);
	    }
	  if (VECTORP (decoder))
	    c = XINT (AREF (decoder, char_index));
	  else
	    c = GET_TEMP_CHARSET_WORK_DECODER (char_index);
	}
      else			/* method == CHARSET_METHOD_OFFSET */
	{
	  c = char_index + CHARSET_CODE_OFFSET (charset);
	  if (CHARSET_UNIFIED_P (charset)
	      && c > MAX_UNICODE_CHAR)
	    MAYBE_UNIFY_CHAR (c);
	}
    }

  return c;
}

/* Variable used temporarily by the macro ENCODE_CHAR.  */
Lisp_Object charset_work;

/* Return a code-point of CHAR in CHARSET.  If CHAR doesn't belong to
   CHARSET, return CHARSET_INVALID_CODE (CHARSET).  If STRICT is true,
   use CHARSET's strict_max_char instead of max_char.  */

unsigned
encode_char (charset, c)
     struct charset *charset;
     int c;
{
  unsigned code;
  enum charset_method method = CHARSET_METHOD (charset);

  if (CHARSET_UNIFIED_P (charset))
    {
      Lisp_Object deunifier, deunified;
      int code_index = -1;

      deunifier = CHARSET_DEUNIFIER (charset);
      if (! CHAR_TABLE_P (deunifier))
	{
	  load_charset (charset, 2);
	  deunifier = CHARSET_DEUNIFIER (charset);
	}
      if (CHAR_TABLE_P (deunifier))
	{
	  Lisp_Object deunified = CHAR_TABLE_REF (deunifier, c);

	  if (INTEGERP (deunified))
	    code_index = XINT (deunified);
	}
      else
	{
	  code_index = GET_TEMP_CHARSET_WORK_ENCODER (c);
	}
      if (code_index >= 0)
	c = CHARSET_CODE_OFFSET (charset) + code_index;
    }

  if (method == CHARSET_METHOD_SUBSET)
    {
      Lisp_Object subset_info;
      struct charset *this_charset;

      subset_info = CHARSET_SUBSET (charset);
      this_charset = CHARSET_FROM_ID (XFASTINT (AREF (subset_info, 0)));
      code = ENCODE_CHAR (this_charset, c);
      if (code == CHARSET_INVALID_CODE (this_charset)
	  || code < XFASTINT (AREF (subset_info, 1))
	  || code > XFASTINT (AREF (subset_info, 2)))
	return CHARSET_INVALID_CODE (charset);
      code += XINT (AREF (subset_info, 3));
      return code;
    }

  if (method == CHARSET_METHOD_SUPERSET)
    {
      Lisp_Object parents;

      parents = CHARSET_SUPERSET (charset);
      for (; CONSP (parents); parents = XCDR (parents))
	{
	  int id = XINT (XCAR (XCAR (parents)));
	  int code_offset = XINT (XCDR (XCAR (parents)));
	  struct charset *this_charset = CHARSET_FROM_ID (id);

	  code = ENCODE_CHAR (this_charset, c);
	  if (code != CHARSET_INVALID_CODE (this_charset))
	    return code + code_offset;
	}
      return CHARSET_INVALID_CODE (charset);
    }

  if (! CHARSET_FAST_MAP_REF ((c), charset->fast_map)
      || c < CHARSET_MIN_CHAR (charset) || c > CHARSET_MAX_CHAR (charset))
    return CHARSET_INVALID_CODE (charset);

  if (method == CHARSET_METHOD_MAP)
    {
      Lisp_Object encoder;
      Lisp_Object val;

      encoder = CHARSET_ENCODER (charset);
      if (! CHAR_TABLE_P (CHARSET_ENCODER (charset)))
	{
	  load_charset (charset, 2);
	  encoder = CHARSET_ENCODER (charset);
	}
      if (CHAR_TABLE_P (encoder))
	{
	  val = CHAR_TABLE_REF (encoder, c);
	  if (NILP (val))
	    return CHARSET_INVALID_CODE (charset);
	  code = XINT (val);
	  if (! CHARSET_COMPACT_CODES_P (charset))
	    code = INDEX_TO_CODE_POINT (charset, code);
	}
      else
	{
	  code = GET_TEMP_CHARSET_WORK_ENCODER (c);
	  code = INDEX_TO_CODE_POINT (charset, code);	  
	}
    }
  else				/* method == CHARSET_METHOD_OFFSET */
    {
      int code_index = c - CHARSET_CODE_OFFSET (charset);

      code = INDEX_TO_CODE_POINT (charset, code_index);
    }

  return code;
}


DEFUN ("decode-char", Fdecode_char, Sdecode_char, 2, 3, 0,
       doc: /* Decode the pair of CHARSET and CODE-POINT into a character.
Return nil if CODE-POINT is not valid in CHARSET.

CODE-POINT may be a cons (HIGHER-16-BIT-VALUE . LOWER-16-BIT-VALUE).

Optional argument RESTRICTION specifies a way to map the pair of CCS
and CODE-POINT to a character.  Currently not supported and just ignored.  */)
  (charset, code_point, restriction)
     Lisp_Object charset, code_point, restriction;
{
  int c, id;
  unsigned code;
  struct charset *charsetp;

  CHECK_CHARSET_GET_ID (charset, id);
  if (CONSP (code_point))
    {
      CHECK_NATNUM_CAR (code_point);
      CHECK_NATNUM_CDR (code_point);
      code = (XINT (XCAR (code_point)) << 16) | (XINT (XCDR (code_point)));
    }
  else
    {
      CHECK_NATNUM (code_point);
      code = XINT (code_point);
    }
  charsetp = CHARSET_FROM_ID (id);
  c = DECODE_CHAR (charsetp, code);
  return (c >= 0 ? make_number (c) : Qnil);
}


DEFUN ("encode-char", Fencode_char, Sencode_char, 2, 3, 0,
       doc: /* Encode the character CH into a code-point of CHARSET.
Return nil if CHARSET doesn't include CH.

Optional argument RESTRICTION specifies a way to map CH to a
code-point in CCS.  Currently not supported and just ignored.  */)
     (ch, charset, restriction)
     Lisp_Object ch, charset, restriction;
{
  int id;
  unsigned code;
  struct charset *charsetp;

  CHECK_CHARSET_GET_ID (charset, id);
  CHECK_NATNUM (ch);
  charsetp = CHARSET_FROM_ID (id);
  code = ENCODE_CHAR (charsetp, XINT (ch));
  if (code == CHARSET_INVALID_CODE (charsetp))
    return Qnil;
  if (code > 0x7FFFFFF)
    return Fcons (make_number (code >> 16), make_number (code & 0xFFFF));
  return make_number (code);
}


DEFUN ("make-char", Fmake_char, Smake_char, 1, 5, 0,
       doc:
       /* Return a character of CHARSET whose position codes are CODEn.

CODE1 through CODE4 are optional, but if you don't supply sufficient
position codes, it is assumed that the minimum code in each dimension
is specified.  */)
     (charset, code1, code2, code3, code4)
     Lisp_Object charset, code1, code2, code3, code4;
{
  int id, dimension;
  struct charset *charsetp;
  unsigned code;
  int c;

  CHECK_CHARSET_GET_ID (charset, id);
  charsetp = CHARSET_FROM_ID (id);

  dimension = CHARSET_DIMENSION (charsetp);
  if (NILP (code1))
    code = (CHARSET_ASCII_COMPATIBLE_P (charsetp)
	    ? 0 : CHARSET_MIN_CODE (charsetp));
  else
    {
      CHECK_NATNUM (code1);
      if (XFASTINT (code1) >= 0x100)
	args_out_of_range (make_number (0xFF), code1);
      code = XFASTINT (code1);

      if (dimension > 1)
	{
	  code <<= 8;
	  if (NILP (code2))
	    code |= charsetp->code_space[(dimension - 2) * 4];
	  else
	    {
	      CHECK_NATNUM (code2);
	      if (XFASTINT (code2) >= 0x100)
		args_out_of_range (make_number (0xFF), code2);
	      code |= XFASTINT (code2);
	    }

	  if (dimension > 2)
	    {
	      code <<= 8;
	      if (NILP (code3))
		code |= charsetp->code_space[(dimension - 3) * 4];
	      else
		{
		  CHECK_NATNUM (code3);
		  if (XFASTINT (code3) >= 0x100)
		    args_out_of_range (make_number (0xFF), code3);
		  code |= XFASTINT (code3);
		}

	      if (dimension > 3)
		{
		  code <<= 8;
		  if (NILP (code4))
		    code |= charsetp->code_space[0];
		  else
		    {
		      CHECK_NATNUM (code4);
		      if (XFASTINT (code4) >= 0x100)
			args_out_of_range (make_number (0xFF), code4);
		      code |= XFASTINT (code4);
		    }
		}
	    }
	}
    }

  if (CHARSET_ISO_FINAL (charsetp) >= 0)
    code &= 0x7F7F7F7F;
  c = DECODE_CHAR (charsetp, code);
  if (c < 0)
    error ("Invalid code(s)");
  return make_number (c);
}


/* Return the first charset in CHARSET_LIST that contains C.
   CHARSET_LIST is a list of charset IDs.  If it is nil, use
   Vcharset_ordered_list.  */

struct charset *
char_charset (c, charset_list, code_return)
     int c;
     Lisp_Object charset_list;
     unsigned *code_return;
{
  int maybe_null = 0;

  if (NILP (charset_list))
    charset_list = Vcharset_ordered_list;
  else
    maybe_null = 1;

  while (CONSP (charset_list))
    {
      struct charset *charset = CHARSET_FROM_ID (XINT (XCAR (charset_list)));
      unsigned code = ENCODE_CHAR (charset, c);

      if (code != CHARSET_INVALID_CODE (charset))
	{
	  if (code_return)
	    *code_return = code;
	  return charset;
	}
      charset_list = XCDR (charset_list);
      if (c <= MAX_UNICODE_CHAR
	 && EQ (charset_list, Vcharset_non_preferred_head))
	return CHARSET_FROM_ID (charset_unicode);
    }
  return (maybe_null ? NULL
	  : c <= MAX_5_BYTE_CHAR ? CHARSET_FROM_ID (charset_emacs)
	  : CHARSET_FROM_ID (charset_eight_bit));
}


DEFUN ("split-char", Fsplit_char, Ssplit_char, 1, 1, 0,
       doc:
       /*Return list of charset and one to four position-codes of CH.
The charset is decided by the current priority order of charsets.
A position-code is a byte value of each dimension of the code-point of
CH in the charset.  */)
     (ch)
     Lisp_Object ch;
{
  struct charset *charset;
  int c, dimension;
  unsigned code;
  Lisp_Object val;

  CHECK_CHARACTER (ch);
  c = XFASTINT (ch);
  charset = CHAR_CHARSET (c);
  if (! charset)
    abort ();
  code = ENCODE_CHAR (charset, c);
  if (code == CHARSET_INVALID_CODE (charset))
    abort ();
  dimension = CHARSET_DIMENSION (charset);
  for (val = Qnil; dimension > 0; dimension--)
    {
      val = Fcons (make_number (code & 0xFF), val);
      code >>= 8;
    }
  return Fcons (CHARSET_NAME (charset), val);
}


DEFUN ("char-charset", Fchar_charset, Schar_charset, 1, 1, 0,
       doc: /* Return the charset of highest priority that contains CH.  */)
     (ch)
     Lisp_Object ch;
{
  struct charset *charset;

  CHECK_CHARACTER (ch);
  charset = CHAR_CHARSET (XINT (ch));
  return (CHARSET_NAME (charset));
}


DEFUN ("charset-after", Fcharset_after, Scharset_after, 0, 1, 0,
       doc: /*
Return charset of a character in the current buffer at position POS.
If POS is nil, it defauls to the current point.
If POS is out of range, the value is nil.  */)
     (pos)
     Lisp_Object pos;
{
  Lisp_Object ch;
  struct charset *charset;

  ch = Fchar_after (pos);
  if (! INTEGERP (ch))
    return ch;
  charset = CHAR_CHARSET (XINT (ch));
  return (CHARSET_NAME (charset));
}


DEFUN ("iso-charset", Fiso_charset, Siso_charset, 3, 3, 0,
       doc: /*
Return charset of ISO's specification DIMENSION, CHARS, and FINAL-CHAR.

ISO 2022's designation sequence (escape sequence) distinguishes charsets
by their DIMENSION, CHARS, and FINAL-CHAR,
whereas Emacs distinguishes them by charset symbol.
See the documentation of the function `charset-info' for the meanings of
DIMENSION, CHARS, and FINAL-CHAR.  */)
     (dimension, chars, final_char)
     Lisp_Object dimension, chars, final_char;
{
  int id;
  int chars_flag;

  check_iso_charset_parameter (dimension, chars, final_char);
  chars_flag = XFASTINT (chars) == 96;
  id = ISO_CHARSET_TABLE (XFASTINT (dimension), chars_flag,
			  XFASTINT (final_char));
  return (id >= 0 ? CHARSET_NAME (CHARSET_FROM_ID (id)) : Qnil);
}


DEFUN ("clear-charset-maps", Fclear_charset_maps, Sclear_charset_maps,
       0, 0, 0,
       doc: /*
Internal use only.
Clear temporary charset mapping tables.
It should be called only from temacs invoked for dumping.  */)
     ()
{
  int i;
  struct charset *charset;
  Lisp_Object attrs;

  if (temp_charset_work)
    {
      free (temp_charset_work);
      temp_charset_work = NULL;
    }

  if (CHAR_TABLE_P (Vchar_unify_table))
    Foptimize_char_table (Vchar_unify_table, Qnil);

  return Qnil;
}

DEFUN ("charset-priority-list", Fcharset_priority_list,
       Scharset_priority_list, 0, 1, 0,
       doc: /* Return the list of charsets ordered by priority.
HIGHESTP non-nil means just return the highest priority one.  */)
     (highestp)
     Lisp_Object highestp;
{
  Lisp_Object val = Qnil, list = Vcharset_ordered_list;

  if (!NILP (highestp))
    return CHARSET_NAME (CHARSET_FROM_ID (XINT (Fcar (list))));

  while (!NILP (list))
    {
      val = Fcons (CHARSET_NAME (CHARSET_FROM_ID (XINT (XCAR (list)))), val);
      list = XCDR (list);
    }
  return Fnreverse (val);
}

DEFUN ("set-charset-priority", Fset_charset_priority, Sset_charset_priority,
       1, MANY, 0,
       doc: /* Assign higher priority to the charsets given as arguments.
usage: (set-charset-priority &rest charsets)  */)
       (nargs, args)
     int nargs;
     Lisp_Object *args;
{
  Lisp_Object new_head, old_list, arglist[2];
  Lisp_Object list_2022, list_emacs_mule;
  int i, id;

  old_list = Fcopy_sequence (Vcharset_ordered_list);
  new_head = Qnil;
  for (i = 0; i < nargs; i++)
    {
      CHECK_CHARSET_GET_ID (args[i], id);
      if (! NILP (Fmemq (make_number (id), old_list)))
	{
	  old_list = Fdelq (make_number (id), old_list);
	  new_head = Fcons (make_number (id), new_head);
	}
    }
  arglist[0] = Fnreverse (new_head);
  arglist[1] = Vcharset_non_preferred_head = old_list;
  Vcharset_ordered_list = Fnconc (2, arglist);
  charset_ordered_list_tick++;

  for (old_list = Vcharset_ordered_list, list_2022 = list_emacs_mule = Qnil;
       CONSP (old_list); old_list = XCDR (old_list))
    {
      if (! NILP (Fmemq (XCAR (old_list), Viso_2022_charset_list)))
	list_2022 = Fcons (XCAR (old_list), list_2022);
      if (! NILP (Fmemq (XCAR (old_list), Vemacs_mule_charset_list)))
	list_emacs_mule = Fcons (XCAR (old_list), list_emacs_mule);
    }
  Viso_2022_charset_list = Fnreverse (list_2022);
  Vemacs_mule_charset_list = Fnreverse (list_emacs_mule);

  return Qnil;
}

DEFUN ("charset-id-internal", Fcharset_id_internal, Scharset_id_internal,
       0, 1, 0,
       doc: /* Internal use only.
Return charset identification number of CHARSET.  */)
     (charset)
     Lisp_Object charset;
{
  int id;

  CHECK_CHARSET_GET_ID (charset, id);
  return make_number (id);
}


void
init_charset ()
{
  Lisp_Object tempdir;
  tempdir = Fexpand_file_name (build_string ("charsets"), Vdata_directory);
  if (access (SDATA (tempdir), 0) < 0)
    {
      dir_warning ("Error: charsets directory (%s) does not exist.\n\
Emacs will not function correctly without the character map files.\n\
Please check your installation!\n",
                   tempdir);
      /* TODO should this be a fatal error?  (Bug#909)  */
    }

  Vcharset_map_path = Fcons (tempdir, Qnil);
}


void
init_charset_once ()
{
  int i, j, k;

  for (i = 0; i < ISO_MAX_DIMENSION; i++)
    for (j = 0; j < ISO_MAX_CHARS; j++)
      for (k = 0; k < ISO_MAX_FINAL; k++)
	iso_charset_table[i][j][k] = -1;

  for (i = 0; i < 256; i++)
    emacs_mule_charset[i] = NULL;

  charset_jisx0201_roman = -1;
  charset_jisx0208_1978 = -1;
  charset_jisx0208 = -1;

  for (i = 0; i < 128; i++)
    unibyte_to_multibyte_table[i] = i;
  for (; i < 256; i++)
    unibyte_to_multibyte_table[i] = BYTE8_TO_CHAR (i);
}

#ifdef emacs

void
syms_of_charset ()
{
  DEFSYM (Qcharsetp, "charsetp");

  DEFSYM (Qascii, "ascii");
  DEFSYM (Qunicode, "unicode");
  DEFSYM (Qemacs, "emacs");
  DEFSYM (Qeight_bit, "eight-bit");
  DEFSYM (Qiso_8859_1, "iso-8859-1");

  DEFSYM (Qgl, "gl");
  DEFSYM (Qgr, "gr");

  staticpro (&Vcharset_ordered_list);
  Vcharset_ordered_list = Qnil;

  staticpro (&Viso_2022_charset_list);
  Viso_2022_charset_list = Qnil;

  staticpro (&Vemacs_mule_charset_list);
  Vemacs_mule_charset_list = Qnil;

  /* Don't staticpro them here.  It's done in syms_of_fns.  */
  QCtest = intern (":test");
  Qeq = intern ("eq");

  staticpro (&Vcharset_hash_table);
  {
    Lisp_Object args[2];
    args[0] = QCtest;
    args[1] = Qeq;
    Vcharset_hash_table = Fmake_hash_table (2, args);
  }

  charset_table_size = 128;
  charset_table = ((struct charset *)
		   xmalloc (sizeof (struct charset) * charset_table_size));
  charset_table_used = 0;

  defsubr (&Scharsetp);
  defsubr (&Smap_charset_chars);
  defsubr (&Sdefine_charset_internal);
  defsubr (&Sdefine_charset_alias);
  defsubr (&Scharset_plist);
  defsubr (&Sset_charset_plist);
  defsubr (&Sunify_charset);
  defsubr (&Sget_unused_iso_final_char);
  defsubr (&Sdeclare_equiv_charset);
  defsubr (&Sfind_charset_region);
  defsubr (&Sfind_charset_string);
  defsubr (&Sdecode_char);
  defsubr (&Sencode_char);
  defsubr (&Ssplit_char);
  defsubr (&Smake_char);
  defsubr (&Schar_charset);
  defsubr (&Scharset_after);
  defsubr (&Siso_charset);
  defsubr (&Sclear_charset_maps);
  defsubr (&Scharset_priority_list);
  defsubr (&Sset_charset_priority);
  defsubr (&Scharset_id_internal);

  DEFVAR_LISP ("charset-map-path", &Vcharset_map_path,
	       doc: /* *List of directories to search for charset map files.  */);
  Vcharset_map_path = Qnil;

  DEFVAR_BOOL ("inhibit-load-charset-map", &inhibit_load_charset_map,
	       doc: /* Inhibit loading of charset maps.  Used when dumping Emacs.  */);
  inhibit_load_charset_map = 0;

  DEFVAR_LISP ("charset-list", &Vcharset_list,
	       doc: /* List of all charsets ever defined.  */);
  Vcharset_list = Qnil;

  DEFVAR_LISP ("current-iso639-language", &Vcurrent_iso639_language,
	       doc: /* ISO639 language mnemonic symbol for the current language environment.
If the current language environment is for multiple languages (e.g. "Latin-1"),
the value may be a list of mnemonics.  */);
  Vcurrent_iso639_language = Qnil;

  charset_ascii
    = define_charset_internal (Qascii, 1, "\x00\x7F\x00\x00\x00\x00",
			       0, 127, 'B', -1, 0, 1, 0, 0);
  charset_iso_8859_1
    = define_charset_internal (Qiso_8859_1, 1, "\x00\xFF\x00\x00\x00\x00",
			       0, 255, -1, -1, -1, 1, 0, 0);
  charset_unicode
    = define_charset_internal (Qunicode, 3, "\x00\xFF\x00\xFF\x00\x10",
			       0, MAX_UNICODE_CHAR, -1, 0, -1, 1, 0, 0);
  charset_emacs
    = define_charset_internal (Qemacs, 3, "\x00\xFF\x00\xFF\x00\x3F",
			       0, MAX_5_BYTE_CHAR, -1, 0, -1, 1, 1, 0);
  charset_eight_bit
    = define_charset_internal (Qeight_bit, 1, "\x80\xFF\x00\x00\x00\x00",
			       128, 255, -1, 0, -1, 0, 1,
			       MAX_5_BYTE_CHAR + 1);
}

#endif /* emacs */

/* arch-tag: 66a89b8d-4c28-47d3-9ca1-56f78440d69f
   (do not change this comment) */