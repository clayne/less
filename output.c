/*
 * Copyright (C) 1984-2023  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


/*
 * High level routines dealing with the output to the screen.
 */

#include "less.h"
#if MSDOS_COMPILER==WIN32C
#include "windows.h"
#ifndef COMMON_LVB_UNDERSCORE
#define COMMON_LVB_UNDERSCORE 0x8000
#endif
#endif

public int errmsgs;    /* Count of messages displayed by error() */
public int need_clr;
public int final_attr;
public int at_prompt;

extern int sigs;
extern int sc_width;
extern int so_s_width, so_e_width;
extern int screen_trashed;
extern int is_tty;
extern int oldbot;
extern char intr_char;

#if MSDOS_COMPILER==WIN32C || MSDOS_COMPILER==BORLANDC || MSDOS_COMPILER==DJGPPC
extern int ctldisp;
extern int nm_fg_color, nm_bg_color;
extern int bo_fg_color, bo_bg_color;
extern int ul_fg_color, ul_bg_color;
extern int so_fg_color, so_bg_color;
extern int bl_fg_color, bl_bg_color;
extern int sgr_mode;
#if MSDOS_COMPILER==WIN32C
extern int vt_enabled;
#endif
#endif

/*
 * Display the line which is in the line buffer.
 */
public void put_line(void)
{
	int c;
	int i;
	int a;

	if (ABORT_SIGS())
	{
		/*
		 * Don't output if a signal is pending.
		 */
		screen_trashed = 1;
		return;
	}

	final_attr = AT_NORMAL;

	for (i = 0;  (c = gline(i, &a)) != '\0';  i++)
	{
		at_switch(a);
		final_attr = a;
		if (c == '\b')
			putbs();
		else
			putchr(c);
	}

	at_exit();
}

static char obuf[OUTBUF_SIZE];
static char *ob = obuf;
static int outfd = 2; /* stderr */

#if MSDOS_COMPILER==WIN32C || MSDOS_COMPILER==BORLANDC || MSDOS_COMPILER==DJGPPC

typedef unsigned t_attr;

#define A_BOLD      (1u<<0)
#define A_ITALIC    (1u<<1)
#define A_UNDERLINE (1u<<2)
#define A_BLINK     (1u<<3)
#define A_INVERSE   (1u<<4)
#define A_CONCEAL   (1u<<5)

/* long is guaranteed 32 bits, and we reserve bits for type + RGB */
typedef unsigned long t_color;

#define T_DEFAULT   0ul
#define T_ANSI      1ul  /* colors 0-7 */

#define CGET_ANSI(c) ((c) & 0x7)

#define C_DEFAULT    (T_DEFAULT <<24) /* 0 */
#define C_ANSI(c)   ((T_ANSI    <<24) | (c))

/* attr/fg/bg/all 0 is the default attr/fg/bg/all, respectively */
typedef struct t_sgr {
	t_attr attr;
	t_color fg;
	t_color bg;
} t_sgr;

static const t_sgr SGR_DEFAULT; /* = {0} */

static void update_sgr(t_sgr *sgr, unsigned char code)
{
	switch (code)
	{
	case  0: *sgr = SGR_DEFAULT; break;

	case  1: sgr->attr |=  A_BOLD; break;
	case 22: sgr->attr &= ~A_BOLD; break;

	case  3: sgr->attr |=  A_ITALIC; break;
	case 23: sgr->attr &= ~A_ITALIC; break;

	case  4: sgr->attr |=  A_UNDERLINE; break;
	case 24: sgr->attr &= ~A_UNDERLINE; break;

	case  6: /* fast-blink, fallthrough */
	case  5: sgr->attr |=  A_BLINK; break;
	case 25: sgr->attr &= ~A_BLINK; break;

	case  7: sgr->attr |=  A_INVERSE; break;
	case 27: sgr->attr &= ~A_INVERSE; break;

	case  8: sgr->attr |=  A_CONCEAL; break;
	case 28: sgr->attr &= ~A_CONCEAL; break;

	case 39: sgr->fg = C_DEFAULT; break;
	case 49: sgr->bg = C_DEFAULT; break;

	case 30: case 31: case 32: case 33:
	case 34: case 35: case 36: case 37:
		sgr->fg = C_ANSI(code - 30);
		break;

	case 40: case 41: case 42: case 43:
	case 44: case 45: case 46: case 47:
		sgr->bg = C_ANSI(code - 40);
		break;
	}
}

