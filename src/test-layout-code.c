/*
  Gpredict-Blue: Real-time satellite tracking and orbit prediction program

  Copyright (C)  2025  Jan Koch DN9JUR

  Unit tests for module layout pane code strings (preferences sidebar editor).
*/
#ifdef HAVE_CONFIG_H
#include <build-config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtk-sat-module.h"
#include "layout-code.h"

static int      failures = 0;

static void
fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    failures++;
}

static void
ok(const char *msg)
{
    fprintf(stdout, "ok %s\n", msg);
}

static void
test_predef_parse(void)
{
    GArray         *panes = NULL;
    const gchar    *code;

    code = layout_code_predef_string(1);
    if (!layout_code_parse(code, &panes) || panes == NULL || panes->len != 1)
        fail("predef 1 should yield one pane");
    else
        ok("predef 1 parse");

    if (panes != NULL) {
        layout_pane_t   pane = g_array_index(panes, layout_pane_t, 0);

        if (pane.type != 1 || pane.left != 0 || pane.right != 2)
            fail("predef 1 pane geometry");
        else
            ok("predef 1 geometry");
        g_array_free(panes, TRUE);
    }
}

static void
test_roundtrip(void)
{
    GArray         *panes = NULL;
    gchar          *formatted;

    if (!layout_code_parse(layout_code_predef_string(3), &panes)
        || panes == NULL)
        fail("parse predef 3");
    formatted = layout_code_format(panes);
    if (formatted == NULL
        || strcmp(formatted, layout_code_predef_string(3)) != 0)
        fail("roundtrip predef 3");
    else
        ok("roundtrip predef 3");
    g_free(formatted);
    g_array_free(panes, TRUE);
}

static void
test_match_predef(void)
{
    if (layout_code_match_predef(layout_code_predef_string(4)) != 4)
        fail("match predef index 4");
    else
        ok("match predef 4");

    if (layout_code_match_predef("9;9;9;9;9") != LAYOUT_PREDEF_NUM - 1)
        fail("unknown code should be custom");
    else
        ok("custom preset index");
}

static void
test_invalid_pane(void)
{
    layout_pane_t   bad = { 0, 0, 0, 0, 1 };

    if (layout_pane_valid(&bad, GTK_SAT_MOD_VIEW_NUM))
        fail("right<=left invalid");
    else
        ok("reject invalid pane");
}

int
main(void)
{
    test_predef_parse();
    test_roundtrip();
    test_match_predef();
    test_invalid_pane();

    if (failures > 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    fprintf(stdout, "All layout-code tests passed.\n");
    return EXIT_SUCCESS;
}
