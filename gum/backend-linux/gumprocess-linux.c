/*
 * Copyright (C) 2010-2015 Ole André Vadla Ravnås <ole.andre.ravnas@tillitech.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumprocess.h"

#include "gumlinux.h"

#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef HAVE_ANDROID
# include <ucontext.h>
#endif
#include <unistd.h>
#include <gio/gio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#define GUM_HIJACK_SIGNAL (SIGRTMIN + 7)

#if GLIB_SIZEOF_VOID_P == 4
typedef Elf32_Ehdr GumElfEHeader;
typedef Elf32_Shdr GumElfSHeader;
typedef Elf32_Sym GumElfSymbol;
# define GUM_ELF_ST_BIND(val) ELF32_ST_BIND(val)
# define GUM_ELF_ST_TYPE(val) ELF32_ST_TYPE(val)
#else
typedef Elf64_Ehdr GumElfEHeader;
typedef Elf64_Shdr GumElfSHeader;
typedef Elf64_Sym GumElfSymbol;
# define GUM_ELF_ST_BIND(val) ELF64_ST_BIND(val)
# define GUM_ELF_ST_TYPE(val) ELF64_ST_TYPE(val)
#endif

#define GUM_MAPS_LINE_SIZE (1024 + PATH_MAX)

typedef struct _GumFindModuleContext GumFindModuleContext;
typedef struct _GumEnumerateModuleRangesContext GumEnumerateModuleRangesContext;
typedef struct _GumFindExportContext GumFindExportContext;

struct _GumFindModuleContext
{
  const gchar * module_name;
  GumAddress base;
  gchar * path;
};

struct _GumEnumerateModuleRangesContext
{
  const gchar * module_name;
  GumFoundRangeFunc func;
  gpointer user_data;
};

struct _GumFindExportContext
{
  GumAddress result;
  const gchar * symbol_name;
};

#ifndef HAVE_ANDROID
static void gum_do_modify_thread (int sig, siginfo_t * siginfo,
    void * context);
static void gum_store_cpu_context (GumThreadId thread_id,
    GumCpuContext * cpu_context, gpointer user_data);
#endif

static gboolean gum_emit_range_if_module_name_matches (
    const GumRangeDetails * details, gpointer user_data);
static gboolean gum_store_base_and_path_if_name_matches (
    const GumModuleDetails * details, gpointer user_data);
static gboolean gum_store_address_if_export_name_matches (
    const GumExportDetails * details, gpointer user_data);

static gboolean gum_module_path_equals (const gchar * path,
    const gchar * name_or_path);

#ifndef HAVE_ANDROID
static void gum_cpu_context_from_linux (const ucontext_t * uc,
    GumCpuContext * ctx);
static void gum_cpu_context_to_linux (const GumCpuContext * ctx,
    ucontext_t * uc);
static GumThreadState gum_thread_state_from_proc_status_character (gchar c);
#endif
static GumPageProtection gum_page_protection_from_proc_perms_string (
    const gchar * perms);

#ifndef HAVE_ANDROID
G_LOCK_DEFINE_STATIC (gum_modify_thread);
static volatile gboolean gum_modify_thread_did_load_cpu_context;
static volatile gboolean gum_modify_thread_did_modify_cpu_context;
static volatile gboolean gum_modify_thread_did_store_cpu_context;
static GumCpuContext gum_modify_thread_cpu_context;
#endif

gboolean
gum_process_is_debugger_attached (void)
{
  gboolean result;
  gchar * status, * p;
  gboolean success;

  success = g_file_get_contents ("/proc/self/status", &status, NULL, NULL);
  g_assert (success);

  p = strstr (status, "TracerPid:");
  g_assert (p != NULL);

  result = atoi (p + 10) != 0;

  g_free (status);

  return result;
}

GumThreadId
gum_process_get_current_thread_id (void)
{
  return syscall (__NR_gettid);
}

gboolean
gum_process_modify_thread (GumThreadId thread_id,
                           GumModifyThreadFunc func,
                           gpointer user_data)
{
  gboolean success = FALSE;
#ifndef HAVE_ANDROID
  struct sigaction action, old_action;

  if (thread_id == gum_process_get_current_thread_id ())
  {
    ucontext_t uc;
    volatile gboolean modified = FALSE;

    getcontext (&uc);
    if (!modified)
    {
      GumCpuContext cpu_context;

      gum_cpu_context_from_linux (&uc, &cpu_context);
      func (thread_id, &cpu_context, user_data);
      gum_cpu_context_to_linux (&cpu_context, &uc);

      modified = TRUE;
      setcontext (&uc);
    }

    success = TRUE;
  }
  else
  {
    G_LOCK (gum_modify_thread);

    gum_modify_thread_did_load_cpu_context = FALSE;
    gum_modify_thread_did_modify_cpu_context = FALSE;
    gum_modify_thread_did_store_cpu_context = FALSE;

    action.sa_sigaction = gum_do_modify_thread;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction (GUM_HIJACK_SIGNAL, &action, &old_action);

    if (syscall (SYS_tgkill, getpid (), thread_id, GUM_HIJACK_SIGNAL) == 0)
    {
      /* FIXME: timeout? */
      while (!gum_modify_thread_did_load_cpu_context)
        g_thread_yield ();
      func (thread_id, &gum_modify_thread_cpu_context, user_data);
      gum_modify_thread_did_modify_cpu_context = TRUE;
      while (!gum_modify_thread_did_store_cpu_context)
        g_thread_yield ();

      success = TRUE;
    }

    sigaction (GUM_HIJACK_SIGNAL, &old_action, NULL);

    G_UNLOCK (gum_modify_thread);
  }