static void set_win_colors(t_sgr *sgr)
{
#if MSDOS_COMPILER==WIN32C
	/* Screen colors used by 3x and 4x SGR commands. */
	static unsigned char screen_color[] = {
		0, /* BLACK */
		FOREGROUND_RED,
		FOREGROUND_GREEN,
		FOREGROUND_RED|FOREGROUND_GREEN,
		FOREGROUND_BLUE,
		FOREGROUND_BLUE|FOREGROUND_RED,
		FOREGROUND_BLUE|FOREGROUND_GREEN,
		FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED
	};
#else
	static enum COLORS screen_color[] = {
		BLACK, RED, GREEN, BROWN,
		BLUE, MAGENTA, CYAN, LIGHTGRAY
	};
#endif

	int fg, bg, tmp;  /* Windows colors */

	/* Not "SGR mode": apply -D<x> to default fg+bg with one attribute */
	if (!sgr_mode && sgr->fg == C_DEFAULT && sgr->bg == C_DEFAULT)
	{
		switch (sgr->attr)
		{
		case A_BOLD:
			WIN32setcolors(bo_fg_color, bo_bg_color);
			return;
		case A_UNDERLINE:
			WIN32setcolors(ul_fg_color, ul_bg_color);
			return;
		case A_BLINK:
			WIN32setcolors(bl_fg_color, bl_bg_color);
			return;
		case A_INVERSE:
			WIN32setcolors(so_fg_color, so_bg_color);
			return;
		/*
		 * There's no -Di so italic should not be here, but to
		 * preserve lagacy behavior, apply -Ds to italic too.
		 */
		case A_ITALIC:
			WIN32setcolors(so_fg_color, so_bg_color);
			return;
		}
	}

	/* generic application of the SGR state as Windows colors */

	fg = sgr->fg == C_DEFAULT ? nm_fg_color
	                          : screen_color[CGET_ANSI(sgr->fg)];

	bg = sgr->bg == C_DEFAULT ? nm_bg_color
	                          : screen_color[CGET_ANSI(sgr->bg)];

	if (sgr->attr & A_BOLD)
		fg |= 8;

	if (sgr->attr & (A_BLINK | A_UNDERLINE))
		bg |= 8;  /* TODO: can be illegible */

	if (sgr->attr & (A_INVERSE | A_ITALIC))
	{
		tmp = fg;
		fg = bg;
		bg = tmp;
	}

	if (sgr->attr & A_CONCEAL)
		fg = bg ^ 8;

	WIN32setcolors(fg, bg);
}

static void win_flush(void)
{
	if (ctldisp != OPT_ONPLUS || (vt_enabled && sgr_mode))
		WIN32textout(obuf, ob - obuf);
	else
	{
		/*
		 * Digest text, apply embedded SGR sequences as Windows-colors.
		 * By default - when -Da ("SGR mode") is unset - also apply
		 * translation of -D command-line options (at set_win_colors)
		 */
		char *anchor, *p, *p_next;
		static t_sgr sgr;

		for (anchor = p_next = obuf;
			 (p_next = memchr(p_next, ESC, ob - p_next)) != NULL; )
		{
			p = p_next;
			if (p[1] == '[')  /* "ESC-[" sequence */
			{
				if (p > anchor)
				{
					/*
					 * If some chars seen since
					 * the last escape sequence,
					 * write them out to the screen.
					 */
					WIN32textout(anchor, p-anchor);
					anchor = p;
				}
				p += 2;  /* Skip the "ESC-[" */
				if (is_ansi_end(*p))
				{
					/*
					 * Handle null escape sequence
					 * "ESC[m" as if it was "ESC[0m"
					 */
					p++;
					anchor = p_next = p;
					update_sgr(&sgr, 0);
					set_win_colors(&sgr);
					continue;
				}
				p_next = p;

				/*
				 * Parse and apply SGR values to the SGR state
				 * based on the escape sequence. 
				 */
				while (!is_ansi_end(*p))
				{
					char *q;
					long code = strtol(p, &q, 10);

					if (*q == '\0')
					{
						/*
						 * Incomplete sequence.
						 * Leave it unprocessed
						 * in the buffer.
						 */
						int slop = (int) (q - anchor);
						/* {{ strcpy args overlap! }} */
						strcpy(obuf, anchor);
						ob = &obuf[slop];
						return;
					}

					if (q == p ||
						code > 49 || code < 0 ||
						(!is_ansi_end(*q) && *q != ';'))
					{
						p_next = q;
						break;
					}
					if (*q == ';')
						q++;

					update_sgr(&sgr, code);
					p = q;
				}
				if (!is_ansi_end(*p) || p == p_next)
					break;

				set_win_colors(&sgr);
				p_next = anchor = p + 1;
			} else
				p_next++;
		}

		/* Output what's left in the buffer.  */
		WIN32textout(anchor, ob - anchor);
	}
	ob = obuf;
}
#endif

