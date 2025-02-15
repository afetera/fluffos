#include "base/std.h"

#include "compiler/internal/disassembler.h"

#include "vm/vm.h"
#include "compiler/internal/lex.h"
#include "compiler/internal/icode.h"

#include <fmt/format.h>

static void disassemble(FILE *f /*f*/, char *code /*code*/, int /*start*/ start, int /*end*/ end,
                        program_t *prog /*prog*/);
static const char *disassem_string(const char * /*str*/);
static int short_compare(const void * /*a*/, const void * /*b*/);
static void dump_line_numbers(FILE * /*f*/, program_t * /*prog*/);

void dump_prog_details(program_t *prog, FILE *f, int flags) {
  int i, j;

  fprintf(f, "\n;;; %s\n\n", prog->filename);

  fprintf(f, "VARIABLES:\n");
  for (i = 0; i < prog->num_variables_defined; i++) {
    char buf[255];
    auto end = &buf[sizeof(buf) - 1];
    get_type_name(&buf[0], end, prog->variable_types[i]);
    fprintf(f, "%4d: %s%s\n", i, buf, prog->variable_table[i]);
  }
  fprintf(f, "STRINGS:\n");
  for (i = 0; i < prog->num_strings; i++) {
    fprintf(f, "%4d: ", i);
    for (j = 0; j < 32; j++) {
      char c;

      if (!(c = prog->strings[i][j])) {
        break;
      } else if (c == '\n') {
        fprintf(f, "\\n");
      } else {
        fputc(c, f);
      }
    }
    fputc('\n', f);
  }

  if (flags & 1) {
    fprintf(f, "DISASSEMBLY:\n");
    disassemble(f, prog->program, 0, prog->program_size, prog);
  } else {
    fprintf(f, "PROGRAM:");
    for (i = 0; i < prog->program_size; i++) {
      if (i % 16 == 0) {
        fprintf(f, "\n\t%04x: ", static_cast<unsigned int>(i));
      }
      fprintf(f, "%02d ", static_cast<unsigned char>(prog->program[i]));
    }
    fputc('\n', f);
  }
  if (flags & 2) {
    fprintf(f, "\n;;;  *** Line Number Info ***\n");
    dump_line_numbers(f, prog);
  }
}
/* Current flags:
 * 1 - do disassembly
 * 2 - dump line number table
 */
void dump_prog(program_t *prog, FILE *f, int flags) {
  int i;
  int num_funcs_total;

  fprintf(f, "NAME: /%s\n", prog->filename);
  fprintf(f, "INHERITS:\n");
  fprintf(f, "      name                    fio    vio\n");
  fprintf(f, "      ----------------        ---    ---\n");
  for (i = 0; i < prog->num_inherited; i++) {
    fprintf(f, "\t%-20s  %5d  %5d\n", prog->inherit[i].prog->filename,
            prog->inherit[i].function_index_offset, prog->inherit[i].variable_index_offset);
  }
  fprintf(f, "FUNCTIONS:\n");
  fprintf(f, "      name                  offset  mods   flags   fio  # locals  # args # def args\n");
  fprintf(f, "      --------------------- ------  ----  -------  ---  --------  ------ ----------\n");
  num_funcs_total = prog->last_inherited + prog->num_functions_defined;

  for (i = 0; i < num_funcs_total; i++) {
    char sflags[8];
    int flags;
    int runtime_index;
    function_t *func_entry = find_func_entry(prog, i);
    int low, high, mid;

    flags = prog->function_flags[i];
    if (flags & FUNC_ALIAS) {
      runtime_index = flags & ~FUNC_ALIAS;
      sflags[4] = 'a';
    } else {
      runtime_index = i;
      sflags[4] = '-';
    }

    flags = prog->function_flags[runtime_index];

    char smods[4];
    smods[0] = (flags & DECL_HIDDEN) ? '-' : '-';
    smods[0] = (flags & DECL_PRIVATE) ? 'p' : '-';
    smods[0] = (flags & DECL_PROTECTED) ? 'P' : '-';
    smods[0] = (flags & DECL_PUBLIC) ? '+' : '-';
    smods[1] = (flags & DECL_NOMASK) ? 'm' : '-';
    smods[2] = (flags & DECL_NOSAVE) ? 's' : '-';
    smods[3] = '\0';

    sflags[0] = (flags & FUNC_INHERITED) ? 'i' : '-';
    sflags[1] = (flags & FUNC_UNDEFINED) ? 'u' : '-';
    sflags[2] = (flags & FUNC_STRICT_TYPES) ? 's' : '-';
    sflags[3] = (flags & FUNC_PROTOTYPE) ? 'p' : '-';
    sflags[5] = (flags & FUNC_TRUE_VARARGS) ? 'V' : '-';
    sflags[6] = (flags & FUNC_VARARGS) ? 'v' : '-';
    sflags[7] = '\0';

    if (flags & FUNC_INHERITED) {
      low = 0;
      high = prog->num_inherited - 1;
      while (high > low) {
        mid = (low + high + 1) / 2;
        if (prog->inherit[mid].function_index_offset > runtime_index) {
          high = mid - 1;
        } else {
          low = mid;
        }
      }

      fprintf(f, "%4d: %-20s  %6d  %4s  %7s  %3d\n", i, func_entry->funcname, low, smods, sflags,
              runtime_index - prog->inherit[low].function_index_offset);
    } else {
      fprintf(f, "%4d: %-20s  %6d  %4s  %7s        %7d   %5d %10d", i, func_entry->funcname,
              runtime_index - prog->last_inherited, smods, sflags, func_entry->num_local,
              func_entry->num_arg, func_entry->num_arg - func_entry->min_arg);

      std::string default_arg_findex_map;
      for(int j = 0; j < func_entry->num_arg; j++) {
        if (func_entry->default_args_findex[j] != 0) {
          default_arg_findex_map += fmt::format(FMT_STRING(" {}:{}"), j, func_entry->default_args_findex[j]);
        }
      }
      fprintf(f, " %s\n", default_arg_findex_map.c_str());
    }
  }

  for (i = 0; i < prog->num_inherited; i++) {
    dump_prog_details(prog->inherit[i].prog, f, flags);
  }
  dump_prog_details(prog, f, flags);
}

