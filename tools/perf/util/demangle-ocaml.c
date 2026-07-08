// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "util/string2.h"

#include "demangle-ocaml.h"
#include "demangle-ocaml-structured.h"

#include <linux/ctype.h>

/*
 * Flat OCaml demangling
 *
 * Mangled symbols start with "caml" followed by an uppercase letter.
 * "__" encodes "." and "$xx" encodes character with hex value xx.
 */

static const char *caml_prefix = "caml";
static const size_t caml_prefix_len = 4;

/* mangled flat OCaml symbols start with "caml" followed by an upper-case letter */
static bool
is_flat(const char *sym)
{
	return 0 == strncmp(sym, caml_prefix, caml_prefix_len)
		&& isupper(sym[caml_prefix_len]);
}

static char *
ocaml_demangle_flat(const char *sym)
{
	char *result;
	int j = 0;
	int i;
	int len;

	len = strlen(sym);

	/* the demangled symbol is always smaller than the mangled symbol */
	result = malloc(len + 1);
	if (!result)
		return NULL;

	/* skip "caml" prefix */
	i = caml_prefix_len;

	while (i < len) {
		if (sym[i] == '_' && sym[i + 1] == '_') {
			/* "__" -> "." */
			result[j++] = '.';
			i += 2;
		}
		else if (sym[i] == '$' && isxdigit(sym[i + 1]) && isxdigit(sym[i + 2])) {
			/* "$xx" is a hex-encoded character */
			result[j++] = (hex(sym[i + 1]) << 4) | hex(sym[i + 2]);
			i += 3;
		}
		else {
			result[j++] = sym[i++];
		}
	}
	result[j] = '\0';

	return result;
}

/*
 * input:
 *     sym: a symbol which may have been mangled by the OCaml compiler
 * return:
 *     if the input doesn't look like a mangled OCaml symbol, NULL is returned
 *     otherwise, a newly allocated string containing the demangled symbol is returned
 *
 * Supports both:
 *   - Flat mangling:       "caml<Name>..." (OCaml compiler 4.* through to 5.*)
 *   - Structured mangling: "_Caml<path>"   (OxCaml compiler)
 */
char *
ocaml_demangle_sym(const char *sym)
{
	char *result = ocaml_demangle_structured_sym(sym);

	if (result)
		return result;

	if (is_flat(sym))
		return ocaml_demangle_flat(sym);

	return NULL;
}