/*
 * Flush buffered output.
 *
 * If we haven't displayed any file data yet,
 * output messages on error output (file descriptor 2),
 * otherwise output on standard output (file descriptor 1).
 *
 * This has the desirable effect of producing all
 * error messages on error output if standard output
 * is directed to a file.  It also does the same if
 * we never produce any real output; for example, if
 * the input file(s) cannot be opened.  If we do
 * eventually produce output, code in edit() makes
 * sure these messages can be seen before they are
 * overwritten or scrolled away.
 */
public void flush(void)
{
	int n;

	n = (int) (ob - obuf);
	if (n == 0)
		return;
	ob = obuf;

#if MSDOS_COMPILER==MSOFTC
	if (interactive())
	{
		obuf[n] = '\0';
		_outtext(obuf);
		return;
	}
#else
#if MSDOS_COMPILER==WIN32C || MSDOS_COMPILER==BORLANDC || MSDOS_COMPILER==DJGPPC
	if (interactive())
	{
		ob = obuf + n;
		*ob = '\0';
		win_flush();
		return;
	}
#endif
#endif

	if (write(outfd, obuf, n) != n)
		screen_trashed = 1;
}

/*
 * Set the output file descriptor (1=stdout or 2=stderr).
 */
public void set_output(int fd)
{
	flush();
	outfd = fd;
}

/*
 * Output a character.
 */
public int putchr(int c)
{
#if 0 /* fake UTF-8 output for testing */
	extern int utf_mode;
	if (utf_mode)
	{
		static char ubuf[MAX_UTF_CHAR_LEN];
		static int ubuf_len = 0;
		static int ubuf_index = 0;
		if (ubuf_len == 0)
		{
			ubuf_len = utf_len(c);
			ubuf_index = 0;
		}
		ubuf[ubuf_index++] = c;
		if (ubuf_index < ubuf_len)
			return c;
		c = get_wchar(ubuf) & 0xFF;
		ubuf_len = 0;
	}
#endif
	clear_bot_if_needed();
#if MSDOS_COMPILER
	if (c == '\n' && is_tty)
	{
		/* remove_top(1); */
		putchr('\r');
	}
#else
#ifdef _OSK
	if (c == '\n' && is_tty)  /* In OS-9, '\n' == 0x0D */
		putchr(0x0A);
#endif
#endif
	/*
	 * Some versions of flush() write to *ob, so we must flush
	 * when we are still one char from the end of obuf.
	 */
	if (ob >= &obuf[sizeof(obuf)-1])
		flush();
	*ob++ = c;
	at_prompt = 0;
	return (c);
}

public void clear_bot_if_needed(void)
{
	if (!need_clr)
		return;
	need_clr = 0;
	clear_bot();
}

/*
 * Output a string.
 */
public void putstr(constant char *s)
{
	while (*s != '\0')
		putchr(*s++);
}


/*
 * Convert an integral type to a string.
 */
#define TYPE_TO_A_FUNC(funcname, type) \
void funcname(type num, char *buf, int radix) \
{ \
	int neg = (num < 0); \
	char tbuf[INT_STRLEN_BOUND(num)+2]; \
	char *s = tbuf + sizeof(tbuf); \
	if (neg) num = -num; \
	*--s = '\0'; \
	do { \
		*--s = "0123456789ABCDEF"[num % radix]; \
	} while ((num /= radix) != 0); \
	if (neg) *--s = '-'; \
	strcpy(buf, s); \
}

TYPE_TO_A_FUNC(postoa, POSITION)
TYPE_TO_A_FUNC(linenumtoa, LINENUM)
TYPE_TO_A_FUNC(inttoa, int)

/*
 * Convert a string to an integral type.  Return ((type) -1) on overflow.
 */
#define STR_TO_TYPE_FUNC(funcname, type) \
type funcname(char *buf, char **ebuf, int radix) \
{ \
	type val = 0; \
	int v = 0; \
	for (;; buf++) { \
		char c = *buf; \
		int digit = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; \
		if (digit < 0 || digit >= radix) break; \
		v |= ckd_mul(&val, val, radix); \
		v |= ckd_add(&val, val, digit); \
	} \
	if (ebuf != NULL) *ebuf = buf; \
	return v ? -1 : val; \
}

STR_TO_TYPE_FUNC(lstrtopos, POSITION)
STR_TO_TYPE_FUNC(lstrtoi, int)
STR_TO_TYPE_FUNC(lstrtoul, unsigned long)