static const char *disassem_string(const char *str) {
  static char buf[30 * 2 + 1];
  char *b;
  int i;

  if (!str) {
    return "0";
  }

  b = buf;
  for (i = 0; i < 29; i++) {
    if (!str[i]) {
      break;
    }
    if (str[i] == '\n') {
      *b++ = '\\';
      *b++ = 'n';
    } else {
      *b++ = str[i];
    }
  }
  *b++ = 0;
  return buf;
}

#define NUM_FUNS (prog->num_functions_defined + prog->last_inherited)
#define NUM_FUNS_D prog->num_functions_defined
#define VARS prog->variable_names
#define NUM_VARS prog->num_variables_total
#define STRS prog->strings
#define NUM_STRS prog->num_strings
#define CLSS prog->classes

static int short_compare(const void *a, const void *b) {
  int x = *(unsigned short *)a;
  int y = *(unsigned short *)b;

  return x - y;
}

static const char *pushes[] = {"string", "number", "global", "local"};

static void print_function_sig(FILE *f, program_t *prog, int idx) {
  char buf[255];
  auto end = &buf[sizeof(buf) - 1];

  auto funp = prog->function_table[idx];
  get_type_name(&buf[0], end, funp.type);
  fprintf(f, "%s", buf);
  fprintf(f, "%s", funp.funcname);

  fprintf(f, "(");
  unsigned short *types;
  if (prog->type_start && prog->type_start[idx] != INDEX_START_NONE) {
    types = &prog->argument_types[prog->type_start[idx]];
  } else {
    types = nullptr;
  }
  if (funp.num_arg > 0) {
    if (types) {
      for (int i = 0; i < funp.num_arg; i++) {
        auto p = get_type_name(buf, end, types[i]);
        *(p - 1) = '\0';  // get rid of last space
        if (i != 0) fprintf(f, ",");
        fprintf(f, "%s", buf);
      }
    } else {
      fprintf(f, "args: %d", funp.num_arg);
    }
  }
  fprintf(f, ")");
}

