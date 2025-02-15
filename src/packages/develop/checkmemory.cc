/*
 * md.cc
 *
 *  Created on: Nov 16, 2014
 *      Author: sunyc
 */

#include "base/package_api.h"

#include "packages/core/file.h"
#include "packages/core/call_out.h"
#include "packages/core/outbuf.h"
#include "packages/core/heartbeat.h"
#ifdef PACKAGE_PARSER
#include "packages/parser/parser.h"
#endif
#ifdef PACKAGE_PCRE
#include "packages/pcre/pcre.h"
#endif
#ifdef PACKAGE_UIDS
#include "packages/uids/uids.h"
#endif
#ifdef PACKAGE_SOCKETS
#include "packages/sockets/socket_efuns.h"
#endif
#ifdef PACKAGE_DB
#include "packages/db/db.h"
#endif
#ifdef PACKAGE_ASYNC
#include "packages/async/async.h"
#endif

#if (defined(DEBUGMALLOC) && defined(DEBUGMALLOC_EXTENSIONS))

void mark_svalue(struct svalue_t *);
void check_string_stats(struct outbuffer_t *);

outbuffer_t out;

static const char *sources[] = {"*",
                                "temporary blocks",
                                "permanent blocks",
                                "compiler blocks",
                                "data blocks",
                                "miscellaneous blocks",
                                "<#6>",
                                "<#7>",
                                "<#8>",
                                "<#9>",
                                "<#10>",
                                "program blocks",
                                "call_out blocks",
                                "interactives",
                                "ed blocks",
                                "<#15>",
                                "include list",
                                "permanent identifiers",
                                "identifier hash table",
                                "reserved block",
                                "mudlib stats",
                                "objects",
                                "object table",
                                "config table",
                                "simul_efuns",
                                "sentences",
                                "string table",
                                "free swap blocks",
                                "uids",
                                "object names",
                                "predefines",
                                "line numbers",
                                "compiler local blocks",
                                "compiled program",
                                "users",
                                "debugmalloc overhead",
                                "heart_beat list",
                                "parser",
                                "input_to",
                                "sockets",
                                "strings",
                                "malloc strings",
                                "shared strings",
                                "function pointers",
                                "arrays",
                                "mappings",
                                "mapping nodes",
                                "mapping tables",
                                "buffers",
                                "classes"};

void mark_svalue(svalue_t *sv);

char *dump_debugmalloc(const char *tfn, int mask) {
  int j, total = 0, chunks = 0;
  const char *fn;
  md_node_t *entry;
  FILE *fp;

  outbuf_zero(&out);
  fn = check_valid_path(tfn, current_object, "debugmalloc", 1);
  if (!fn) {
    error("Invalid path '%s' for writing.\n", tfn);
  }
  fp = fopen(fn, "w");
  if (!fp) {
    error("Unable to open %s for writing.\n", fn);
  }
  fprintf(fp, "%12s %12s %12s %5s %7s %s\n", "id", "gametick", "ptr", "tag", "sz", "desc");
  for (j = 0; j < MD_TABLE_SIZE; j++) {
    for (entry = table[j]; entry; entry = entry->next) {
      if (!mask || (entry->tag == mask)) {
        fprintf(fp, "%12d %12" PRId64 " %12p %1d:%03d %7d %s\n", entry->id, entry->gametick,
                PTR(entry), (entry->tag >> 8) & 0xff, entry->tag & 0xff, entry->size, entry->desc);
        total += entry->size;
        chunks++;
      }
    }
  }
  fprintf(fp, "total =    %8d\n", total);
  fprintf(fp, "# chunks = %8d\n", chunks);
  fprintf(fp, "ave. bytes per chunk = %7.2f\n\n", (double)total / chunks);
  fprintf(fp, "categories:\n\n");
  for (j = 0; j < MAX_CATEGORY; j++) {
    fprintf(fp, "%4d: %10" PRIu64 " %10" PRIu64 "\n", j, blocks[j], totals[j]);
  }
  fprintf(fp, "tags:\n\n");
  for (j = MAX_CATEGORY + 1; j < MAX_TAGS; j++) {
    fprintf(fp, "%4d: %10" PRIu64 " %10" PRIu64 "\n", j, blocks[j], totals[j]);
  }
  fclose(fp);
  outbuf_addv(&out, "total =    %8d\n", total);
  outbuf_addv(&out, "# chunks = %8d\n", chunks);
  if (chunks) {
    outbuf_addv(&out, "ave. bytes per chunk = %7.2f\n", (double)total / chunks);
  }
  outbuf_fix(&out);
  return out.buffer;
}
#endif