/*
 * Print an integral type.
 */
#define IPRINT_FUNC(funcname, type, typetoa) \
static int funcname(type num, int radix) \
{ \
	char buf[INT_STRLEN_BOUND(num)]; \
	typetoa(num, buf, radix); \
	putstr(buf); \
	return (int) strlen(buf); \
}

IPRINT_FUNC(iprint_int, int, inttoa)
IPRINT_FUNC(iprint_linenum, LINENUM, linenumtoa)

/*
 * This function implements printf-like functionality
 * using a more portable argument list mechanism than printf's.
 *
 * {{ This paranoia about the portability of printf dates from experiences
 *    with systems in the 1980s and is of course no longer necessary. }}
 */
public int less_printf(char *fmt, PARG *parg)
{
	char *s;
	int col;

	col = 0;
	while (*fmt != '\0')
	{
		if (*fmt != '%')
		{
			putchr(*fmt++);
			col++;
		} else
		{
			++fmt;
			switch (*fmt++)
			{
			case 's':
				s = parg->p_string;
				parg++;
				while (*s != '\0')
				{
					putchr(*s++);
					col++;
				}
				break;
			case 'd':
				col += iprint_int(parg->p_int, 10);
				parg++;
				break;
			case 'x':
				col += iprint_int(parg->p_int, 16);
				parg++;
				break;
			case 'n':
				col += iprint_linenum(parg->p_linenum, 10);
				parg++;
				break;
			case 'c':
				s = prchar(parg->p_char);
				parg++;
				while (*s != '\0')
				{
					putchr(*s++);
					col++;
				}
				break;
			case '%':
				putchr('%');
				break;
			}
		}
	}
	return (col);
}

/*
 * Get a RETURN.
 * If some other non-trivial char is pressed, unget it, so it will
 * become the next command.
 */
public void get_return(void)
{
	int c;

#if ONLY_RETURN
	while ((c = getchr()) != '\n' && c != '\r')
		bell();
#else
	c = getchr();
	if (c != '\n' && c != '\r' && c != ' ' && c != READ_INTR)
		ungetcc(c);
#endif
}

/*
 * Output a message in the lower left corner of the screen
 * and wait for carriage return.
 */
public void error(char *fmt, PARG *parg)
{
	int col = 0;
	static char return_to_continue[] = "  (press RETURN)";

	errmsgs++;

	if (!interactive())
	{
		less_printf(fmt, parg);
		putchr('\n');
		return;
	}

	if (!oldbot)
		squish_check();
	at_exit();
	clear_bot();
	at_enter(AT_STANDOUT|AT_COLOR_ERROR);
	col += so_s_width;
	col += less_printf(fmt, parg);
	putstr(return_to_continue);
	at_exit();
	col += sizeof(return_to_continue) + so_e_width;

	get_return();
	lower_left();
	clear_eol();

	if (col >= sc_width)
		/*
		 * Printing the message has probably scrolled the screen.
		 * {{ Unless the terminal doesn't have auto margins,
		 *    in which case we just hammered on the right margin. }}
		 */
		screen_trashed = 1;

	flush();
}

/*
 * Output a message in the lower left corner of the screen
 * and don't wait for carriage return.
 * Usually used to warn that we are beginning a potentially
 * time-consuming operation.
 */
static void ierror_suffix(char *fmt, PARG *parg, char *suffix1, char *suffix2, char *suffix3)
{
	at_exit();
	clear_bot();
	at_enter(AT_STANDOUT|AT_COLOR_ERROR);
	(void) less_printf(fmt, parg);
	putstr(suffix1);
	putstr(suffix2);
	putstr(suffix3);
	at_exit();
	flush();
	need_clr = 1;
}

public void ierror(char *fmt, PARG *parg)
{
	ierror_suffix(fmt, parg, "... (interrupt to abort)", "", "");
}

public void ixerror(char *fmt, PARG *parg)
{
	if (!supports_ctrl_x())
		ierror(fmt, parg);
	else
		ierror_suffix(fmt, parg,
			"... (", prchar(intr_char), " or interrupt to abort)");
}

/*
 * Output a message in the lower left corner of the screen
 * and return a single-character response.
 */
public int query(char *fmt, PARG *parg)
{
	int c;
	int col = 0;

	if (interactive())
		clear_bot();

	(void) less_printf(fmt, parg);
	c = getchr();

	if (interactive())
	{
		lower_left();
		if (col >= sc_width)
			screen_trashed = 1;
		flush();
	} else
	{
		putchr('\n');
	}

	if (c == 'Q')
		quit(QUIT_OK);
	return (c);
}