#endif

  return success;
}

#ifndef HAVE_ANDROID
static void
gum_do_modify_thread (int sig,
                      siginfo_t * siginfo,
                      void * context)
{
  ucontext_t * uc = (ucontext_t *) context;

  gum_cpu_context_from_linux (uc, &gum_modify_thread_cpu_context);
  gum_modify_thread_did_load_cpu_context = TRUE;
  while (!gum_modify_thread_did_modify_cpu_context)
    ;
  gum_cpu_context_to_linux (&gum_modify_thread_cpu_context, uc);
  gum_modify_thread_did_store_cpu_context = TRUE;
}
#endif

void
gum_process_enumerate_threads (GumFoundThreadFunc func,
                               gpointer user_data)
{
#ifndef HAVE_ANDROID
  GDir * dir;
  const gchar * name;
  gboolean carry_on = TRUE;

  dir = g_dir_open ("/proc/self/task", 0, NULL);
  g_assert (dir != NULL);

  while (carry_on && (name = g_dir_read_name (dir)) != NULL)
  {
    gchar * path, * info = NULL;

    path = g_strconcat ("/proc/self/task/", name, "/stat", NULL);
    if (g_file_get_contents (path, &info, NULL, NULL))
    {
      gchar * state;
      GumThreadDetails details;

      state = strrchr (info, ')') + 2;

      details.id = atoi (name);
      details.state = gum_thread_state_from_proc_status_character (*state);
      if (gum_process_modify_thread (details.id, gum_store_cpu_context,
            &details.cpu_context))
      {
        carry_on = func (&details, user_data);
      }
    }

    g_free (info);
    g_free (path);
  }

  g_dir_close (dir);
#endif
}

#ifndef HAVE_ANDROID
static void
gum_store_cpu_context (GumThreadId thread_id,
                       GumCpuContext * cpu_context,
                       gpointer user_data)
{
  memcpy (user_data, cpu_context, sizeof (GumCpuContext));
}
#endif