static void disassemble(FILE *f, char *code, int start, int end, program_t *prog) {
  extern int num_simul_efun;
  short instr;
  int i, j, ri;
  LPC_INT iarg;
  unsigned short sarg;
  unsigned short offset;
  char *pc, buff[2048];
  int next_func;

  short *offsets;

  if (start == 0) {
    /* sort offsets of functions */
    offsets = reinterpret_cast<short *>(malloc(NUM_FUNS_D * 2 * sizeof(short)));
    for (i = 0; i < NUM_FUNS_D; i++) {
      ri = i + prog->last_inherited;

      if (prog->function_flags[ri] & FUNC_NO_CODE) {
        offsets[i << 1] = end + 1;
      } else {
        offsets[i << 1] = prog->function_table[i].address;
      }
      offsets[(i << 1) + 1] = i;
    }
    qsort(reinterpret_cast<char *>(&offsets[0]), NUM_FUNS_D, sizeof(short) * 2, short_compare);
    next_func = 0;
  } else {
    offsets = nullptr;
    next_func = -1;
  }

  pc = code + start;

  const char *last_file = nullptr;
  int last_line = 0;

  while ((pc - code) < end) {
    if ((next_func >= 0) && ((pc - code) >= offsets[next_func])) {
      if (next_func > 0) {
        if (last_file && last_line > 0) {
          fprintf(f, "; %s:%d\n", last_file, last_line);
        }
        last_file = nullptr;
        last_line = 0;
      }
      fprintf(f, "\n;; Function: ");
      auto func_idx = offsets[next_func + 1];
      print_function_sig(f, prog, func_idx);
      fprintf(f, "\n");

      next_func += 2;
      if (next_func >= (NUM_FUNS_D * 2)) {
        next_func = -1;
      }
    }

    auto saved_pc = pc;
    instr = *pc++;
    buff[0] = 0;
    sarg = 0;

    {
      const char *new_file = nullptr;
      int new_line = 0;
      get_explicit_line_number_info(pc, prog, &new_file, &new_line);
      if (last_file != new_file || last_line != new_line) {
        if (last_file && last_line > 0) {
          fprintf(f, "; %s:%d\n", last_file, last_line);
        }
        last_file = new_file;
        last_line = new_line;
      }
    }

    fflush(f);
    fprintf(f, "%04tx: ", (pc - 1) - code); // Address

    switch (instr) {
      case F_PUSH: {
        auto p = buff;
        p += sprintf(p, "push ");
        i = EXTRACT_UCHAR(pc++);
        while (i--) {
          j = EXTRACT_UCHAR(pc++);
          p += sprintf(p, "%s %d", pushes[(j & PUSH_WHAT) >> 6], j & PUSH_MASK);
          if (i) {
            p += sprintf(p, ", ");
          } else {
            break;
          }
        }
        break;
      }
        /* Single numeric arg */
      case F_BRANCH_NE:
      case F_BRANCH_GE:
      case F_BRANCH_LE:
      case F_BRANCH_EQ:
      case F_BRANCH:
      case F_BRANCH_WHEN_ZERO:
      case F_BRANCH_WHEN_NON_ZERO:
#ifdef F_LOR
      case F_LOR:
      case F_LAND:
#endif
        COPY_SHORT(&sarg, pc);
        offset = (pc - code) + sarg;
        sprintf(buff, "%04x (%04x)", static_cast<unsigned>(sarg), static_cast<unsigned>(offset));
        pc += 2;
        break;

      case F_NEXT_FOREACH:
      case F_BBRANCH_LT:
        COPY_SHORT(&sarg, pc);
        offset = (pc - code) - sarg;
        sprintf(buff, "%04x (%04x)", static_cast<unsigned>(sarg), static_cast<unsigned>(offset));
        pc += 2;
        break;
      case F_FOREACH: {
        int flags = EXTRACT_UCHAR(pc++);
        const char *left = "local", *right = "local";

        if (flags & FOREACH_LEFT_GLOBAL) {
          left = "global";
        }
        if (flags & FOREACH_RIGHT_GLOBAL) {
          right = "global";
        }
        if (flags & FOREACH_REF) {
          if (flags & FOREACH_MAPPING) {
            right = "ref";
          } else {
            left = "ref";
          }
        }

        if (flags & FOREACH_MAPPING) {
          char *tmp = pc++;
          sprintf(buff, "(mapping) %s %i, %s %i", left, EXTRACT_UCHAR(tmp), right,
                  EXTRACT_UCHAR(pc++));
        } else {
          sprintf(buff, "(array | string) %s %i", left, EXTRACT_UCHAR(pc++));
        }
        break;
      }

      case F_BBRANCH_WHEN_ZERO:
      case F_BBRANCH_WHEN_NON_ZERO:
      case F_BBRANCH:
        COPY_SHORT(&sarg, pc);
        offset = (pc - code) - sarg;
        sprintf(buff, "%04x (%04x)", static_cast<unsigned>(sarg), static_cast<unsigned>(offset));
        pc += 2;
        break;

#ifdef F_JUMP
      case F_JUMP:
#endif
#ifdef F_JUMP_WHEN_ZERO
      case F_JUMP_WHEN_ZERO:
      case F_JUMP_WHEN_NON_ZERO:
#endif
      case F_CATCH:
        COPY_SHORT(&sarg, pc);
        sprintf(buff, "%04x", static_cast<unsigned>(sarg));
        pc += 2;
        break;

      case F_AGGREGATE:
      case F_AGGREGATE_ASSOC:
        COPY_SHORT(&sarg, pc);
        sprintf(buff, "%d", sarg);
        pc += 2;
        break;

      case F_MAKE_REF:
      case F_KILL_REFS:
      case F_MEMBER:
      case F_MEMBER_LVALUE:
        sprintf(buff, "%d", EXTRACT_UCHAR(pc++));
        break;

      case F_EXPAND_VARARGS: {
        int which = EXTRACT_UCHAR(pc++);
        if (which) {
          sprintf(buff, "%d from top of stack", which);
        } else {
          strcpy(buff, "top of stack");
        }
      } break;

      case F_NEW_EMPTY_CLASS:
      case F_NEW_CLASS: {
        int which = EXTRACT_UCHAR(pc++);
        strcpy(buff, STRS[CLSS[which].classname]);
        break;
      }

      case F_CALL_FUNCTION_BY_ADDRESS: {
        COPY_SHORT(&sarg, pc);
        pc += sizeof(short);
        const uint8_t args = EXTRACT_UCHAR(pc++);
        if (sarg < NUM_FUNS) {
          sprintf(buff, "%s, pushed_args:%d", function_name(prog, sarg), args);
        } else {
          sprintf(buff, "<out of range %d>", sarg);
        }
      }
      break;

      case F_CALL_INHERITED: {
        program_t *newprog;

        newprog = (prog->inherit + EXTRACT_UCHAR(pc++))->prog;
        COPY_SHORT(&sarg, pc);
        pc += 3;
        if (sarg < (newprog->num_functions_defined + newprog->last_inherited)) {
          sprintf(buff, "%30s::%-12s %5d", newprog->filename, function_name(newprog, sarg), sarg);
        } else {
          sprintf(buff, "<out of range in %30s - %d>", newprog->filename, sarg);
        }
        break;
      }
      case F_GLOBAL_LVALUE:
      case F_GLOBAL: {
        short iarg;
        LOAD2(iarg, pc);
        if (iarg < NUM_VARS) {
          sprintf(buff, "%s(%d)", variable_name(prog, iarg), iarg);
        } else {
          sprintf(buff, "<out of range %d >", iarg);
        }
        break;
      }
      case F_LOOP_INCR:
        sprintf(buff, "LV%d", EXTRACT_UCHAR(pc));
        pc++;
        break;
      case F_WHILE_DEC:
        COPY_SHORT(&sarg, pc + 1);
        offset = (pc - code) - sarg;
        sprintf(buff, "LV%d--, branch %04x (%04x)", EXTRACT_UCHAR(pc), static_cast<unsigned>(sarg),
                static_cast<unsigned>(offset));
        pc += 3;
        break;
      case F_TRANSFER_LOCAL:
      case F_LOCAL:
      case F_LOCAL_LVALUE:
      case F_VOID_ASSIGN_LOCAL:
      case F_REF:
      case F_REF_LVALUE:
        sprintf(buff, "LV%d", EXTRACT_UCHAR(pc));
        pc++;
        break;
      case F_LOOP_COND_NUMBER:
        i = EXTRACT_UCHAR(pc++);
        COPY_INT(&iarg, pc);
        pc += 4;
        COPY_SHORT(&sarg, pc);
        offset = (pc - code) - sarg;
        pc += 2;
        sprintf(buff, "LV%d < %" LPC_INT_FMTSTR_P " bbranch_when_non_zero %04x (%04x)", i, iarg,
                sarg, offset);
        break;
      case F_LOOP_COND_LOCAL:
        i = EXTRACT_UCHAR(pc++);
        iarg = *pc++;
        COPY_SHORT(&sarg, pc);
        offset = (pc - code) - sarg;
        pc += 2;
        sprintf(buff, "LV%d < LV%" LPC_INT_FMTSTR_P " bbranch_when_non_zero %04x (%04x)", i, iarg,
                sarg, offset);
        break;
      case F_STRING:
        COPY_SHORT(&sarg, pc);
        if (sarg < NUM_STRS) {
          sprintf(buff, "\"%s\"", disassem_string(STRS[sarg]));
        } else {
          sprintf(buff, "<out of range %d>", sarg);
        }
        pc += 2;
        break;
      case F_SHORT_STRING:
        if (EXTRACT_UCHAR(pc) < NUM_STRS) {
          sprintf(buff, "\"%s\"", disassem_string(STRS[EXTRACT_UCHAR(pc)]));
        } else {
          sprintf(buff, "<out of range %d>", EXTRACT_UCHAR(pc));
        }
        pc++;
        break;
      case F_SIMUL_EFUN:
        COPY_SHORT(&sarg, pc);
        if (sarg >= num_simul_efun) {
          sprintf(buff, "<invalid %d> %d\n", sarg, pc[2]);
        } else {
          sprintf(buff, "\"%s\" args: %d", simuls[sarg].func->funcname, pc[2]);
        }
        pc += 3;
        break;

      case F_FUNCTION_CONSTRUCTOR:
        switch (EXTRACT_UCHAR(pc++)) {
          case FP_SIMUL:
            LOAD_SHORT(sarg, pc);
            sprintf(buff, "<simul_efun> \"%s\"", simuls[sarg].func->funcname);
            break;
          case FP_EFUN:
            LOAD_SHORT(sarg, pc);
            sprintf(buff, "<efun> %s", instrs[sarg].name);
            break;
          case FP_LOCAL:
            LOAD_SHORT(sarg, pc);
            if (sarg < NUM_FUNS) {
              sprintf(buff, "<local_fun> %s", function_name(prog, sarg));
            } else {
              sprintf(buff, "<local_fun> <out of range %d>", sarg);
            }
            break;
          case FP_FUNCTIONAL:
          case FP_FUNCTIONAL | FP_NOT_BINDABLE: {
            uint8_t num_args = EXTRACT_UCHAR(pc++);
            uint16_t size;
            LOAD_SHORT(size, pc);
            sprintf(buff, "<functional, %d args>: Code size: %d,", num_args, size);
            break;
          }
          case FP_ANONYMOUS:
          case FP_ANONYMOUS | FP_NOT_BINDABLE:
            COPY_SHORT(&sarg, &pc[2]);
            sprintf(buff,
                    "<anonymous function, %d args, %d locals, ends at "
                    "%04tu>\nCode:",
                    pc[0], pc[1], (pc + 3 + sarg - code));
            pc += 4;
            break;
        }
        break;
      case F_SHORT_INT: {
        short iarg_tmp;

        COPY_SHORT(&iarg_tmp, pc);
        sprintf(buff, "short %d", iarg_tmp);
        pc += sizeof(iarg_tmp);
        break;
      };
      case F_NUMBER: {
        LPC_INT iarg_tmp;

        COPY_INT(&iarg_tmp, pc);
        sprintf(buff, "%" LPC_INT_FMTSTR_P, iarg_tmp);
        pc += sizeof(LPC_INT);
        break;
      }
      case F_REAL: {
        LPC_FLOAT farg;

        COPY_FLOAT(&farg, pc);
        sprintf(buff, "%" LPC_FLOAT_FMTSTR_P, farg);
        pc += sizeof(LPC_FLOAT);
        break;
      }
      case F_SSCANF:
      case F_PARSE_COMMAND:
      case F_BYTE:
        sprintf(buff, "%d", EXTRACT_UCHAR(pc));
        pc++;
        break;

      case F_NBYTE:
        sprintf(buff, "-%d", EXTRACT_UCHAR(pc));
        pc++;
        break;

      case F_SWITCH: {
        unsigned char ttype;
        unsigned short stable, etable, def;
        unsigned int addr;
        char *aptr;
        char *parg;

        ttype = EXTRACT_UCHAR(pc);
        COPY_SHORT(&stable, pc + 1);
        COPY_SHORT(&etable, pc + 3);
        COPY_SHORT(&def, pc + 5);
        addr = pc - code;
        aptr = pc;

        fprintf(f, "switch\n");
        fprintf(f, "      type: %02x table: %04x-%04x deflt: %04x\n", static_cast<unsigned>(ttype),
                addr + stable, addr + etable, addr + def);
        /* recursively disassemble stuff in switch */
        disassemble(f, code, pc - code + 7, addr + stable, prog);

        /* now print out table - ugly... */
        fprintf(f, "      switch table (for %04x)\n", static_cast<unsigned>(pc - code - 1));
        if (ttype == 0xfe) {
          ttype = 0; /* direct lookup */
        } else if (ttype >> 4 == 0xf) {
          ttype = 1; /* normal int */
        } else {
          ttype = 2; /* string */
        }

        pc += stable;
        if (ttype == 0) {
          i = 0;
          while (pc < aptr + etable - 4) {
            COPY_SHORT(&sarg, pc);
            fprintf(f, "\t%2d: %04x\n", i++, addr + sarg);
            pc += 2;
          }
          COPY_INT(&iarg, pc);
          fprintf(f, "\tminval = %" LPC_INT_FMTSTR_P "\n", iarg);
          pc += 4;
        } else {
          while (pc < aptr + etable) {
            COPY_PTR(&parg, pc);
            COPY_SHORT(&sarg, pc + sizeof(char *));
            if (ttype == 1 || !parg) {
              if (sarg == 1) {
                fprintf(f, "\t%-4p\t<range start>\n", parg);
              } else {
                fprintf(f, "\t%-4p\t%04x\n", parg, addr + sarg);
              }
            } else {
              fprintf(f, "\t\"%s\"\t%04x\n", disassem_string(parg), addr + sarg);
            }
            pc += 2 + sizeof(char *);
          }
        }
        continue;
      }
      case F_EFUNV: {
        short efun;
        LOAD_SHORT(efun, pc);
        auto args = EXTRACT_UCHAR(pc++);
        sprintf(buff, "EFUN_V (ARGS: %d) %s(%d)", args, query_instr_name(efun), efun);
        sprintf(buff, "EFUN: %s(%d)", query_instr_name(efun), efun);
        break;
      }
      case F_EFUN0:
      case F_EFUN1:
      case F_EFUN2:
      case F_EFUN3: {
        short efun;
        LOAD_SHORT(efun, pc);
        sprintf(buff, "EFUN: %s(%d)", query_instr_name(efun), efun);
        break;
      }
      case 0:
        fprintf(f, "*** zero opcode ***\n");
        continue;
      default:
        // fprintf(f, "*** %s (%d) ***\n", query_instr_name(instr), instr);
        // continue;
        break;
    }
    {
      char tmp[256 + 1] = {};
      auto p = &tmp[0];
      while (saved_pc != pc) {
        p += sprintf(p, "%02hhX ", *saved_pc++);
      }
      fprintf(f, " %-25s", tmp); // byte code in HEX
    }
    fprintf(f, " %-35s; %s\n", query_instr_name(instr), buff);
  }

  // print last line
  fprintf(f, "; %s:%d\n", last_file, last_line);

  if (offsets) {
    free(offsets);
  }
}

