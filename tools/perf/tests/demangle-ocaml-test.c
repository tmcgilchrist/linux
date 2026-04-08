// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "debug.h"
#include "symbol.h"
#include "tests.h"

static int test__demangle_ocaml(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_OK;
	char *buf = NULL;
	size_t i;

	struct {
		const char *mangled, *demangled;
	} test_cases[] = {
		/* non-OCaml symbols should not be demangled */
		{ "main",
		  NULL },
		/* "caml" followed by lowercase is not a mangled symbol */
		{ "camlfoo",
		  NULL },
		/* runtime symbol "caml_" prefix is not a mangled user symbol */
		{ "caml_exn_Match_failure",
		  NULL },

		/* === Flat mangling === */

		/* compilation unit name only, no member */
		{ "camlFoo",
		  "Foo" },
		/* unit with a single member separated by __ */
		{ "camlFoo__bar_0",
		  "Foo.bar_0" },
		/* deeply nested module path */
		{ "camlA__B__C__D__func_0",
		  "A.B.C.D.func_0" },
		/* single underscores are preserved (not separators) */
		{ "camlFoo__bar_baz_42",
		  "Foo.bar_baz_42" },
		{ "camlStdlib__array__map_154",
		  "Stdlib.array.map_154" },
		/* hex-encoded special chars in anonymous function name */
		{ "camlStdlib__anon_fn$5bstdlib$2eml$3a334$2c0$2d$2d54$5d_1453",
		  "Stdlib.anon_fn[stdlib.ml:334,0--54]_1453" },
		/* hex-encoded operator (++) */
		{ "camlStdlib__bytes__$2b$2b_2205",
		  "Stdlib.bytes.++_2205" },
		/* multiple consecutive hex-encoded chars (+++) */
		{ "camlFoo__$2b$2b$2b_1",
		  "Foo.+++_1" },
		/* incomplete hex escape at end: $ not followed by two hex digits */
		{ "camlFoo__bar$2",
		  "Foo.bar$2" },
		/* $ followed by non-hex chars is kept literal */
		{ "camlFoo__bar$gz",
		  "Foo.bar$gz" },
		/* Parameterized libraries prepend the unit name to the pack-qualified
		 * path, so the unit "Baz" appears both at the front and the end
		 * (BazFoo__Bar__Baz); the flat scheme just turns every "__" into
		 * "." and keeps the trailing closure id ("_0_2_code") verbatim. */
		{ "camlBazFoo__Bar__Baz__init_0_2_code",
		  "BazFoo.Bar.Baz.init_0_2_code" },
		/* instance arguments (4 underscores = two __ separators) */
		{ "camlFoo____Bar__baz_1",
		  "Foo..Bar.baz_1" },
		/* trailing double underscore */
		{ "camlFoo__",
		  "Foo." },

		/* Internal/runtime symbols the unit name keeps its single
		 * underscore and "__" becomes ".". */
		{ "camlE2e_inline__entry",
		  "E2e_inline.entry" },
		{ "camlE2e_inline__Pmakeblock137",
		  "E2e_inline.Pmakeblock137" },
		/* operator with several hex escapes (.%()<-) */
		{ "camlE2e_ocaml__.$25$28$29$3c$2d_102",
		  "E2e_ocaml..%()<-_102" },
		/* anonymous function with a bracketed location; flat keeps the
		 * whole "_<n>_<m>_code" stamp verbatim */
		{ "camlE2e_inline__fn$5be2e_inline.ml$3a52$2c30$2d$2d61$5d_5_21_code",
		  "E2e_inline.fn[e2e_inline.ml:52,30--61]_5_21_code" },

		/* === Structured mangling === */

		/* bare prefix with no path items is malformed */
		{ "_Caml",
		  NULL },
		/* basic: _CamlU3FooM3BarF3baz -> Foo.Bar.baz */
		{ "_CamlU3FooM3BarF3baz",
		  "Foo.Bar.baz" },
		/* unit + function only */
		{ "_CamlU6StdlibF3map",
		  "Stdlib.map" },
		/* class tag */
		{ "_CamlU3FooO5MyObj",
		  "Foo.MyObj" },
		/* inline marker (specialized copy) between two compilation units */
		{ "_CamlU3FooIU3BarF3qux",
		  "Foo.<specialization_of>.Bar.qux" },
		/* universal encoding: operator >>= (all non-output chars)
		 * >>=: hex 3e,3e,3d at position 0 (A), empty raw
		 * payload: A3e3e3d_ (8 chars) */
		{ "_CamlU3FooFu8A3e3e3d_",
		  "Foo.>>=" },
		/* universal encoding: let* (* at position 3 = D, hex 2a)
		 * payload: D2a_let (7 chars) */
		{ "_CamlU3FooFu7D2a_let",
		  "Foo.let*" },
		/* universal encoding: func'sub' (two insertions)
		 * ' at position 4 = E, hex 27; ' at relative position 3 = D, hex 27
		 * payload: E27D27_funcsub (14 chars) */
		{ "_CamlU3FooFu14E27D27_funcsub",
		  "Foo.func'sub'" },
		/* anonymous function with file location
		 * Anonymous_function(334, 0, "stdlib.ml")
		 * location string: stdlib.ml_334_0
		 * . at position 6 = G, hex 2e; raw: stdlibml_334_0
		 * payload: G2e_stdlibml_334_0 (18 chars) */
		{ "_CamlU6StdlibLu18G2e_stdlibml_334_0",
		  "Stdlib.fn(stdlib.ml:334:0)" },
		/* anonymous module (struct) without file
		 * Anonymous_module(42, 7, None) -> location string: _42_7
		 * all output chars, no escaping needed (5 chars) */
		{ "_CamlU3FooS5_42_7",
		  "Foo.mod(:42:7)" },
		/* partial application without file
		 * Partial_function(10, 5, None) -> location string: _10_5
		 * all output chars, no escaping needed (5 chars) */
		{ "_CamlU3FooP5_10_5",
		  "Foo.partial(:10:5)" },
		/* nested modules and function */
		{ "_CamlU3FooM3BarM3BazF6my_fun",
		  "Foo.Bar.Baz.my_fun" },
		/* universal encoding: identifier starting with digit
		 * string "0foo" starts with digit -> requires universal encoding
		 * no non-output chars, so escaped is empty, raw = 0foo
		 * payload: _0foo (5 chars) */
		{ "_CamlU3FooFu5_0foo",
		  "Foo.0foo" },

		/* nested module + class + function */
		{ "_CamlU3FooM3BarO5ShapeF4area",
		  "Foo.Bar.Shape.area" },
		/* nested anonymous functions with file locations */
		{ "_CamlU3FooF3barLu14D2e_fooml_3_15Lu14D2e_fooml_4_22F4fn_7",
		  "Foo.bar.fn(foo.ml:3:15).fn(foo.ml:4:22).fn_7" },
		/* anonymous struct with a file location */
		{ "_CamlU3FooSu14D2e_fooml_2_10F4initF4fn_5_6",
		  "Foo.mod(foo.ml:2:10).init.fn_5_6" },
		/* partial application with a file location */
		{ "_CamlU3FooF3barPu14D2e_fooml_9_15",
		  "Foo.bar.partial(foo.ml:9:15)" },

		/* class (O) with a generated method closure */
		{ "_CamlU7FixtureO7counterF4fn_3_11_code",
		  "Fixture.counter.fn_3" },
		/* partial application: OxCmaml drops the Partial_function (P)
		 * scope and emits a named "partial_<fn>" closure (F tag), so a
		 * real partial app looks like an ordinary function */
		{ "_CamlU6Ptest2F14partial_add3_3_3_code",
		  "Ptest2.partial_add3_3" },
		/* local module nested inside a function (F -> M -> F) */
		{ "_CamlU9E2e_ocamlF17with_local_moduleM1MF8scale_23_103_code",
		  "E2e_ocaml.with_local_module.M.scale_23" },
		/* deeply nested modules with a module inside a function */
		{ "_CamlU9E2e_ocamlM9Deep_nestM6Layer1M6Layer2F10down_we_goM8Way_downF7bump_56_136_code",
		  "E2e_ocaml.Deep_nest.Layer1.Layer2.down_we_go.Way_down.bump_56" },
		/* inline marker before a real cross-unit specialization path */
		{ "_CamlU10E2e_inlineIU14E2e_inline_libF9iterate_nF6loop_7_25_code",
		  "E2e_inline.<specialization_of>.E2e_inline_lib.iterate_n.loop_7" },
		/* anonymous function with a real file-basename location */
		{ "_CamlU10E2e_inlineLu22K2e_e2e_inlineml_52_30F4fn_5_21_code",
		  "E2e_inline.fn(e2e_inline.ml:52:30).fn_5" },
		/* anonymous struct with a real location, inside an inline path */
		{ "_CamlU10E2e_inlineIU14E2e_inline_libSu26O2e_e2e_inline_libml_62_10F8apply_20_38_code",
		  "E2e_inline.<specialization_of>.E2e_inline_lib.mod(e2e_inline_lib.ml:62:10).apply_20" },

		/* === Suffix handling ===
		 * Compiler appends _<stamp>_code or _<stamp> after the path. */

		/* closure suffix _<stamp>_code is dropped */
		{ "_CamlU4MainF11say_hello_0_5_code",
		  "Main.say_hello_0" },
		{ "_CamlU4MainM4TestF5foo_1_6_code",
		  "Main.Test.foo_1" },
		{ "_CamlU12Stdlib__ListF6map_15_113_code",
		  "Stdlib__List.map_15" },
		/* a non-_code suffix (here _300) is kept verbatim */
		{ "_CamlU4MainSu15E2e_testml_5_15_300",
		  "Main.mod(test.ml:5:15)_300" },

		/* === Malformed structured symbols (rejected) === */

		/* invalid path-item tag ('X' is not U/I/M/S/O/F/L/P) */
		{ "_CamlU3FooXXX",
		  NULL },
		/* claimed identifier length overruns the remaining input */
		{ "_CamlU99Foo",
		  NULL },
		/* missing decimal length after a path-item tag */
		{ "_CamlUFoo",
		  NULL },
		/* zero-length identifier payload is malformed */
		{ "_CamlU0M3Bar",
		  NULL },
		/* universal index past the end of the raw section */
		{ "_CamlU3FooFu6G1234_funcsub",
		  NULL },
		/* non-hex digit in the escaped section */
		{ "_CamlU3FooFu11G2g_funcsub",
		  NULL },
		/* uppercase hex is not accepted in the escaped section */
		{ "_CamlU3FooFu11G2A_funcsub",
		  NULL },
	};

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		buf = dso__demangle_sym(/*dso=*/NULL, /*kmodule=*/0, test_cases[i].mangled);
		if ((buf == NULL && test_cases[i].demangled != NULL)
				|| (buf != NULL && test_cases[i].demangled == NULL)
				|| (buf != NULL && strcmp(buf, test_cases[i].demangled))) {
			pr_debug("FAILED: %s: %s != %s\n", test_cases[i].mangled,
				 buf == NULL ? "(null)" : buf,
				 test_cases[i].demangled == NULL ? "(null)" : test_cases[i].demangled);
			ret = TEST_FAIL;
		}
		free(buf);
	}

	return ret;
}

DEFINE_SUITE("Demangle OCaml", demangle_ocaml);