void
gum_process_enumerate_modules (GumFoundModuleFunc func,
                               gpointer user_data)
{
  FILE * fp;
  const guint line_size = GUM_MAPS_LINE_SIZE;
  gchar * line, * path, * prev_path;
  gboolean carry_on = TRUE;

  fp = fopen ("/proc/self/maps", "r");
  g_assert (fp != NULL);

  line = g_malloc (line_size);

  path = g_malloc (PATH_MAX);
  prev_path = g_malloc (PATH_MAX);
  prev_path[0] = '\0';

  while (carry_on && fgets (line, line_size, fp) != NULL)
  {
    const guint8 elf_magic[] = { 0x7f, 'E', 'L', 'F' };
    guint8 * start, * end;
    gchar perms[5] = { 0, };
    gint n;
    gboolean readable;
    gchar * name;
    GumMemoryRange range;
    GumModuleDetails details;

    n = sscanf (line, "%p-%p %4c %*x %*s %*s %s", &start, &end, perms, path);
    if (n == 3)
      continue;
    g_assert_cmpint (n, ==, 4);

    readable = perms[0] == 'r';
    if (strcmp (path, prev_path) == 0 || path[0] == '[')
      continue;
    else if (!readable || memcmp (start, elf_magic, sizeof (elf_magic)) != 0)
      continue;

    name = g_path_get_basename (path);

    range.base_address = GUM_ADDRESS (start);
    range.size = end - start;

    details.name = name;
    details.range = &range;
    details.path = path;

    carry_on = func (&details, user_data);

    g_free (name);

    strcpy (prev_path, path);
  }

  g_free (path);
  g_free (prev_path);

  g_free (line);

  fclose (fp);
}

void
gum_process_enumerate_ranges (GumPageProtection prot,
                              GumFoundRangeFunc func,
                              gpointer user_data)
{
  gum_linux_enumerate_ranges (getpid (), prot, func, user_data);
}

void
gum_linux_enumerate_ranges (pid_t pid,
                            GumPageProtection prot,
                            GumFoundRangeFunc func,
                            gpointer user_data)
{
  gchar * maps_path;
  FILE * fp;
  const guint line_size = GUM_MAPS_LINE_SIZE;
  gchar * line;
  gboolean carry_on = TRUE;

  maps_path = g_strdup_printf ("/proc/%d/maps", pid);

  fp = fopen (maps_path, "r");
  g_assert (fp != NULL);

  g_free (maps_path);

  line = g_malloc (line_size);

  while (carry_on && fgets (line, line_size, fp) != NULL)
  {
    GumRangeDetails details;
    GumMemoryRange range;
    GumFileMapping file;
    GumAddress end;
    gchar perms[5] = { 0, };
    guint64 inode;
    gint length, n;

    n = sscanf (line,
        "%" G_GINT64_MODIFIER "x-%" G_GINT64_MODIFIER "x "
        "%4c "
        "%" G_GINT64_MODIFIER "x %*s %" G_GINT64_MODIFIER "d"
        "%n",
        &range.base_address, &end,
        perms,
        &file.offset, &inode,
        &length);
    g_assert (n == 5 || n == 6);

    range.size = end - range.base_address;

    details.file = NULL;
    if (inode != 0)
    {
      file.path = strchr (line + length, '/');
      if (file.path != NULL)
      {
        *strchr (file.path, '\n') = '\0';
        details.file = &file;
      }
    }

    details.range = &range;
    details.prot = gum_page_protection_from_proc_perms_string (perms);

    if ((details.prot & prot) == prot)
    {
      carry_on = func (&details, user_data);
    }
  }

  g_free (line);

  fclose (fp);
}

void
gum_process_enumerate_malloc_ranges (GumFoundMallocRangeFunc func,
                                     gpointer user_data)
{
  /* Not implemented */
}