#ifdef DEBUGMALLOC_EXTENSIONS
static void mark_object(object_t *ob) {
#ifndef NO_ADD_ACTION
  sentence_t *sent;
#endif
  int i;

  if (ob->prog) {
    ob->prog->extra_ref++;
  }

  if (ob->obname) {
    DO_MARK(ob->obname, TAG_OBJ_NAME);
  }

  if (ob->replaced_program) {
    EXTRA_REF(BLOCK(ob->replaced_program))++;
  }

#ifdef PRIVS
  if (ob->privs) {
    EXTRA_REF(BLOCK(ob->privs))++;
  }
#endif

#ifndef NO_ADD_ACTION
  if (ob->living_name) {
    EXTRA_REF(BLOCK(ob->living_name))++;
  }

  sent = ob->sent;

  while (sent) {
    DO_MARK(sent, TAG_SENTENCE);
    if (sent->flags & V_FUNCTION) {
      sent->function.f->hdr.extra_ref++;
    } else {
      if (sent->function.s) {
        EXTRA_REF(BLOCK(sent->function.s))++;
      }
    }
    if (sent->verb) {
      EXTRA_REF(BLOCK(sent->verb))++;
    }
    sent = sent->next;
  }
#endif

#ifdef PACKAGE_PARSER
  if (ob->pinfo) {
    parser_mark(ob->pinfo);
  }
#endif

  if (ob->prog)
    for (i = 0; i < ob->prog->num_variables_total; i++) {
      mark_svalue(&ob->variables[i]);
    }
  else
    outbuf_addv(&out, "can't mark variables; %s is swapped.\n", ob->obname);
}

void mark_svalue(svalue_t *sv) {
  switch (sv->type) {
    case T_OBJECT:
      sv->u.ob->extra_ref++;
      break;
    case T_ARRAY:
      sv->u.arr->extra_ref++;
      break;
    case T_CLASS:
      sv->u.arr->extra_ref++;
      break;
    case T_MAPPING:
      sv->u.map->extra_ref++;
      break;
    case T_FUNCTION:
      sv->u.fp->hdr.extra_ref++;
      break;
    case T_BUFFER:
      sv->u.buf->extra_ref++;
      break;
    case T_STRING:
      switch (sv->subtype) {
        case STRING_MALLOC:
          MSTR_EXTRA_REF(sv->u.string)++;
          break;
        case STRING_SHARED:
          EXTRA_REF(BLOCK(sv->u.string))++;
          break;
      }
  }
}

static void mark_funp(funptr_t *fp) {
  if (fp->hdr.args) {
    fp->hdr.args->extra_ref++;
  }

  if (fp->hdr.owner) {
    fp->hdr.owner->extra_ref++;
  }
  switch (fp->hdr.type) {
    case FP_LOCAL | FP_NOT_BINDABLE:
      if (fp->hdr.owner) {
        fp->hdr.owner->prog->extra_func_ref++;
      }
      break;
    case FP_FUNCTIONAL:
    case FP_FUNCTIONAL | FP_NOT_BINDABLE:
      fp->f.functional.prog->extra_func_ref++;
      break;
  }
}

static void mark_sentence(sentence_t *sent) {
  if (sent->flags & V_FUNCTION) {
    if (sent->function.f) {
      sent->function.f->hdr.extra_ref++;
    }
  } else {
    if (sent->function.s) {
      EXTRA_REF(BLOCK(sent->function.s))++;
    }
  }
#ifndef NO_ADD_ACTION
  if (sent->verb) {
    EXTRA_REF(BLOCK(sent->verb))++;
  }
#endif
}

static int print_depth = 0;

static void md_print_array(array_t *vec) {
  int i;

  outbuf_add(&out, "({ ");
  for (i = 0; i < vec->size; i++) {
    switch (vec->item[i].type) {
      case T_INVALID:
        outbuf_add(&out, "INVALID");
        break;
      case T_NUMBER:
        outbuf_addv(&out, "%" LPC_INT_FMTSTR_P, vec->item[i].u.number);
        break;
      case T_REAL:
        outbuf_addv(&out, "%" LPC_FLOAT_FMTSTR_P, vec->item[i].u.real);
        break;
      case T_STRING:
        outbuf_addv(&out, "\"%s\"", vec->item[i].u.string);
        break;
      case T_ARRAY:
        if (print_depth < 2) {
          print_depth++;
          md_print_array(vec->item[i].u.arr);
        } else {
          outbuf_add(&out, "({ ... })");
        }
        break;
      case T_CLASS:
        outbuf_add(&out, "<class>");
        break;
      case T_BUFFER:
        outbuf_add(&out, "<buffer>");
        break;
      case T_FUNCTION:
        outbuf_add(&out, "<function>");
        break;
      case T_MAPPING:
        outbuf_add(&out, "<mapping>");
        break;
      case T_OBJECT:
        outbuf_addv(&out, "OBJ(%s)", vec->item[i].u.ob->obname);
        break;
    }
    if (i != vec->size - 1) {
      outbuf_add(&out, ", ");
    }
  }
  outbuf_add(&out, " })\n");
  print_depth--;
}

