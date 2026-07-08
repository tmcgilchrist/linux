// SPDX-License-Identifier: MIT
/******************************************************************************
 *                                  OxCaml                                    *
 *                  Samuel Hym and Tim McGilchrist, Tarides                   *
 *                          Simon Spies, Jane Street                          *
 * -------------------------------------------------------------------------- *
 *                               MIT License                                  *
 *                                                                            *
 * Copyright (c) 2025--2026 Jane Street Group LLC                             *
 * opensource-contacts@janestreet.com                                         *
 * Copyright (c) 2025--2026 Tarides                                           *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included    *
 * in all copies or substantial portions of the Software.                     *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER *
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        *
 * DEALINGS IN THE SOFTWARE.                                                  *
 ******************************************************************************/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demangle-ocaml-structured.h"

/*
 * Structured OCaml demangling
 *
 * Mangled symbols have the form "_Caml<path>" where <path> is a sequence of
 * tagged, length-prefixed identifiers:
 *
 *   Tags:
 *     U - compilation Unit
 *     I - Inline marker
 *     M - Module
 *     S - anonymous Struct (module)
 *     O - class (Object)
 *     F - Function
 *     L - anonymous function (Lambda)
 *     P - Partial application
 *
 *   Each identifier is encoded as either:
 *     <decimal_length><payload>          (simple: payload is literal chars)
 *     u<decimal_length><escaped>_<raw>   (universal: non-output char or leading digit)
 *
 *   In universal encoding:
 *     - <escaped> is a sequence of (base26_position, hex_bytes) pairs
 *     - base26 uses A=0, B=1, ..., Z=25, BA=26, BB=27, ...
 *     - hex bytes are lowercase [0-9a-f], two chars per byte
 *     - <raw> is the subsequence of output characters
 *     - positions are relative insertion points into the raw string
 *
 *   Example: _CamlU3FooM3BarF3baz -> Foo.Bar.baz
 */

#define STRUCTURED_MAX_LEN (1024 * 1024)

static const char *structured_prefix = "_Caml";
static const size_t structured_prefix_len = 5;

static bool
is_structured(const char *sym)
{
	return 0 == strncmp(sym, structured_prefix, structured_prefix_len);
}

static int
parse_decimal(const char *sym, int len, int *pos)
{
	int val = 0;
	int p = *pos;
	int start = p;

	while (p < len && sym[p] >= '0' && sym[p] <= '9') {
		int digit = sym[p] - '0';

		if (val > (STRUCTURED_MAX_LEN - digit) / 10)
			return -1;
		val = val * 10 + digit;
		p++;
	}
	if (p == start)
		return -1;
	*pos = p;
	return val;
}

static int
parse_base26(const char *sym, int len, int *pos)
{
	int val = 0;
	int p = *pos;
	int start = p;

	while (p < len && sym[p] >= 'A' && sym[p] <= 'Z') {
		int digit = sym[p] - 'A';

		if (val > (STRUCTURED_MAX_LEN - digit) / 26)
			return -1;
		val = val * 26 + digit;
		p++;
	}
	if (p == start)
		return -1;
	*pos = p;
	return val;
}

/* The structured scheme encodes bytes as lowercase hex only ([0-9a-f]). */
static int
lower_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static int
parse_hex_byte(const char *sym, int len, int *pos)
{
	int hi, lo;

	if (*pos + 1 >= len)
		return -1;
	hi = lower_hex(sym[*pos]);
	lo = lower_hex(sym[*pos + 1]);
	if (hi < 0 || lo < 0)
		return -1;
	*pos += 2;
	return (hi << 4) | lo;
}

/*
 * Decode universal-encoded payload: <escaped>_<raw>
 *
 * Streams in a single pass: for each insertion in the escaped section,
 * copy raw characters up to the insertion point, then decode the hex
 * bytes directly to the output. The decoded string is always shorter
 * than the encoded payload, so payload_len + 1 suffices for output.
 */