void
gum_module_enumerate_exports (const gchar * module_name,
                              GumFoundExportFunc func,
                              gpointer user_data)
{
  GumFindModuleContext ctx = { module_name, 0, NULL };
  gint fd = -1;
  gsize file_size;
  gpointer base_address = NULL;
  GumElfEHeader * ehdr;
  guint i;
  gsize dynsym_section_offset = 0, dynsym_section_size = 0;
  gsize dynsym_entry_size = 0;
  const gchar * dynsym_strtab = NULL;

  gum_process_enumerate_modules (gum_store_base_and_path_if_name_matches, &ctx);
  if (ctx.base == 0)
    goto beach;

  fd = open (ctx.path, O_RDONLY);
  if (fd == -1)
    goto beach;

  file_size = lseek (fd, 0, SEEK_END);
  lseek (fd, 0, SEEK_SET);

  base_address = mmap (NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  g_assert (base_address != MAP_FAILED);

  ehdr = base_address;
  if (ehdr->e_type != ET_DYN)
    goto beach;

  for (i = 0; i != ehdr->e_shnum; i++)
  {
    GumElfSHeader * shdr;

    shdr = base_address + ehdr->e_shoff + (i * ehdr->e_shentsize);
    if (shdr->sh_type == SHT_DYNSYM)
    {
      GumElfSHeader * strtab_shdr;

      dynsym_section_offset = shdr->sh_offset;
      dynsym_section_size = shdr->sh_size;
      dynsym_entry_size = shdr->sh_entsize;

      strtab_shdr = base_address + ehdr->e_shoff +
          (shdr->sh_link * ehdr->e_shentsize);
      dynsym_strtab = base_address + strtab_shdr->sh_offset;

      g_assert_cmpuint (dynsym_section_size % dynsym_entry_size, ==, 0);
    }
  }

  if (dynsym_section_offset == 0)
    goto beach;

  for (i = 0; i != dynsym_section_size / dynsym_entry_size; i++)
  {
    GumElfSymbol * sym;

    sym = base_address + dynsym_section_offset + (i * dynsym_entry_size);
    if ((GUM_ELF_ST_BIND (sym->st_info) == STB_GLOBAL ||
         GUM_ELF_ST_BIND (sym->st_info) == STB_WEAK) &&
        sym->st_shndx != SHN_UNDEF)
    {
      GumExportDetails details;

      details.type = GUM_ELF_ST_TYPE (sym->st_info) == STT_FUNC
          ? GUM_EXPORT_FUNCTION
          : GUM_EXPORT_VARIABLE;
      details.name = dynsym_strtab + sym->st_name;
      details.address = ctx.base + sym->st_value;

      if (!func (&details, user_data))
        goto beach;
    }
  }

beach:
  if (base_address != NULL)
    munmap (base_address, file_size);

  if (fd != -1)
    close (fd);

  g_free (ctx.path);
}

void
gum_module_enumerate_ranges (const gchar * module_name,
                             GumPageProtection prot,
                             GumFoundRangeFunc func,
                             gpointer user_data)
{
  GumEnumerateModuleRangesContext ctx;

  ctx.module_name = module_name;
  ctx.func = func;
  ctx.user_data = user_data;

  gum_process_enumerate_ranges (prot, gum_emit_range_if_module_name_matches,
      &ctx);
}

GumAddress
gum_module_find_base_address (const gchar * module_name)
{
  GumFindModuleContext ctx = { module_name, 0, NULL };
  gum_process_enumerate_modules (gum_store_base_and_path_if_name_matches, &ctx);
  g_free (ctx.path);
  return ctx.base;
}

GumAddress
gum_module_find_export_by_name (const gchar * module_name,
                                const gchar * symbol_name)
{
  GumFindExportContext ctx;

  ctx.result = 0;
  ctx.symbol_name = symbol_name;

  gum_module_enumerate_exports (module_name,
      gum_store_address_if_export_name_matches, &ctx);

  return ctx.result;
}

static gboolean
gum_emit_range_if_module_name_matches (const GumRangeDetails * details,
                                       gpointer user_data)
{
  GumEnumerateModuleRangesContext * ctx = user_data;

  if (details->file == NULL)
    return TRUE;
  else if (!gum_module_path_equals (details->file->path, ctx->module_name))
    return TRUE;

  return ctx->func (details, ctx->user_data);
}

static gboolean
gum_store_base_and_path_if_name_matches (const GumModuleDetails * details,
                                         gpointer user_data)
{
  GumFindModuleContext * ctx = user_data;

  if (!gum_module_path_equals (details->path, ctx->module_name))
    return TRUE;

  ctx->base = details->range->base_address;
  ctx->path = g_strdup (details->path);
  return FALSE;
}

static gboolean
gum_store_address_if_export_name_matches (const GumExportDetails * details,
                                          gpointer user_data)
{
  GumFindExportContext * ctx = (GumFindExportContext *) user_data;

  if (strcmp (details->name, ctx->symbol_name) == 0)
  {
    ctx->result = details->address;
    return FALSE;
  }

  return TRUE;
}

GumCpuType
gum_linux_cpu_type_from_file (const gchar * path,
                              GError ** error)
{
  GumCpuType result = -1;
  GFile * file;
  GFileInputStream * base_stream;
  GDataInputStream * stream = NULL;
  GError * read_error;
  guint16 e_machine;

  file = g_file_new_for_path (path);

  base_stream = g_file_read (file, NULL, error);
  if (base_stream == NULL)
    goto beach;

  if (!g_seekable_seek (G_SEEKABLE (base_stream), 0x12, G_SEEK_SET, NULL,
      error))
    goto beach;

  stream = g_data_input_stream_new (G_INPUT_STREAM (base_stream));
  g_data_input_stream_set_byte_order (stream,
      G_DATA_STREAM_BYTE_ORDER_LITTLE_ENDIAN);

  read_error = NULL;
  e_machine = g_data_input_stream_read_uint16 (stream, NULL, &read_error);
  if (read_error != NULL)
  {
    g_propagate_error (error, read_error);
    goto beach;
  }

  switch (e_machine)
  {
    case 0x0003:
      result = GUM_CPU_IA32;
      break;
    case 0x003e:
      result = GUM_CPU_AMD64;
      break;
    case 0x0028:
      result = GUM_CPU_ARM;
      break;
    case 0x00b7:
      result = GUM_CPU_ARM64;
      break;
    default:
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
          "Unsupported executable");
      break;
  }

beach:
  if (stream != NULL)
    g_object_unref (stream);

  if (base_stream != NULL)
    g_object_unref (base_stream);

  g_object_unref (file);

  return result;
}