static void mark_config() {
  int i;

  for (i = 0; i < NUM_CONFIG_STRS; i++) {
    DO_MARK(config_str[i], TAG_STRING);
  }
}

static uint64_t base_overhead = 0;

/* Compute the correct values of allocd_strings, allocd_bytes, and
 * bytes_distinct_strings based on blocks that are actually allocated.
 */
void compute_string_totals(uint64_t *asp, uint64_t *abp, uint64_t *bp) {
  int hsh;
  md_node_t *entry;
  malloc_block_t *msbl;
  block_t *ssbl;

  *asp = 0;
  *abp = 0;
  *bp = 0;

  for (hsh = 0; hsh < MD_TABLE_SIZE; hsh++) {
    for (entry = table[hsh]; entry; entry = entry->next) {
      if (entry->tag == TAG_MALLOC_STRING) {
        msbl = NODET_TO_PTR(entry, malloc_block_t *);
        *bp += msbl->size + 1;
        *asp += msbl->ref;
        *abp += (uint64_t)(msbl->ref) * (msbl->size + 1);
      }
      if (entry->tag == TAG_SHARED_STRING) {
        ssbl = NODET_TO_PTR(entry, block_t *);
        *bp += ssbl->size + 1;
        *asp += ssbl->refs;
        *abp += (uint64_t)(ssbl->refs) * (ssbl->size + 1);
      }
    }
  }
}

/*
 * Verify string statistics.  out can be zero, in which case any errors
 * are printed to stdout and abort() is called.  Otherwise the error messages
 * are added to the outbuffer.
 */
void check_string_stats(outbuffer_t *out) {
  uint64_t overhead = blocks[TAG_SHARED_STRING & 0xff] * sizeof(block_t) +
                      blocks[TAG_MALLOC_STRING & 0xff] * sizeof(malloc_block_t);
  uint64_t const num = blocks[TAG_SHARED_STRING & 0xff] + blocks[TAG_MALLOC_STRING & 0xff];
  uint64_t bytes, as, ab;
  int need_dump = 0;

  compute_string_totals(&as, &ab, &bytes);

  if (!base_overhead) {
    base_overhead = overhead_bytes - overhead;
  }
  overhead += base_overhead;

  if (num != num_distinct_strings) {
    need_dump = 1;
    if (out) {
      outbuf_addv(out, "WARNING: num_distinct_strings is: %" PRIu64 " should be: %" PRIu64 "\n",
                  num_distinct_strings, num);
    } else {
      printf("WARNING: num_distinct_strings is: %" PRIu64 " should be: %" PRIu64 " \n",
             num_distinct_strings, num);
      dump_stralloc(nullptr);
      abort();
    }
  }
  if (overhead != overhead_bytes) {
    need_dump = 1;
    if (out) {
      outbuf_addv(out, "WARNING: overhead_bytes is: %" PRIu64 " should be: %" PRIu64 "\n",
                  overhead_bytes, overhead);
    } else {
      printf("WARNING: overhead_bytes is: %" PRIu64 " should be: %" PRIu64 "\n", overhead_bytes,
             overhead);
      dump_stralloc(nullptr);
      abort();
    }
  }
  if (bytes != bytes_distinct_strings) {
    need_dump = 1;
    if (out) {
      outbuf_addv(out, "WARNING: bytes_distinct_strings is: %" PRIu64 " should be: %" PRIu64 "\n",
                  bytes_distinct_strings, bytes);
    } else {
      printf("WARNING: bytes_distinct_strings is: %" PRIu64 " should be: %" PRIu64 "\n",
             bytes_distinct_strings, bytes);
      dump_stralloc(nullptr);
      abort();
    }
  }
  if (allocd_strings != as) {
    need_dump = 1;
    if (out) {
      outbuf_addv(out, "WARNING: allocd_strings is: %" PRIu64 " should be: %" PRIu64 "\n",
                  allocd_strings, as);
    } else {
      printf("WARNING: allocd_strings is: %" PRIu64 " should be: %" PRIu64 "\n", allocd_strings,
             as);
      dump_stralloc(nullptr);
      abort();
    }
  }
  if (allocd_bytes != ab) {
    need_dump = 1;
    if (out) {
      outbuf_addv(out, "WARNING: allocd_bytes is: %" PRIu64 " should be: %" PRIu64 "\n",
                  allocd_bytes, ab);
    } else {
      printf("WARNING: allocd_bytes is: %" PRIu64 " should be: %" PRIu64 "\n", allocd_bytes, ab);
      dump_stralloc(nullptr);
      abort();
    }
  }

  if (need_dump) {
    dump_stralloc(out);
  }
}