#define INCLUDE_DEPTH 10

static void dump_line_numbers(FILE *f, program_t *prog) {
  unsigned short *fi;
  unsigned char *li_start;
  unsigned char *li_end;
  unsigned char *li;
  int addr;
  int sz;
  ADDRESS_TYPE s;

  if (!prog->line_info) {
    fprintf(f, "Failed to load line numbers\n");
    return;
  }

  fi = prog->file_info;
  li_end = reinterpret_cast<unsigned char *>((reinterpret_cast<char *>(fi)) + fi[0]);
  li_start = reinterpret_cast<unsigned char *>(fi + fi[1]);

  fi += 2;
  fprintf(f, "\nabsolute line -> (file, line) table:\n");
  while (fi < reinterpret_cast<unsigned short *>(li_start)) {
    fprintf(f, "%i lines from %i [%s]\n", fi[0], fi[1], prog->strings[fi[1] - 1]);
    fi += 2;
  }

  li = li_start;
  addr = 0;
  fprintf(f, "\naddress -> absolute line table:\n");
  while (li < li_end) {
    sz = *li++;
#if !defined(USE_32BIT_ADDRESSES)
    COPY_SHORT(&s, li);
#else
    COPY4(&s, li);
#endif
    li += sizeof(ADDRESS_TYPE);
    fprintf(f, "%04x-%04x: %i\n", addr, addr + sz - 1, s);
    addr += sz;
  }
}