GumCpuType
gum_linux_cpu_type_from_pid (pid_t pid,
                             GError ** error)
{
  GumCpuType result = -1;
  gchar * auxv_path;
  guint8 * auxv;
  gsize auxv_size, i;

  auxv_path = g_strdup_printf ("/proc/%d/auxv", pid);

  if (!g_file_get_contents (auxv_path, (gchar **) &auxv, &auxv_size, error))
    goto beach;

#ifdef HAVE_I386
  result = GUM_CPU_AMD64;
#else
  result = GUM_CPU_ARM64;
#endif

  for (i = 0; i < auxv_size; i += 16)
  {
    if (auxv[4] != 0 || auxv[5] != 0 ||
        auxv[6] != 0 || auxv[7] != 0)
    {
#ifdef HAVE_I386
      result = GUM_CPU_IA32;
#else
      result = GUM_CPU_ARM;
#endif
      break;
    }
  }

beach:
  g_free (auxv_path);

  return result;
}

static gboolean
gum_module_path_equals (const gchar * path,
                        const gchar * name_or_path)
{
  gchar * s;

  if (name_or_path[0] == '/')
    return strcmp (name_or_path, path) == 0;

  if ((s = strrchr (path, '/')) != NULL)
    return strcmp (name_or_path, s + 1) == 0;

  return strcmp (name_or_path, path) == 0;
}

#ifndef HAVE_ANDROID

