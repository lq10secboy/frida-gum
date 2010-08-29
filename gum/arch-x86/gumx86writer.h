/*
 * Copyright (C) 2009-2010 Ole Andr� Vadla Ravn�s <ole.andre.ravnas@tandberg.com>
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

#ifndef __GUM_X86_WRITER_H__
#define __GUM_X86_WRITER_H__

#include "gumdefs.h"

G_BEGIN_DECLS

typedef struct _GumX86Writer GumX86Writer;
typedef enum _GumArgType      GumArgType;
typedef enum _GumCpuReg       GumCpuReg;
typedef enum _GumPtrTarget    GumPtrTarget;
typedef enum _GumBranchHint   GumBranchHint;

typedef struct _GumLabelMapping GumLabelMapping;
typedef struct _GumLabelRef GumLabelRef;

typedef guint64 GumAddress;

#define GUM_ADDRESS(a) ((GumAddress) a)

struct _GumX86Writer
{
  GumCpuType target_cpu;

  guint8 * base;
  guint8 * code;

  GumLabelMapping * id_to_address;
  guint id_to_address_len;

  GumLabelRef * label_refs;
  guint label_refs_len;
};

enum _GumArgType
{
  GUM_ARG_POINTER,
  GUM_ARG_REGISTER
};

enum _GumCpuReg
{
  /* 32 bit */
  GUM_REG_EAX = 0,
  GUM_REG_ECX,
  GUM_REG_EDX,
  GUM_REG_EBX,
  GUM_REG_ESP,
  GUM_REG_EBP,
  GUM_REG_ESI,
  GUM_REG_EDI,

  GUM_REG_R8D,
  GUM_REG_R9D,
  GUM_REG_R10D,
  GUM_REG_R11D,
  GUM_REG_R12D,
  GUM_REG_R13D,
  GUM_REG_R14D,
  GUM_REG_R15D,

  GUM_REG_EIP,

  /* 64 bit */
  GUM_REG_RAX,
  GUM_REG_RCX,
  GUM_REG_RDX,
  GUM_REG_RBX,
  GUM_REG_RSP,
  GUM_REG_RBP,
  GUM_REG_RSI,
  GUM_REG_RDI,

  GUM_REG_R8,
  GUM_REG_R9,
  GUM_REG_R10,
  GUM_REG_R11,
  GUM_REG_R12,
  GUM_REG_R13,
  GUM_REG_R14,
  GUM_REG_R15,

  GUM_REG_RIP,

  /* Meta */
  GUM_REG_XAX,
  GUM_REG_XCX,
  GUM_REG_XDX,
  GUM_REG_XBX,
  GUM_REG_XSP,
  GUM_REG_XBP,
  GUM_REG_XSI,
  GUM_REG_XDI,

  GUM_REG_XIP,

  GUM_REG_NONE
};

enum _GumPtrTarget
{
  GUM_PTR_BYTE,
  GUM_PTR_DWORD,
  GUM_PTR_QWORD
};

enum _GumBranchHint
{
  GUM_NO_HINT,
  GUM_LIKELY,
  GUM_UNLIKELY
};

void gum_x86_writer_init (GumX86Writer * writer, gpointer code_address);
void gum_x86_writer_reset (GumX86Writer * writer, gpointer code_address);
void gum_x86_writer_free (GumX86Writer * writer);

void gum_x86_writer_set_target_cpu (GumX86Writer * writer, GumCpuType cpu_type);

gpointer gum_x86_writer_cur (GumX86Writer * self);
guint gum_x86_writer_offset (GumX86Writer * self);

void gum_x86_writer_flush (GumX86Writer * self);

void gum_x86_writer_put_label (GumX86Writer * self, gconstpointer id);

void gum_x86_writer_put_call_with_arguments (GumX86Writer * self, gpointer func, guint n_args, ...);
void gum_x86_writer_put_call (GumX86Writer * self, gconstpointer target);
void gum_x86_writer_put_call_reg (GumX86Writer * self, GumCpuReg reg);
void gum_x86_writer_put_call_indirect (GumX86Writer * self, gconstpointer * addr);
void gum_x86_writer_put_call_near_label (GumX86Writer * self, gconstpointer label_id);
void gum_x86_writer_put_ret (GumX86Writer * self);
void gum_x86_writer_put_jmp (GumX86Writer * self, gconstpointer target);
void gum_x86_writer_put_jmp_short_label (GumX86Writer * self, gconstpointer label_id);
void gum_x86_writer_put_jcc_short_label (GumX86Writer * self, guint8 opcode, gconstpointer label_id);
void gum_x86_writer_put_jcc_near (GumX86Writer * self, guint8 opcode, gconstpointer target);
void gum_x86_writer_put_jmp_reg (GumX86Writer * self, GumCpuReg reg);
void gum_x86_writer_put_jmp_reg_ptr (GumX86Writer * self, GumCpuReg reg);
void gum_x86_writer_put_jz (GumX86Writer * self, gconstpointer target, GumBranchHint hint);
void gum_x86_writer_put_jz_label (GumX86Writer * self, gconstpointer label_id, GumBranchHint hint);
void gum_x86_writer_put_jle (GumX86Writer * self, gconstpointer target, GumBranchHint hint);
void gum_x86_writer_put_jle_label (GumX86Writer * self, gconstpointer label_id, GumBranchHint hint);