static char *
decode_universal(const char *payload, int payload_len)
{
	int sep = -1;
	int i;
	int esc_len;
	const char *raw;
	int raw_len;
	int esc_pos = 0;
	int raw_pos = 0;
	int out = 0;
	char *result;

	for (i = 0; i < payload_len; i++) {
		if (payload[i] == '_') {
			sep = i;
			break;
		}
	}
	if (sep < 0)
		return NULL;

	esc_len = sep;
	raw = payload + sep + 1;
	raw_len = payload_len - sep - 1;

	result = malloc(payload_len + 1);
	if (!result)
		return NULL;

	while (esc_pos < esc_len) {
		int skip = parse_base26(payload, esc_len, &esc_pos);

		if (skip < 0)
			goto fail;

		/* Copy 'skip' raw characters; an insertion past the end of
		 * the raw section is malformed. */
		if (raw_pos + skip > raw_len)
			goto fail;
		memcpy(result + out, raw + raw_pos, skip);
		out += skip;
		raw_pos += skip;

		/* Decode hex bytes until next base26 position or end */
		while (esc_pos < esc_len &&
		       !(payload[esc_pos] >= 'A' && payload[esc_pos] <= 'Z')) {
			int byte_val = parse_hex_byte(payload, esc_len, &esc_pos);

			if (byte_val < 0)
				goto fail;
			result[out++] = (char)byte_val;
		}
	}

	/* Copy remaining raw characters */
	memcpy(result + out, raw + raw_pos, raw_len - raw_pos);
	out += raw_len - raw_pos;

	result[out] = '\0';
	return result;

fail:
	free(result);
	return NULL;
}

static char *
decode_ident(const char *sym, int len, int *pos)
{
	bool universal = false;
	int ident_len;
	char *result;

	if (*pos < len && sym[*pos] == 'u') {
		universal = true;
		(*pos)++;
	}

	ident_len = parse_decimal(sym, len, pos);

	/*
	 * A zero-length payload (e.g. "_CamlU0...") is malformed: every path
	 * item carries a non-empty identifier, so reject it rather than emit
	 * an empty component.
	 */
	if (ident_len <= 0 || *pos + ident_len > len)
		return NULL;

	if (!universal) {
		result = malloc(ident_len + 1);
		if (!result)
			return NULL;
		memcpy(result, sym + *pos, ident_len);
		result[ident_len] = '\0';
	} else {
		result = decode_universal(sym + *pos, ident_len);
	}

	*pos += ident_len;
	return result;
}

/* Human-readable prefixes for the location-bearing path tags. The decoded
 * payload of S/L/P items is a "file_line_col" string rendered as
 * "<prefix>(file:line:col)". The other tags render their identifier as-is. */
static const char *
loc_prefix(char tag)
{
	switch (tag) {
	case 'S': return "mod";
	case 'L': return "fn";
	case 'P': return "partial";
	default:  return NULL;
	}
}

static bool
is_structured_tag(char c)
{
	return c == 'U' || c == 'I' || c == 'M' || c == 'S' ||
	       c == 'O' || c == 'F' || c == 'L' || c == 'P';
}

static bool
all_digits(const char *s, int n)
{
	int i;

	if (n <= 0)
		return false;
	for (i = 0; i < n; i++)
		if (s[i] < '0' || s[i] > '9')
			return false;
	return true;
}

/*
 * Split a decoded "file_line_col" location payload into its components,
 * delimited by the last two '_'. The file part may be empty but line and
 * col must be non-empty decimal integers. Returns false on any other shape.
 */
static bool
parse_location(const char *loc, int loc_len,
	       const char **file, int *file_len,
	       const char **line, int *line_len,
	       const char **col, int *col_len)
{
	int second = -1, first = -1, i;

	for (i = loc_len - 1; i >= 0; i--)
		if (loc[i] == '_') {
			second = i;
			break;
		}
	if (second < 0)
		return false;
	for (i = second - 1; i >= 0; i--)
		if (loc[i] == '_') {
			first = i;
			break;
		}
	if (first < 0)
		return false;

	*file = loc;
	*file_len = first;
	*line = loc + first + 1;
	*line_len = second - first - 1;
	*col = loc + second + 1;
	*col_len = loc_len - second - 1;

	return all_digits(*line, *line_len) && all_digits(*col, *col_len);
}