static void
gum_cpu_context_from_linux (const ucontext_t * uc,
                            GumCpuContext * ctx)
{
#if defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 4
  const greg_t * gr = uc->uc_mcontext.gregs;

  ctx->eip = gr[REG_EIP];

  ctx->edi = gr[REG_EDI];
  ctx->esi = gr[REG_ESI];
  ctx->ebp = gr[REG_EBP];
  ctx->esp = gr[REG_ESP];
  ctx->ebx = gr[REG_EBX];
  ctx->edx = gr[REG_EDX];
  ctx->ecx = gr[REG_ECX];
  ctx->eax = gr[REG_EAX];
#elif defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 8
  const greg_t * gr = uc->uc_mcontext.gregs;

  ctx->rip = gr[REG_RIP];

  ctx->r15 = gr[REG_R15];
  ctx->r14 = gr[REG_R14];
  ctx->r13 = gr[REG_R13];
  ctx->r12 = gr[REG_R12];
  ctx->r11 = gr[REG_R11];
  ctx->r10 = gr[REG_R10];
  ctx->r9 = gr[REG_R9];
  ctx->r8 = gr[REG_R8];

  ctx->rdi = gr[REG_RDI];
  ctx->rsi = gr[REG_RSI];
  ctx->rbp = gr[REG_RBP];
  ctx->rsp = gr[REG_RSP];
  ctx->rbx = gr[REG_RBX];
  ctx->rdx = gr[REG_RDX];
  ctx->rcx = gr[REG_RCX];
  ctx->rax = gr[REG_RAX];
#else
# error FIXME
#endif
}

static void
gum_cpu_context_to_linux (const GumCpuContext * ctx,
                          ucontext_t * uc)
{
#if defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 4
  greg_t * gr = uc->uc_mcontext.gregs;

  gr[REG_EIP] = ctx->eip;

  gr[REG_EDI] = ctx->edi;
  gr[REG_ESI] = ctx->esi;
  gr[REG_EBP] = ctx->ebp;
  gr[REG_ESP] = ctx->esp;
  gr[REG_EBX] = ctx->ebx;
  gr[REG_EDX] = ctx->edx;
  gr[REG_ECX] = ctx->ecx;
  gr[REG_EAX] = ctx->eax;
#elif defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 8
  greg_t * gr = uc->uc_mcontext.gregs;

  gr[REG_RIP] = ctx->rip;

  gr[REG_R15] = ctx->r15;
  gr[REG_R14] = ctx->r14;
  gr[REG_R13] = ctx->r13;
  gr[REG_R12] = ctx->r12;
  gr[REG_R11] = ctx->r11;
  gr[REG_R10] = ctx->r10;
  gr[REG_R9] = ctx->r9;
  gr[REG_R8] = ctx->r8;

  gr[REG_RDI] = ctx->rdi;
  gr[REG_RSI] = ctx->rsi;
  gr[REG_RBP] = ctx->rbp;
  gr[REG_RSP] = ctx->rsp;
  gr[REG_RBX] = ctx->rbx;
  gr[REG_RDX] = ctx->rdx;
  gr[REG_RCX] = ctx->rcx;
  gr[REG_RAX] = ctx->rax;
#else
# error FIXME
#endif
}

static GumThreadState
gum_thread_state_from_proc_status_character (gchar c)
{
  switch (g_ascii_toupper (c))
  {
    case 'R': return GUM_THREAD_RUNNING;
    case 'S': return GUM_THREAD_WAITING;
    case 'D': return GUM_THREAD_UNINTERRUPTIBLE;
    case 'Z': return GUM_THREAD_UNINTERRUPTIBLE;
    case 'T': return GUM_THREAD_STOPPED;
    case 'W': return GUM_THREAD_UNINTERRUPTIBLE;
    default:
      g_assert_not_reached ();
      break;
  }
}

#endif /* !HAVE_ANDROID */

static GumPageProtection
gum_page_protection_from_proc_perms_string (const gchar * perms)
{
  GumPageProtection prot = GUM_PAGE_NO_ACCESS;

  if (perms[0] == 'r')
    prot |= GUM_PAGE_READ;
  if (perms[1] == 'w')
    prot |= GUM_PAGE_WRITE;
  if (perms[2] == 'x')
    prot |= GUM_PAGE_EXECUTE;

  return prot;
}