/* currently: 1 - debug, 2 - suppress leak checks */
void check_all_blocks(int flag) {
  int i, j, hsh;
  md_node_t *entry;
  object_t *ob;
  array_t *vec;
  mapping_t *map;
  buffer_t *buf;
  funptr_t *fp;
  mapping_node_t *node;
  program_t *prog;
  sentence_t *sent;
  char *ptr;
  block_t *ssbl;
  malloc_block_t *msbl;
  extern svalue_t apply_ret_value;

  outbuf_zero(&out);
  if (!(flag & 2)) {
    outbuf_add(&out, "Performing memory tests ...\n");
  }

  for (hsh = 0; hsh < MD_TABLE_SIZE; hsh++) {
    for (entry = table[hsh]; entry; entry = entry->next) {
      entry->tag &= ~TAG_MARKED;
      switch (entry->tag & 0xff00) {
        case TAG_TEMPORARY:
          if (!(flag & 2)) {
            outbuf_addv(&out, "WARNING: Found temporary block: %s %04x\n", entry->desc, entry->tag);
          }
          break;
        case TAG_COMPILER:
          if (!(flag & 2)) {
            outbuf_addv(&out, "Found compiler block: %s %04x\n", entry->desc, entry->tag);
          }
          break;
        case TAG_MISC:
          outbuf_addv(&out, "Found miscellaneous block: %s %04x\n", entry->desc, entry->tag);
          break;
      }
      switch (entry->tag) {
        case TAG_OBJECT:
          ob = NODET_TO_PTR(entry, object_t *);
          ob->extra_ref = 0;
          break;
        case TAG_PROGRAM:
          prog = NODET_TO_PTR(entry, program_t *);
          prog->extra_ref = 0;
          prog->extra_func_ref = 0;
          break;
        case TAG_MALLOC_STRING: {
          char *str;

          msbl = NODET_TO_PTR(entry, malloc_block_t *);
          /* don't give an error for the return value we are
             constructing :) */
          if (msbl == MSTR_BLOCK(out.buffer)) {
            break;
          }

          str = (char *)(msbl + 1);
          msbl->extra_ref = 0;
          if (msbl->size != USHRT_MAX && msbl->size != strlen(str)) {
            outbuf_addv(&out,
                        "Malloc'ed string length is incorrect: %s %04x '%s': "
                        "is: %i should be: %" PRIu64 "\n",
                        entry->desc, entry->tag, str, msbl->size, strlen(str));
          }
          break;
        }
        case TAG_SHARED_STRING:
          ssbl = NODET_TO_PTR(entry, block_t *);
          EXTRA_REF(ssbl) = 0;
          break;
        case TAG_ARRAY:
          vec = NODET_TO_PTR(entry, array_t *);
          vec->extra_ref = 0;
          break;
        case TAG_CLASS:
          vec = NODET_TO_PTR(entry, array_t *);
          vec->extra_ref = 0;
          break;
        case TAG_MAPPING:
          map = NODET_TO_PTR(entry, mapping_t *);
          map->extra_ref = 0;
          break;
        case TAG_FUNP:
          fp = NODET_TO_PTR(entry, funptr_t *);
          fp->hdr.extra_ref = 0;
          break;
        case TAG_BUFFER:
          buf = NODET_TO_PTR(entry, buffer_t *);
          buf->extra_ref = 0;
          break;
      }
    }
  }

  if (!(flag & 2)) {
    /* the easy ones to find */
    if (blocks[TAG_SIMULS & 0xff] > 3) {
      outbuf_add(&out, "WARNING: more than three simul_efun tables allocated.\n");
    }
    if (blocks[TAG_INC_LIST & 0xff] > 1) {
      outbuf_add(&out, "WARNING: more than one include list allocated.\n");
    }
    if (blocks[TAG_IDENT_TABLE & 0xff] > 1) {
      outbuf_add(&out, "WARNING: more than one identifier hash table allocated.\n");
    }
    if (blocks[TAG_RESERVED & 0xff] > 1) {
      outbuf_add(&out, "WARNING: more than one reserved block allocated.\n");
    }
    if (blocks[TAG_OBJ_TBL & 0xff] > 2) {
      outbuf_add(&out, "WARNING: more than two object table allocated.\n");
    }
    if (blocks[TAG_CONFIG & 0xff] > 1) {
      outbuf_add(&out, "WARNING: more than config file table allocated.\n");
    }
    if (blocks[TAG_STR_TBL & 0xff] > 1) {
      outbuf_add(&out, "WARNING: more than string table allocated.\n");
    }
    {
      int const a = totals[TAG_CALL_OUT & 0xff];
      int const b = total_callout_size();
      if (a != b) {
        outbuf_addv(&out, "WARNING: wrong number of call_out blocks allocated: %d vs %d.\n", a, b);
        print_call_out_usage(&out, 1);
      }
    }
    if (blocks[TAG_LOCALS & 0xff] > 3) {
      outbuf_add(&out, "WARNING: more than 3 local blocks allocated.\n");
    }

    if (blocks[TAG_SENTENCE & 0xff] != tot_alloc_sentence)
      outbuf_addv(&out, "WARNING: tot_alloc_sentence is: %" PRIu64 " should be: %" PRIu64 "\n",
                  tot_alloc_sentence, blocks[TAG_SENTENCE & 0xff]);
    if (blocks[TAG_OBJECT & 0xff] != tot_alloc_object)
      outbuf_addv(&out, "WARNING: tot_alloc_object is: %" PRIu64 " should be: %" PRIu64 "\n",
                  tot_alloc_object, blocks[TAG_OBJECT & 0xff]);
    if (blocks[TAG_PROGRAM & 0xff] != total_num_prog_blocks)
      outbuf_addv(&out, "WARNING: total_num_prog_blocks is: %" PRIu64 " should be: %" PRIu64 "\n",
                  total_num_prog_blocks, blocks[TAG_PROGRAM & 0xff]);
    if (blocks[TAG_ARRAY & 0xff] != num_arrays)
      outbuf_addv(&out, "WARNING: num_arrays is: %" PRIu64 " should be: %" PRIu64 "\n", num_arrays,
                  blocks[TAG_ARRAY & 0xff]);
    if (totals[TAG_ARRAY & 0xff] != total_array_size)
      outbuf_addv(&out, "WARNING: total_array_size is: %" PRIu64 " should be: %" PRIu64 "\n",
                  total_array_size, totals[TAG_ARRAY & 0xff]);
    if (blocks[TAG_CLASS & 0xff] != num_classes)
      outbuf_addv(&out, "WARNING: num_classes is: %" PRIu64 " should be: %" PRIu64 "\n",
                  num_classes, blocks[TAG_CLASS & 0xff]);
    if (totals[TAG_CLASS & 0xff] != total_class_size)
      outbuf_addv(&out, "WARNING: total_class_size is: %" PRIu64 " should be: %" PRIu64 "\n",
                  total_class_size, totals[TAG_CLASS & 0xff]);
    if (blocks[TAG_MAPPING & 0xff] != num_mappings)
      outbuf_addv(&out, "WARNING: num_mappings is: %" PRIu64 " should be: %" PRIu64 "\n",
                  num_mappings, blocks[TAG_MAPPING & 0xff]);
    if (blocks[TAG_MAP_TBL & 0xff] != num_mappings)
      outbuf_addv(&out, "WARNING: %" PRIu64 " tables for %" PRIu64 " mappings\n",
                  blocks[TAG_MAP_TBL & 0xff], num_mappings);
    if (blocks[TAG_INTERACTIVE & 0xff] != users_num(true))
      outbuf_addv(&out, "WATNING: num_user is: %d should be: %" PRIu64 "\n", users_num(true),
                  blocks[TAG_INTERACTIVE & 0xff]);

    // String checks
    check_string_stats(&out);

#ifdef PACKAGE_EXTERNAL
    for (i = 0; i < g_num_external_cmds; i++) {
      if (external_cmd[i]) {
        DO_MARK(external_cmd[i], TAG_STRING);
      }
    }
#endif

    // Check to verify no duplicated heartbeats
    check_heartbeats();

    // Verify every objects that has O_HEART_BEAT flag has a corespodning HB
    for (ob = obj_list; ob; ob = ob->next_all) {
      if ((ob->flags & O_HEART_BEAT) != 0) {
        DEBUG_CHECK(query_heart_beat(ob) == 0, "Driver BUG: object with heartbeat not in hb table");
      }
    }
    for (ob = obj_list_destruct; ob; ob = ob->next_all) {
      if ((ob->flags & O_HEART_BEAT) != 0) {
        DEBUG_CHECK(query_heart_beat(ob) == 0, "Driver BUG: object with heartbeat not in hb table");
      }
    }

    /* now do a mark and sweep check to see what should be alloc'd */
    for (const auto &user : users()) {
      DO_MARK(user, TAG_INTERACTIVE);
      user->ob->extra_ref++;
      // FIXME(sunyc): I can't explain this, appearently somewhere
      // is giving interactive object an addtional ref.
      user->ob->extra_ref++;

      if (user->input_to) {
        user->input_to->ob->extra_ref++;
        DO_MARK(user->input_to, TAG_SENTENCE);
        mark_sentence(user->input_to);
        if (user->num_carry) {
          for (j = 0; j < user->num_carry; j++) {
            mark_svalue(user->carryover + j);
          }
        }
      }
#ifndef NO_ADD_ACTION
      if (user->iflags & NOTIFY_FAIL_FUNC) {
        user->default_err_message.f->hdr.extra_ref++;
      } else if (user->default_err_message.s) {
        EXTRA_REF(BLOCK(user->default_err_message.s))++;
      }
#endif
    }

    auto *dfm = CONFIG_STR(__DEFAULT_FAIL_MESSAGE__);
    if (dfm != nullptr && strlen(dfm) > 0) {
      char buf[8192];
      strcpy(buf, dfm);
      strcat(buf, "\n");
      const char *target = findstring(buf);
      if (target) {
        EXTRA_REF(BLOCK(target))++;
      }
    }

#ifdef PACKAGE_UIDS
    mark_all_uid_nodes();
#endif
#ifdef PACKAGE_MUDLIB_STATS
    mark_mudlib_stats();
#endif
#ifdef PACKAGE_SOCKETS
    mark_sockets();
#endif
#ifdef PACKAGE_PARSER
    parser_mark_verbs();
#endif
    mark_file_sv();
    mark_all_defines();
    mark_free_sentences();
    mark_iptable();
    mark_stack();
    mark_command_giver_stack();
    mark_call_outs();
    mark_simuls();
    mark_mapping_node_blocks();
    mark_config();
#ifdef PACKAGE_PCRE
    mark_pcre_cache();
#endif
#ifdef PACKAGE_DB
    mark_db_conn();
#endif
#ifdef PACKAGE_ASYNC
    async_mark_request();
#endif
    mark_svalue(&apply_ret_value);

    if (master_ob) {
      master_ob->extra_ref++;
    }
    if (simul_efun_ob) {
      simul_efun_ob->extra_ref++;
    }
    for (ob = obj_list; ob; ob = ob->next_all) {
      ob->extra_ref++;
    }
    /* objects on obj_list_destruct still have a ref too */
    for (ob = obj_list_destruct; ob; ob = ob->next_all) {
      ob->extra_ref++;
    }

    for (hsh = 0; hsh < MD_TABLE_SIZE; hsh++) {
      for (entry = table[hsh]; entry; entry = entry->next) {
        switch (entry->tag & ~TAG_MARKED) {
          case TAG_IDENT_TABLE: {
            ident_hash_elem_t *hptr, *first;
            ident_hash_elem_t **table;
            int size;

            table = NODET_TO_PTR(entry, ident_hash_elem_t **);
            size = (entry->size / 3) / sizeof(ident_hash_elem_t *);
            for (i = 0; i < size; i++) {
              first = table[i];
              if (first) {
                hptr = first;
                do {
                  if (hptr->token & (IHE_SIMUL | IHE_EFUN)) {
                    DO_MARK(hptr, TAG_PERM_IDENT);
                  }
                  hptr = hptr->next;
                } while (hptr != first);
              }
            }
            break;
          }
          case TAG_FUNP:
            fp = NODET_TO_PTR(entry, funptr_t *);
            mark_funp(fp);
            break;
          case TAG_ARRAY:
            vec = NODET_TO_PTR(entry, array_t *);
            if (entry->size != sizeof(array_t) + sizeof(svalue_t[1]) * (vec->size - 1)) {
              outbuf_addv(&out, "array size doesn't match block size: %s %04x\n", entry->desc,
                          entry->tag);
            }
            for (i = 0; i < vec->size; i++) {
              mark_svalue(&vec->item[i]);
            }
            break;
          case TAG_CLASS:
            vec = NODET_TO_PTR(entry, array_t *);
            if (vec->size &&
                entry->size != sizeof(array_t) + sizeof(svalue_t[1]) * (vec->size - 1)) {
              outbuf_addv(&out, "class size doesn't match block size: %s %04x\n", entry->desc,
                          entry->tag);
            }
            for (i = 0; i < vec->size; i++) {
              mark_svalue(&vec->item[i]);
            }
            break;
          case TAG_MAPPING:
            map = NODET_TO_PTR(entry, mapping_t *);
            DO_MARK(map->table, TAG_MAP_TBL);

            i = map->table_size;
            do {
              for (node = map->table[i]; node; node = node->next) {
                mark_svalue(node->values);
                mark_svalue(node->values + 1);
              }
            } while (i--);
            break;
          case TAG_OBJECT:
            ob = NODET_TO_PTR(entry, object_t *);
            mark_object(ob);
            {
              object_t *tmp = obj_list;
              while (tmp && tmp != ob) {
                tmp = tmp->next_all;
              }
              if (!tmp) {
                tmp = obj_list_destruct;
                while (tmp && tmp != ob) {
                  tmp = tmp->next_all;
                }
              }
#ifdef DEBUG
              if (!tmp) {
                tmp = obj_list_dangling;
                while (tmp && tmp != ob) {
                  tmp = tmp->next_all;
                }
                if (tmp) outbuf_addv(&out, "WARNING: %s is dangling.\n", ob->obname);
              }
#endif
              if (!tmp) outbuf_addv(&out, "WARNING: %s not in object list.\n", ob->obname);
            }
            break;
          case TAG_LPC_OBJECT:
            ob = NODET_TO_PTR(entry, object_t *);
            if (ob->obname) {
              DO_MARK(ob->obname, TAG_OBJ_NAME);
            }
            break;
          case TAG_PROGRAM:
            prog = NODET_TO_PTR(entry, program_t *);

            if (prog->line_info) {
              DO_MARK(prog->file_info, TAG_LINENUMBERS);
            }

            for (i = 0; i < prog->num_inherited; i++) {
              prog->inherit[i].prog->extra_ref++;
            }

            for (i = 0; i < prog->num_functions_defined; i++)
              if (prog->function_table[i].funcname) {
                EXTRA_REF(BLOCK(prog->function_table[i].funcname))++;
              }

            for (i = 0; i < prog->num_strings; i++) {
              EXTRA_REF(BLOCK(prog->strings[i]))++;
            }

            for (i = 0; i < prog->num_variables_defined; i++) {
              EXTRA_REF(BLOCK(prog->variable_table[i]))++;
            }

            EXTRA_REF(BLOCK(prog->filename))++;
        }
      }
    }

    /* now check */
    for (hsh = 0; hsh < MD_TABLE_SIZE; hsh++) {
      for (entry = table[hsh]; entry; entry = entry->next) {
        switch (entry->tag) {
          case TAG_MUDLIB_STATS:
            outbuf_addv(&out, "WARNING: Found orphan mudlib stat block: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_PROGRAM:
            prog = NODET_TO_PTR(entry, program_t *);
            if (prog->ref != prog->extra_ref) {
              outbuf_addv(&out, "Bad ref count for program %s, is %d - should be %d\n",
                          prog->filename, prog->ref, prog->extra_ref);
            }
            if (prog->func_ref != prog->extra_func_ref) {
              outbuf_addv(&out,
                          "Bad function ref count for program %s, is %d - "
                          "should be %d\n",
                          prog->filename, prog->func_ref, prog->extra_func_ref);
            }
            break;
          case TAG_OBJECT:
            ob = NODET_TO_PTR(entry, object_t *);
            if (ob->ref != ob->extra_ref) {
              outbuf_addv(&out, "Bad ref count for object %s, is %d - should be %d\n", ob->obname,
                          ob->ref, ob->extra_ref);
            }
            break;
          case TAG_ARRAY:
            vec = NODET_TO_PTR(entry, array_t *);
            if (vec->ref != vec->extra_ref) {
              outbuf_addv(&out, "Bad ref count for array, is %d - should be %d\n", vec->ref,
                          vec->extra_ref);
              print_depth = 0;
              md_print_array(vec);
            }
            break;
          case TAG_CLASS:
            vec = NODET_TO_PTR(entry, array_t *);
            if (vec->ref != vec->extra_ref) {
              outbuf_addv(&out, "Bad ref count for class, is %d - should be %d\n", vec->ref,
                          vec->extra_ref);
            }
            break;
          case TAG_MAPPING:
            map = NODET_TO_PTR(entry, mapping_t *);
            if (map->ref != map->extra_ref) {
              outbuf_addv(&out, "Bad ref count for mapping, is %d - should be %d\n", map->ref,
                          map->extra_ref);
            }
            break;
          case TAG_FUNP:
            fp = NODET_TO_PTR(entry, funptr_t *);
            if (fp->hdr.owner && (strcmp(fp->hdr.owner->obname, "single/tests/efuns/async") == 0 ||
                                  strcmp(fp->hdr.owner->obname, "single/tests/efuns/db") == 0)) {
              // Async package mark doesn't work yet.
              break;
            }
            if (fp->hdr.ref != fp->hdr.extra_ref) {
              outbuf_addv(&out,
                          "Bad ref count for function pointer (owned by %s), "
                          "is %d - should be %d\n",
                          (fp->hdr.owner ? fp->hdr.owner->obname : "(null)"), fp->hdr.ref,
                          fp->hdr.extra_ref);
            }
            break;
          case TAG_BUFFER:
            buf = NODET_TO_PTR(entry, buffer_t *);
            if (buf->ref != buf->extra_ref) {
              outbuf_addv(&out, "Bad ref count for buffer, is %d - should be %d\n", buf->ref,
                          buf->extra_ref);
            }
            break;
          case TAG_PREDEFINES:
            outbuf_addv(&out, "WARNING: Found orphan predefine: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_LINENUMBERS:
            outbuf_addv(&out, "WARNING: Found orphan line number block: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_OBJ_NAME:
            outbuf_addv(&out, "WARNING: Found orphan object name: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_INTERACTIVE:
            outbuf_addv(&out, "WARNING: Found orphan interactive: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_UID:
            // not sure this is still relevant with the change in data structure
            /*
          outbuf_addv(&out, "WARNING: Found orphan uid node: %s %04x\n", entry->desc, entry->tag);
          */
            break;
          case TAG_SENTENCE:
            sent = NODET_TO_PTR(entry, sentence_t *);
            outbuf_addv(&out, "WARNING: Found orphan sentence: %s:%s - %s %04x\n", sent->ob->obname,
                        sent->function.s, entry->desc, entry->tag);
            break;
          case TAG_PERM_IDENT:
            outbuf_addv(&out, "WARNING: Found orphan permanent identifier: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_STRING:
            ptr = NODET_TO_PTR(entry, char *);
            outbuf_addv(&out, "WARNING: Found orphan malloc'ed string: \"%s\" - %s %04x\n", ptr,
                        entry->desc, entry->tag);
            break;
          case TAG_MALLOC_STRING:
            msbl = NODET_TO_PTR(entry, malloc_block_t *);
            /* don't give an error for the return value we are
               constructing :) */
            if (msbl == MSTR_BLOCK(out.buffer)) {
              break;
            }

            // ignore the current executing command
            if (starts_with(entry->desc, "current_command:")) {
              break;
            }

            if (msbl->ref != msbl->extra_ref) {
              outbuf_addv(&out,
                          "Bad ref count for malloc string \"%s\" %s %04x, is "
                          "%d - should be %d\n",
                          (char *)(msbl + 1), entry->desc, entry->tag, msbl->ref, msbl->extra_ref);
            }
            break;
          case TAG_SHARED_STRING:
            ssbl = NODET_TO_PTR(entry, block_t *);
            if (REFS(ssbl) != EXTRA_REF(ssbl)) {
              outbuf_addv(&out,
                          "Bad ref count for shared string \"%s\", is %d - "
                          "should be %d\n",
                          STRING(ssbl), REFS(ssbl), EXTRA_REF(ssbl));
              std::stringstream ss;
              stralloc_print_entry(ss, ssbl);
              auto result = ss.str();

              outbuf_add(&out, result.c_str());
              md_print_ref_journal(entry, &out);
            }
            break;
          case TAG_ED:
            outbuf_addv(&out, "Found allocated ed block: %s %04x\n", entry->desc, entry->tag);
            break;
          case TAG_MAP_TBL:
            outbuf_addv(&out, "WARNING: Found orphan mapping table: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_MAP_NODE_BLOCK:
            outbuf_addv(&out, "WARNING: Found orphan mapping node block: %s %04x\n", entry->desc,
                        entry->tag);
            break;
          case TAG_PERMANENT: /* only save_object|resotre_object uses this */
            break;
            /* FIXME: need to account these. */
          case TAG_INC_LIST:
          case TAG_IDENT_TABLE:
          case TAG_OBJ_TBL:
          case TAG_SIMULS:
          case TAG_STR_TBL:
          case TAG_LOCALS:
          case TAG_CALL_OUT:
          case TAG_INPUT_TO:
            break;
          default:
            if (entry->tag < TAG_MARKED) {
              printf("WARNING: unaccounted node block: %s %d\n", entry->desc, entry->tag);
            }
            break;
        }
        entry->tag &= ~TAG_MARKED;
      }
    }
  }

  if (flag & 1) {
    outbuf_add(&out, "\n\n");
    outbuf_add(&out, "      source                    blks   total\n");
    outbuf_add(&out, "------------------------------ ------ --------\n");
    for (i = 1; i < MAX_TAGS; i++) {
      if (totals[i]) {
        outbuf_addv(&out, "%-30s %6" PRIu64 " %8" PRIu64 "\n", sources[i], blocks[i], totals[i]);
      }
      if (i == 5) {
        outbuf_add(&out, "\n");
      }
    }
  }
  if (!(flag & 2)) {
    outbuf_push(&out);
  } else {
    FREE_MSTR(out.buffer);
    push_number(0);
  }
}

#endif /* DEBUGMALLOC_EXTENSIONS */