void gum_x86_writer_put_add_reg_imm (GumX86Writer * self, GumCpuReg reg, gssize imm_value);
void gum_x86_writer_put_add_reg_reg (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);
void gum_x86_writer_put_sub_reg_imm (GumX86Writer * self, GumCpuReg reg, gssize imm_value);
void gum_x86_writer_put_sub_reg_reg (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);
void gum_x86_writer_put_inc_reg (GumX86Writer * self, GumCpuReg reg);
void gum_x86_writer_put_dec_reg (GumX86Writer * self, GumCpuReg reg);
void gum_x86_writer_put_inc_reg_ptr (GumX86Writer * self, GumPtrTarget target, GumCpuReg reg);
void gum_x86_writer_put_dec_reg_ptr (GumX86Writer * self, GumPtrTarget target, GumCpuReg reg);
void gum_x86_writer_put_lock_xadd_reg_ptr_reg (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);
void gum_x86_writer_put_lock_cmpxchg_reg_ptr_reg (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);
void gum_x86_writer_put_lock_inc_imm32_ptr (GumX86Writer * self, gpointer target);
void gum_x86_writer_put_lock_dec_imm32_ptr (GumX86Writer * self, gpointer target);

void gum_x86_writer_put_and_reg_u32 (GumX86Writer * self, GumCpuReg reg, guint32 imm_value);
void gum_x86_writer_put_shl_reg_u8 (GumX86Writer * self, GumCpuReg reg, guint8 imm_value);
void gum_x86_writer_put_xor_reg_reg (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);

void gum_x86_writer_put_mov_reg_reg (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);
void gum_x86_writer_put_mov_reg_u32 (GumX86Writer * self, GumCpuReg dst_reg, guint32 imm_value);
void gum_x86_writer_put_mov_reg_u64 (GumX86Writer * self, GumCpuReg dst_reg, guint64 imm_value);
void gum_x86_writer_put_mov_reg_address (GumX86Writer * self, GumCpuReg dst_reg, GumAddress address);
void gum_x86_writer_put_mov_reg_ptr_u32 (GumX86Writer * self, GumCpuReg dst_reg, guint32 imm_value);
void gum_x86_writer_put_mov_reg_offset_ptr_u32 (GumX86Writer * self, GumCpuReg dst_reg, gssize dst_offset, guint32 imm_value);
void gum_x86_writer_put_mov_reg_ptr_reg (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);
void gum_x86_writer_put_mov_reg_offset_ptr_reg (GumX86Writer * self, GumCpuReg dst_reg, gssize dst_offset, GumCpuReg src_reg);
void gum_x86_writer_put_mov_reg_reg_ptr (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg);
void gum_x86_writer_put_mov_reg_reg_offset_ptr (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg, gssize src_offset);
void gum_x86_writer_put_mov_reg_base_index_scale_offset_ptr (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg base_reg, GumCpuReg index_reg, guint8 scale, gssize offset);

void gum_x86_writer_put_mov_fs_u32_ptr_reg (GumX86Writer * self, guint32 fs_offset, GumCpuReg src_reg);
void gum_x86_writer_put_mov_reg_fs_u32_ptr (GumX86Writer * self, GumCpuReg dst_reg, guint32 fs_offset);
void gum_x86_writer_put_mov_gs_u32_ptr_reg (GumX86Writer * self, guint32 fs_offset, GumCpuReg src_reg);
void gum_x86_writer_put_mov_reg_gs_u32_ptr (GumX86Writer * self, GumCpuReg dst_reg, guint32 fs_offset);

void gum_x86_writer_put_movq_xmm0_esp_offset_ptr (GumX86Writer * self, gint8 offset);
void gum_x86_writer_put_movq_eax_offset_ptr_xmm0 (GumX86Writer * self, gint8 offset);
void gum_x86_writer_put_movdqu_xmm0_esp_offset_ptr (GumX86Writer * self, gint8 offset);
void gum_x86_writer_put_movdqu_eax_offset_ptr_xmm0 (GumX86Writer * self, gint8 offset);

void gum_x86_writer_put_lea_reg_reg_offset (GumX86Writer * self, GumCpuReg dst_reg, GumCpuReg src_reg, gssize src_offset);

void gum_x86_writer_put_xchg_reg_reg_ptr (GumX86Writer * self, GumCpuReg left_reg, GumCpuReg right_reg);

void gum_x86_writer_put_push_u32 (GumX86Writer * self, guint32 imm_value);
void gum_x86_writer_put_push_reg (GumX86Writer * self, GumCpuReg reg);
void gum_x86_writer_put_pop_reg (GumX86Writer * self, GumCpuReg reg);
void gum_x86_writer_put_push_imm_ptr (GumX86Writer * self, gconstpointer imm_ptr);
void gum_x86_writer_put_pushax (GumX86Writer * self);
void gum_x86_writer_put_popax (GumX86Writer * self);
void gum_x86_writer_put_pushfx (GumX86Writer * self);
void gum_x86_writer_put_popfx (GumX86Writer * self);

void gum_x86_writer_put_test_reg_reg (GumX86Writer * self, GumCpuReg reg_a, GumCpuReg reg_b);
void gum_x86_writer_put_cmp_reg_i32 (GumX86Writer * self, GumCpuReg reg, gint32 imm_value);
void gum_x86_writer_put_cmp_imm_ptr_imm_u32 (GumX86Writer * self, gconstpointer imm_ptr, guint32 imm_value);
void gum_x86_writer_put_clc (GumX86Writer * self);
void gum_x86_writer_put_stc (GumX86Writer * self);

void gum_x86_writer_put_cpuid (GumX86Writer * self);
void gum_x86_writer_put_lfence (GumX86Writer * self);
void gum_x86_writer_put_rdtsc (GumX86Writer * self);
void gum_x86_writer_put_pause (GumX86Writer * self);
void gum_x86_writer_put_nop (GumX86Writer * self);
void gum_x86_writer_put_int3 (GumX86Writer * self);

void gum_x86_writer_put_byte (GumX86Writer * self, guint8 b);
void gum_x86_writer_put_bytes (GumX86Writer * self, const guint8 * data, guint n);

G_END_DECLS

#endif