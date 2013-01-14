/*
 * Copyright (C) 2009-2010 Ole Andr� Vadla Ravn�s <ole.andre.ravnas@tandberg.com>
 * Copyright (C)      2010 Karl Trygve Kalleberg <karltk@boblycat.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gumstalker.h"

#include "fakeeventsink.h"
#include "gumx86writer.h"
#include "gummemory.h"
#include "testutil.h"

#include <stdlib.h>
#include <string.h>
#ifdef G_OS_WIN32
#define VC_EXTRALEAN
#include <windows.h>
#include <tchar.h>
#endif

#define STALKER_TESTCASE(NAME) \
    void test_stalker_ ## NAME ( \
        TestStalkerFixture * fixture, gconstpointer data)
#define STALKER_TESTENTRY(NAME) \
    TEST_ENTRY_WITH_FIXTURE ("Core/Stalker", test_stalker, NAME, \
        TestStalkerFixture)

#if defined (G_OS_WIN32) && GLIB_SIZEOF_VOID_P == 4
# define STALKER_TESTFUNC __fastcall
#else
# define STALKER_TESTFUNC
#endif

#define NTH_EVENT_AS_CALL(N) \
    (gum_fake_event_sink_get_nth_event_as_call (fixture->sink, N))
#define NTH_EVENT_AS_RET(N) \
    (gum_fake_event_sink_get_nth_event_as_ret (fixture->sink, N))
#define NTH_EXEC_EVENT_LOCATION(N) \
    (gum_fake_event_sink_get_nth_event_as_exec (fixture->sink, N)->location)

typedef struct _TestStalkerFixture
{
  GumStalker * stalker;
  GumFakeEventSink * sink;

  guint8 * code;
  guint8 * last_invoke_calladdr;
  guint8 * last_invoke_retaddr;
} TestStalkerFixture;

typedef gint (STALKER_TESTFUNC * StalkerTestFunc) (gint arg);

static void
test_stalker_fixture_setup (TestStalkerFixture * fixture,
                            gconstpointer data)
{
  fixture->stalker = gum_stalker_new ();
  fixture->sink = GUM_FAKE_EVENT_SINK (gum_fake_event_sink_new ());

#ifdef G_OS_WIN32
  if (IsDebuggerPresent ())
  {
    static gboolean shown_once = FALSE;

    if (!shown_once)
    {
      g_print ("\n\nWARNING:\tRunning Stalker tests with debugger attached "
          "is not supported.\n\t\tSome tests will fail.\n\n");
      shown_once = TRUE;
    }
  }
#endif
}

static void
test_stalker_fixture_teardown (TestStalkerFixture * fixture,
                               gconstpointer data)
{
  g_object_unref (fixture->sink);
  g_object_unref (fixture->stalker);

  if (fixture->code != NULL)
    gum_free_pages (fixture->code);
}

static guint8 *
test_stalker_fixture_dup_code (TestStalkerFixture * fixture,
                               const guint8 * tpl_code,
                               guint tpl_size)
{
  GumAddressSpec spec;

  spec.near_address = gum_stalker_follow_me;
  spec.max_distance = G_MAXINT32 / 2;

  if (fixture->code != NULL)
    gum_free_pages (fixture->code);
  fixture->code = (guint8 *) gum_alloc_n_pages_near (
      (tpl_size / gum_query_page_size ()) + 1, GUM_PAGE_RWX, &spec);
  memcpy (fixture->code, tpl_code, tpl_size);
  return fixture->code;
}

#if GLIB_SIZEOF_VOID_P == 4
# define INVOKER_INSN_COUNT 11
# define INVOKER_IMPL_OFFSET 5
#else
# define INVOKER_INSN_COUNT 10
# define INVOKER_IMPL_OFFSET 4
#endif

/* custom invoke code as we want to stalk a deterministic code sequence */
static gint
test_stalker_fixture_follow_and_invoke (TestStalkerFixture * fixture,
                                        StalkerTestFunc func,
                                        gint arg)
{
  GumAddressSpec spec;
  gint ret;
  guint8 * code;
  GumX86Writer cw;
#if GLIB_SIZEOF_VOID_P == 4
  guint align_correction_follow = 4;
  guint align_correction_call = 12;
  guint align_correction_unfollow = 8;
#else
  guint align_correction_follow = 8;
  guint align_correction_call = 0;
  guint align_correction_unfollow = 8;
#endif
  GCallback invoke_func;

  spec.near_address = gum_stalker_follow_me;
  spec.max_distance = G_MAXINT32 / 2;

  code = (guint8 *) gum_alloc_n_pages_near (1, GUM_PAGE_RWX, &spec);

  gum_x86_writer_init (&cw, code);

  gum_x86_writer_put_pushax (&cw);

  gum_x86_writer_put_sub_reg_imm (&cw, GUM_REG_XSP, align_correction_follow);
  gum_x86_writer_put_call_with_arguments (&cw,
      gum_stalker_follow_me, 2,
      GUM_ARG_POINTER, fixture->stalker,
      GUM_ARG_POINTER, fixture->sink);
  gum_x86_writer_put_add_reg_imm (&cw, GUM_REG_XSP, align_correction_follow);

  gum_x86_writer_put_sub_reg_imm (&cw, GUM_REG_XSP, align_correction_call);
  gum_x86_writer_put_mov_reg_u32 (&cw, GUM_REG_ECX, arg);
  fixture->last_invoke_calladdr = (guint8 *) gum_x86_writer_cur (&cw);
  gum_x86_writer_put_call (&cw, func);
  fixture->last_invoke_retaddr = (guint8 *) gum_x86_writer_cur (&cw);
  gum_x86_writer_put_mov_reg_address (&cw, GUM_REG_XCX, GUM_ADDRESS (&ret));
  gum_x86_writer_put_mov_reg_ptr_reg (&cw, GUM_REG_XCX, GUM_REG_EAX);
  gum_x86_writer_put_add_reg_imm (&cw, GUM_REG_XSP, align_correction_call);

  gum_x86_writer_put_sub_reg_imm (&cw, GUM_REG_XSP, align_correction_unfollow);
  gum_x86_writer_put_call_with_arguments (&cw,
      gum_stalker_unfollow_me, 1,
      GUM_ARG_POINTER, fixture->stalker);
  gum_x86_writer_put_add_reg_imm (&cw, GUM_REG_XSP, align_correction_unfollow);

  gum_x86_writer_put_popax (&cw);

  gum_x86_writer_put_ret (&cw);

  gum_x86_writer_free (&cw);

  invoke_func = GUM_POINTER_TO_FUNCPTR (GCallback, code);
  invoke_func ();

  gum_free_pages (code);

  return ret;
}

static void pretend_workload (void);

gint gum_stalker_dummy_global_to_trick_optimizer = 0;