/*
 * The compiler appends a "_<stamp>_code" closure suffix after the path; the
 * preceding identifier's length prefix marks where it begins, so the whole
 * suffix is dropped when demangling. Other "_<stamp>" suffixes are kept.
 */
static bool
is_code_suffix(const char *s, int n)
{
	int i;

	if (n <= 6 || s[0] != '_' || memcmp(s + n - 5, "_code", 5) != 0)
		return false;
	for (i = 1; i < n - 5; i++)
		if (s[i] < '0' || s[i] > '9')
			return false;
	return true;
}

/*
 * Demangle a structured OCaml symbol (_Caml prefix). Components are joined
 * with "." and rendered as:
 *   U/M/O/F (unit/module/class/function): the decoded identifier
 *   S (struct):  mod(file:line:col)
 *   L (lambda):  fn(file:line:col)
 *   P (partial): partial(file:line:col)
 *   I (inline):  <specialization_of>
 * A bare '_' ends the path; the trailing suffix is kept unless it is a
 * "_<stamp>_code" closure marker. Any other character is malformed.
 */
static char *
ocaml_demangle_structured(const char *sym)
{
	size_t slen = strlen(sym);
	int len;
	int pos = structured_prefix_len;
	/*
	 * Decoded identifiers are never longer than their encoding and the
	 * longest fixed label is "<specialization_of>"; 4x input plus a small
	 * slack is a safe upper bound for the output buffer.
	 */
	int buf_size;
	char *result;
	int out = 0;
	bool first = true;

	if (slen > STRUCTURED_MAX_LEN)
		return NULL;
	len = slen;
	buf_size = len * 4 + 64;

	result = malloc(buf_size);
	if (!result)
		return NULL;

	while (pos < len) {
		char tag = sym[pos];
		const char *prefix;
		char *ident;
		int n;

		/* A bare '_' ends the path; the rest is a compiler suffix. */
		if (tag == '_') {
			int suffix_len = len - pos;

			if (!is_code_suffix(sym + pos, suffix_len)) {
				if (out + suffix_len >= buf_size)
					goto fail;
				memcpy(result + out, sym + pos, suffix_len);
				out += suffix_len;
			}
			break;
		}

		if (!is_structured_tag(tag))
			goto fail;
		pos++;

		/* Add separator between components */
		if (!first) {
			if (out + 1 >= buf_size)
				goto fail;
			result[out++] = '.';
		}
		first = false;

		if (tag == 'I') {
			/* Inline marker has no payload */
			n = snprintf(result + out, buf_size - out,
				     "<specialization_of>");
			if (n < 0 || out + n >= buf_size)
				goto fail;
			out += n;
			continue;
		}

		/* All other tags have an encoded identifier payload */
		ident = decode_ident(sym, len, &pos);
		if (!ident)
			goto fail;

		prefix = loc_prefix(tag);
		if (prefix) {
			const char *file, *line, *col;
			int file_len, line_len, col_len;

			if (!parse_location(ident, strlen(ident),
					     &file, &file_len, &line, &line_len,
					     &col, &col_len)) {
				free(ident);
				goto fail;
			}
			n = snprintf(result + out, buf_size - out,
				     "%s(%.*s:%.*s:%.*s)", prefix,
				     file_len, file, line_len, line,
				     col_len, col);
		} else {
			n = snprintf(result + out, buf_size - out, "%s", ident);
		}
		free(ident);
		if (n < 0 || out + n >= buf_size)
			goto fail;
		out += n;
	}

	/* Require at least one successfully decoded component */
	if (first)
		goto fail;

	result[out] = '\0';
	return result;

fail:
	free(result);
	return NULL;
}

/*
 * input:
 *     sym: a symbol which may have been mangled by the OxCaml compiler
 * return:
 *     if the input isn't a structured ("_Caml") symbol, or is malformed,
 *     NULL is returned; otherwise a newly allocated string containing the
 *     demangled symbol is returned
 */
char *
ocaml_demangle_structured_sym(const char *sym)
{
	if (!is_structured(sym))
		return NULL;

	return ocaml_demangle_structured(sym);
}
