/*
 *  $Id$
 *
 *  This file is part of the OpenLink Software Virtuoso Open-Source (VOS)
 *  project.
 *
 *  Copyright (C) 1998-2006 OpenLink Software
 *
 *  This project is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; only version 2 of the License, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#include "libutil.h"
#include "sqlnode.h"
#include "sqlbif.h"
#include "sqlparext.h"
#include "bif_text.h"
#include "xmlparser.h"
#include "xmltree.h"
#include "numeric.h"
#include "security.h"
#include "sqlcmps.h"
#include "sparql.h"
#include "sparql2sql.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "sparql_p.h"
#ifdef __cplusplus
}
#endif

#include "rdf_mapping_jso.h"

#ifdef MALLOC_DEBUG
const char *spartlist_impl_file="???";
int spartlist_impl_line;
#endif

SPART*
spartlist_impl (sparp_t *sparp, ptrlong length, ptrlong type, ...)
{
  SPART *tree;
  va_list ap;
  int inx;
  va_start (ap, type);
#ifdef DEBUG
  if (IS_POINTER(type))
    GPF_T;
#endif
  length += 1;
#ifdef MALLOC_DEBUG
  tree = (SPART *) dbg_mp_alloc_box (spartlist_impl_file, spartlist_impl_line, THR_TMP_POOL, sizeof (caddr_t) * length, DV_ARRAY_OF_POINTER);
#else
  tree = (SPART *) t_alloc_box (sizeof (caddr_t) * length, DV_ARRAY_OF_POINTER);
#endif
  for (inx = 2; inx < length; inx++)
    {
      caddr_t child = va_arg (ap, caddr_t);
#if 0
#ifdef MALLOC_DEBUG
      if (IS_BOX_POINTER (child))
	t_alloc_box_assert (child);
#endif
#endif
      ((caddr_t *)(tree))[inx] = child;
    }
  va_end (ap);
  tree->type = type;
  tree->srcline = t_box_num ((NULL != sparp) ? ((NULL != sparp->sparp_curr_lexem) ? sparp->sparp_curr_lexem->sparl_lineno : 0) : 0);
  /*spart_check (sparp, tree);*/
  return tree;
}


SPART*
spartlist_with_tail_impl (sparp_t *sparp, ptrlong length, caddr_t tail, ptrlong type, ...)
{
  SPART *tree;
  va_list ap;
  int inx;
  ptrlong tail_len = BOX_ELEMENTS(tail);
  va_start (ap, type);
#ifdef DEBUG
  if (IS_POINTER(type))
    GPF_T;
#endif
#ifdef MALLOC_DEBUG
  tree = (SPART *) dbg_dk_alloc_box (spartlist_impl_file, spartlist_impl_line, sizeof (caddr_t) * (1+length+tail_len), DV_ARRAY_OF_POINTER);
#else
  tree = (SPART *) dk_alloc_box (sizeof (caddr_t) * (1+length+tail_len), DV_ARRAY_OF_POINTER);
#endif
  for (inx = 2; inx < length; inx++)
    {
      caddr_t child = va_arg (ap, caddr_t);
#ifdef MALLOC_DEBUG
      if (IS_BOX_POINTER (child))
	dk_alloc_box_assert (child);
#endif
      ((caddr_t *)(tree))[inx] = child;
    }
  va_end (ap);
  tree->type = type;
  tree->srcline = t_box_num ((NULL != sparp) ? ((NULL != sparp->sparp_curr_lexem) ? sparp->sparp_curr_lexem->sparl_lineno : 0) : 0);
  ((ptrlong *)(tree))[length] = tail_len;
  memcpy (((caddr_t *)(tree))+length+1, tail, sizeof(caddr_t) * tail_len);
  dk_free_box (tail);
  /*spart_check (sparp, tree);*/
  return tree;
}

caddr_t
spar_source_place (sparp_t *sparp, char *raw_text)
{
  char buf [3000];
  int lineno = ((NULL != sparp->sparp_curr_lexem) ? (int) sparp->sparp_curr_lexem->sparl_lineno : 0);
  char *next_text = NULL;
  if ((NULL == raw_text) && (NULL != sparp->sparp_curr_lexem))
    raw_text = sparp->sparp_curr_lexem->sparl_raw_text;
  if ((NULL != raw_text) && (NULL != sparp->sparp_curr_lexem))
    {
      if ((sparp->sparp_curr_lexem_bmk.sparlb_offset + 1) < sparp->sparp_lexem_buf_len)
        next_text = sparp->sparp_curr_lexem[1].sparl_raw_text;
    }
  sprintf (buf, "%.400s, line %d%.6s%.1000s%.5s%.15s%.1000s%.5s",
      sparp->sparp_err_hdr,
      lineno,
      ((NULL == raw_text) ? "" : ", at '"),
      ((NULL == raw_text) ? "" : raw_text),
      ((NULL == raw_text) ? "" : "'"),
      ((NULL == next_text) ? "" : ", before '"),
      ((NULL == next_text) ? "" : next_text),
      ((NULL == next_text) ? "" : "'")
      );
  return t_box_dv_short_string (buf);
}

caddr_t
spar_dbg_string_of_triple_field (sparp_t *sparp, SPART *fld)
{
  switch (SPART_TYPE (fld))
    {
    case SPAR_BLANK_NODE_LABEL: return t_box_sprintf (210, "_:%.200s", fld->_.var.vname);
    case SPAR_VARIABLE: return t_box_sprintf (210, "?%.200s", fld->_.var.vname);
    case SPAR_QNAME: return t_box_sprintf (210, "<%.200s>", fld->_.lit.val);
    case SPAR_LIT:
      if (NULL != fld->_.lit.language)
        return t_box_sprintf (410, "\"%.200s\"@%.200s", fld->_.lit.val, fld->_.lit.language);
      if (NULL == fld->_.lit.datatype)
        return t_box_sprintf (210, "\"%.200s\"", fld->_.lit.val);
      if (
        (uname_xmlschema_ns_uri_hash_integer == fld->_.lit.datatype) ||
        (uname_xmlschema_ns_uri_hash_decimal == fld->_.lit.datatype) ||
        (uname_xmlschema_ns_uri_hash_double == fld->_.lit.datatype) )
        return t_box_sprintf (210, "%.200s", fld->_.lit.val);
      return t_box_sprintf (410, "\"%.200s\"^^<%.200s>", fld->_.lit.val, fld->_.lit.datatype);
    default: return t_box_dv_short_string ("...");
    }
}

void
sparyyerror_impl (sparp_t *sparp, char *raw_text, const char *strg)
{
  int lineno = ((NULL != sparp->sparp_curr_lexem) ? (int) sparp->sparp_curr_lexem->sparl_lineno : 0);
  char *next_text = NULL;
  if ((NULL == raw_text) && (NULL != sparp->sparp_curr_lexem))
    raw_text = sparp->sparp_curr_lexem->sparl_raw_text;
  if ((NULL != raw_text) && (NULL != sparp->sparp_curr_lexem))
    {
      if ((sparp->sparp_curr_lexem_bmk.sparlb_offset + 1) < sparp->sparp_lexem_buf_len)
        next_text = sparp->sparp_curr_lexem[1].sparl_raw_text;
    }
  sqlr_new_error ("37000", "SP030",
      "%.400s, line %d: %.500s%.5s%.1000s%.5s%.15s%.1000s%.5s",
      sparp->sparp_err_hdr,
      lineno,
      strg,
      ((NULL == raw_text) ? "" : " at '"),
      ((NULL == raw_text) ? "" : raw_text),
      ((NULL == raw_text) ? "" : "'"),
      ((NULL == next_text) ? "" : " before '"),
      ((NULL == next_text) ? "" : next_text),
      ((NULL == next_text) ? "" : "'")
      );
}

void
sparyyerror_impl_1 (sparp_t *sparp, char *raw_text, int yystate, short *yyssa, short *yyssp, const char *strg)
{
  int sm2, sm1, sp1;
  int lineno = (int) ((NULL != sparp->sparp_curr_lexem) ? sparp->sparp_curr_lexem->sparl_lineno : 0);
  char *next_text = NULL;
  if ((NULL == raw_text) && (NULL != sparp->sparp_curr_lexem))
    raw_text = sparp->sparp_curr_lexem->sparl_raw_text;
  if ((NULL != raw_text) && (NULL != sparp->sparp_curr_lexem))
    {
      if ((sparp->sparp_curr_lexem_bmk.sparlb_offset + 1) < sparp->sparp_lexem_buf_len)
        next_text = sparp->sparp_curr_lexem[1].sparl_raw_text;
    }

  sp1 = yyssp[1];
  sm1 = yyssp[-1];
  sm2 = ((sm1 > 0) ? yyssp[-2] : 0);

  sqlr_new_error ("37000", "SP030",
     /*errlen,*/ "%.400s, line %d: %.500s [%d-%d-(%d)-%d]%.5s%.1000s%.5s%.15s%.1000s%.5s",
      sparp->sparp_err_hdr,
      lineno,
      strg,
      sm2,
      sm1,
      yystate,
      ((sp1 & ~0x7FF) ? -1 : sp1) /* stub to avoid printing random garbage in logs */,
      ((NULL == raw_text) ? "" : " at '"),
      ((NULL == raw_text) ? "" : raw_text),
      ((NULL == raw_text) ? "" : "'"),
      ((NULL == next_text) ? "" : " before '"),
      ((NULL == next_text) ? "" : next_text),
      ((NULL == next_text) ? "" : "'")
      );
}

void spar_error (sparp_t *sparp, const char *format, ...)
{
  va_list ap;
  va_start (ap, format);
  if (NULL == sparp)
    sqlr_new_error ("37000", "SP031",
      "SPARQL generic error: %.1500s",
      box_vsprintf (1500, format, ap) );
  else
    sqlr_new_error ("37000", "SP031",
      "%.400s: %.1500s",
      sparp->sparp_err_hdr,
      t_box_vsprintf (1500, format, ap) );
  va_end (ap);
}

void
spar_internal_error (sparp_t *sparp, const char *msg)
{
#if 0
  FILE *core_reason1;
  fprintf (stderr, "Internal error %s while processing\n-----8<-----\n%s\n-----8<-----\n", msg, sparp->sparp_text);
  core_reason1 = fopen ("core_reason1","wt");
  fprintf (core_reason1, "Internal error %s while processing\n-----8<-----\n%s\n-----8<-----\n", msg, sparp->sparp_text);
  fclose (core_reason1);
  GPF_T1(msg);
#else
  sqlr_new_error ("37000", "SP031",
    "%.400s: Internal error: %.1500s",
    ((NULL != sparp && sparp->sparp_err_hdr) ? sparp->sparp_err_hdr : "SPARQL"), msg);
#endif
}

#ifdef MALLOC_DEBUG
spartlist_track_t *
spartlist_track (const char *file, int line)
{
  static spartlist_track_t ret = { spartlist_impl, spartlist_with_tail_impl };
  spartlist_impl_file = file;
  spartlist_impl_line = line;
  return &ret;
}
#endif

#if 0
void sparqr_free (spar_query_t *sparqr)
{
  dk_free_tree (sparqr->sparqr_tree);
  ;;;
}
#endif

/*
caddr_t spar_charref_to_strliteral (sparp_t *sparp, const char *strg)
{
  const char *src_end = strchr(strg, '\0');
  const char *err_msg = NULL;
  int charref_val = spar_charref_to_unichar (&strg, src_end, &err_msg);
  if (0 > charref_val)
    xpyyerror_impl (sparp, NULL, err_msg);
  else
    {
      char tmp[MAX_UTF8_CHAR];
      char *tgt_tail = eh_encode_char__UTF8 (charref_val, tmp, tmp + MAX_UTF8_CHAR);
      return box_dv_short_nchars (tmp, tgt_tail - tmp);
    }
  return box_dv_short_nchars ("\0", 1);
}
*/

caddr_t
sparp_expand_qname_prefix (sparp_t *sparp, caddr_t qname)
{
  char *lname = strchr (qname, ':');
  dk_set_t ns_dict;
  caddr_t ns_pref, ns_uri, res;
  int ns_uri_len, local_len, res_len;
  if (NULL == lname)
    return qname;
  lname++;
  ns_dict = sparp->sparp_env->spare_namespace_prefixes;
  ns_pref = t_box_dv_short_nchars (qname, lname - qname);
  ns_uri = dk_set_get_keyword (ns_dict, ns_pref, NULL);
  if (NULL == ns_uri)
    {
      if (!strcmp (ns_pref, "rdf:"))
        ns_uri = uname_rdf_ns_uri;
      else if (!strcmp (ns_pref, "xsd:"))
        ns_uri = uname_xmlschema_ns_uri_hash;
      else if (!strcmp (ns_pref, "virtrdf:"))
        ns_uri = uname_virtrdf_ns_uri;
      else if (!strcmp ("sql:", ns_pref) || !strcmp ("bif:", ns_pref))
        ns_uri = ns_pref;
      else
        sparyyerror_impl (sparp, ns_pref, "Undefined namespace prefix");
    }
  ns_uri_len = box_length (ns_uri) - 1;
  local_len = strlen (lname);
  res_len = ns_uri_len + local_len;
  res = box_dv_ubuf (res_len);
  memcpy (res, ns_uri, ns_uri_len);
  memcpy (res + ns_uri_len, lname, local_len);
  res[res_len] = '\0';
  return box_dv_uname_from_ubuf (res);
}

caddr_t
sparp_expand_q_iri_ref (sparp_t *sparp, caddr_t ref)
{
  if (NULL != sparp->sparp_env->spare_base_uri)
    {
      query_instance_t *qi = sparp->sparp_sparqre->sparqre_qi;
      caddr_t err = NULL, path_utf8_tmp, res;
      if (NULL == qi)
        qi = CALLER_LOCAL;
      path_utf8_tmp = xml_uri_resolve (qi,
        &err, sparp->sparp_env->spare_base_uri, ref, "UTF-8" );
      res = t_box_dv_uname_string (path_utf8_tmp);
      dk_free_tree (path_utf8_tmp);
      return res;
    }
  else
    return ref;
}

caddr_t spar_strliteral (sparp_t *sparp, const char *strg, int strg_is_long, char delimiter)
{
  caddr_t tmp_buf;
  caddr_t res;
  const char *err_msg;
  const char *src_tail, *src_end;
  char *tgt_tail;
  src_tail = strg + (strg_is_long ? 3 : 1);
  src_end = strg + strlen (strg) - (strg_is_long ? 3 : 1);
  tgt_tail = tmp_buf = dk_alloc_box ((src_end - src_tail) + 1, DV_SHORT_STRING);
  while (src_tail < src_end)
    {
      switch (src_tail[0])
	{
	case '\\':
          {
	    const char *bs_src		= "abfnrtv\\\'\"uU";
	    const char *bs_trans	= "\a\b\f\n\r\t\v\\\'\"\0\0";
            const char *bs_lengths	= "\2\2\2\2\2\2\2\2\2\2\6\012";
	    const char *hit = strchr (bs_src, src_tail[1]);
	    char bs_len, bs_tran;
	    const char *nextchr;
	    if (NULL == hit)
	      {
		err_msg = "Unsupported escape sequence after '\'";
		goto err;
	      }
            bs_len = bs_lengths [hit - bs_src];
            bs_tran = bs_trans [hit - bs_src];
	    nextchr = src_tail + bs_len;
	    if ((src_tail + bs_len) > src_end)
	      {
	        err_msg = "There is no place for escape sequence between '\' and the end of string";
	        goto err;
	      }
            if ('\0' != bs_tran)
              (tgt_tail++)[0] = bs_tran;
	    else
	      {
		unichar acc = 0;
		for (src_tail += 2; src_tail < nextchr; src_tail++)
		  {
		    int dgt = src_tail[0];
		    if ((dgt >= '0') && (dgt <= '9'))
		      dgt = dgt - '0';
		    else if ((dgt >= 'A') && (dgt <= 'F'))
		      dgt = 10 + dgt - 'A';
		    else if ((dgt >= 'a') && (dgt <= 'f'))
		      dgt = 10 + dgt - 'a';
		    else
		      {
		        err_msg = "Invalid hexadecimal digit in escape sequence";
			goto err;
		      }
		    acc = acc * 16 + dgt;
		  }
		if (acc < 0)
		  {
		    err_msg = "The \\U escape sequence represents invalid Unicode char";
		    goto err;
		  }
		tgt_tail = eh_encode_char__UTF8 (acc, tgt_tail, tgt_tail + MAX_UTF8_CHAR);
	      }
	    src_tail = nextchr;
            continue;
	  }
	default: (tgt_tail++)[0] = (src_tail++)[0];
	}
    }
  res = t_box_dv_short_nchars (tmp_buf, tgt_tail - tmp_buf);
  dk_free_box (tmp_buf);
  return res;

err:
  dk_free_box (tmp_buf);
  sparyyerror_impl (sparp, NULL, err_msg);
  return NULL;
}

void
sparp_free (sparp_t * sparp)
{
}

caddr_t
spar_mkid (sparp_t * sparp, const char *prefix)
{
  return t_box_sprintf (0x100, "%s-%ld-%ld",
    prefix,
    (long)((NULL != sparp->sparp_curr_lexem) ?
      sparp->sparp_curr_lexem->sparl_lineno : 0),
    (long)(sparp->sparp_unictr++) );
}

void spar_change_sign (caddr_t *lit_ptr)
{
  switch (DV_TYPE_OF (lit_ptr[0]))
    {
    case DV_NUMERIC:
      {
        numeric_t tmp = t_numeric_allocate();
        numeric_negate (tmp, (numeric_t)(lit_ptr[0]));
        lit_ptr[0] = (caddr_t)tmp;
        break;
      }
    case DV_LONG_INT:
      lit_ptr[0] = t_box_num (- unbox (lit_ptr[0]));
      break;
    case DV_DOUBLE_FLOAT:
      lit_ptr[0] = t_box_double (- ((double *)(lit_ptr[0]))[0]);
      break;
    default: GPF_T1("spar_change_sign(): bad box type");
    }
}

static const char *sparp_known_get_params[] = {
    "get:login", "get:method", "get:proxy", "get:query", "get:refresh", "get:soft", "get:uri", NULL };

static const char *sparp_integer_defines[] = {
    "input:grab-depth", "input:grab-limit", "sql:log-enable", "sql:signal-void-variables", NULL };

static const char *sparp_var_defines[] = { NULL };

void
sparp_define (sparp_t *sparp, caddr_t param, ptrlong value_lexem_type, caddr_t value)
{
  switch (value_lexem_type)
    {
    case Q_IRI_REF:
      value = sparp_expand_q_iri_ref (sparp, value);
      break;
    case QNAME:
    value = sparp_expand_qname_prefix (sparp, value);
      break;
    case SPARQL_INTEGER:
      {
        const char **chk;
        for (chk = sparp_integer_defines; (NULL != chk[0]) && strcmp (chk[0], param); chk++) ;
        if (NULL == chk[0])
          spar_error (sparp, "Integer value %ld is specified for define %s", (long)unbox(value), param);
        break;
      }
    case SPAR_VARIABLE:
      {
        const char **chk;
        for (chk = sparp_var_defines; (NULL != chk[0]) && strcmp (chk[0], param); chk++) ;
        if (NULL == chk[0])
          spar_error (sparp, "The value specified for define %s should be constant, not an variable", param);
        break;
      }
    }
  if ((7 < strlen (param)) && !memcmp (param, "output:", 7))
    {
      if (!strcmp (param, "output:valmode")) {
          sparp->sparp_env->spare_output_valmode_name = t_box_dv_uname_string (value); return; }
      if (!strcmp (param, "output:format")) {
          sparp->sparp_env->spare_output_format_name = t_box_dv_uname_string (value); return; }
    }
  if ((6 < strlen (param)) && !memcmp (param, "input:", 6))
    {
  if (!strcmp (param, "input:default-graph-uri") || !strcmp (param, "input:named-graph-uri"))
    {
  if (!strcmp (param, "input:default-graph-uri"))
    {
          SPART *new_precode = sparp_make_graph_precode ( sparp,
        spartlist (sparp, 2, SPAR_QNAME, t_box_dv_uname_string (value)),
        NULL );
              t_set_push ((dk_set_t *) &(sparp->sparp_env->spare_default_graph_precodes), new_precode);
          sparp->sparp_env->spare_default_graphs_locked = 1;
      return;
    }
  if (!strcmp (param, "input:named-graph-uri"))
    {
              t_set_push ((dk_set_t *) &(sparp->sparp_env->spare_named_graph_precodes),
        sparp_make_graph_precode (sparp,
          spartlist (sparp, 2, SPAR_QNAME, t_box_dv_uname_string (value)),
          NULL ) );
      sparp->sparp_env->spare_named_graphs_locked = 1;
      return;
    }
    }
      if (!strcmp (param, "input:same-as"))
        {
          sparp->sparp_env->spare_use_same_as = t_box_copy_tree (value);
          return;
        }
  if (!strcmp (param, "input:storage"))
    {
      if (NULL != sparp->sparp_env->spare_storage_name)
        spar_error (sparp, "'define %.30s' is used more than once", param);
      sparp->sparp_env->spare_storage_name = t_box_dv_uname_string (value);
      return;
    }
      if (!strcmp (param, "input:inference"))
        {
          if (NULL != sparp->sparp_env->spare_inference_name)
            spar_error (sparp, "'define %.30s' is used more than once", param);
          sparp->sparp_env->spare_inference_name = t_box_dv_uname_string (value);
          return;
        }
      if ((11 < strlen (param)) && !memcmp (param, "input:grab-", 11))
    {
      rdf_grab_config_t *rgc = &(sparp->sparp_env->spare_grab);
      const char *lock_pragma = NULL;
          rgc->rgc_pview_mode = 1;
      if (sparp->sparp_env->spare_default_graphs_locked)
        lock_pragma = "input:default-graph-uri";
      else if (sparp->sparp_env->spare_named_graphs_locked)
        lock_pragma = "input:named-graph-uri";
      if (NULL != lock_pragma)
        spar_error (sparp, "define %s should not appear after define %s", param, lock_pragma);
      if (!strcmp (param, "input:grab-all"))
        {
          rgc->rgc_all = 1;
          return;
        }
          if (!strcmp (param, "input:grab-intermediate"))
            {
              rgc->rgc_intermediate = 1;
              return;
            }
          if (!strcmp (param, "input:grab-seealso") || !strcmp (param, "input:grab-follow-predicate"))
            {
              switch (value_lexem_type)
                {
                case QNAME:
                case Q_IRI_REF:
                  t_set_push (&(rgc->rgc_sa_preds), value);
                  break;
                default:
                  if (('?' == value[0]) || ('$' == value[0]))
                    spar_error (sparp, "The value of define input:grab-seealso should be predicate name");
                  else
                    t_set_push_new_string (&(rgc->rgc_sa_preds), value);
                }
              return;
            }
      if (!strcmp (param, "input:grab-iri"))
        {
          switch (value_lexem_type)
            {
            case QNAME:
            case Q_IRI_REF:
              t_set_push (&(rgc->rgc_consts), value);
              break;
            default:
              if (('?' == value[0]) || ('$' == value[0]))
                    t_set_push_new_string (&(rgc->rgc_vars), t_box_dv_uname_string (value+1));
              else
                    t_set_push_new_string (&(rgc->rgc_consts), value);
            }
          return;
        }
  if (!strcmp (param, "input:grab-var"))
    {
      caddr_t varname;
      if (('?' == value[0]) || ('$' == value[0]))
        varname = t_box_dv_uname_string (value+1);
      else
        varname = t_box_dv_uname_string (value);
              t_set_push_new_string (&(rgc->rgc_vars), varname);
      return;
    }
  if (!strcmp (param, "input:grab-depth"))
    {
              ptrlong val = ((DV_LONG_INT == DV_TYPE_OF (value)) ? unbox_ptrlong (value) : -1);
      if (0 >= val)
        spar_error (sparp, "define input:grab-depth should have positive integer value");
          rgc->rgc_depth = t_box_num_nonull (val);
      return;
    }
  if (!strcmp (param, "input:grab-limit"))
    {
              ptrlong val = ((DV_LONG_INT == DV_TYPE_OF (value)) ? unbox_ptrlong (value) : -1);
      if (0 >= val)
        spar_error (sparp, "define input:grab-limit should have positive integer value");
          rgc->rgc_limit = t_box_num_nonull (val);
          return;
        }
          if (!strcmp (param, "input:grab-base")) {
              rgc->rgc_base = t_box_dv_uname_string (value); return; }
          if (!strcmp (param, "input:grab-destination")) {
              rgc->rgc_destination = t_box_dv_uname_string (value); return; }
          if (!strcmp (param, "input:grab-group-destination")) {
              rgc->rgc_group_destination = t_box_dv_uname_string (value); return; }
          if (!strcmp (param, "input:grab-resolver")) {
              rgc->rgc_resolver_name = t_box_dv_uname_string (value); return; }
          if (!strcmp (param, "input:grab-loader")) {
              rgc->rgc_loader_name = t_box_dv_uname_string (value); return; }
        }
    }
  if ((4 < strlen (param)) && !memcmp (param, "get:", 4))
    {
      const char **chk;
      for (chk = sparp_known_get_params; (NULL != chk[0]) && strcmp (chk[0], param); chk++) ;
      if (NULL != chk[0])
        {
          dk_set_t *opts_ptr = &(sparp->sparp_env->spare_common_sponge_options);
          if (0 < dk_set_position_of_string (opts_ptr[0], param))
            spar_error (sparp, "'define %.30s' is used more than once", param);
          t_set_push (opts_ptr, t_box_dv_short_string (value));
          t_set_push (opts_ptr, t_box_dv_uname_string (param));
          return;
        }
    }
  if ((4 < strlen (param)) && !memcmp (param, "sql:", 4))
    {
      if (!strcmp (param, "sql:log-enable"))
        {
          ptrlong val = ((DV_LONG_INT == DV_TYPE_OF (value)) ? unbox_ptrlong (value) : -1);
          if (0 > val)
            spar_error (sparp, "define sql:log-enable should have nonnegative integer value");
            sparp->sparp_env->spare_sparul_log_mode = t_box_num_and_zero (val);
          return; }
      if (!strcmp (param, "sql:table-option")) {
          t_set_push (&(sparp->sparp_env->spare_common_sql_table_options), t_box_dv_uname_string (value));
          return; }
      if (!strcmp (param, "sql:select-option")) {
          t_set_push (&(sparp->sparp_env->spare_sql_select_options), t_box_dv_uname_string (value));
          return; }
      if (!strcmp (param, "sql:signal-void-variables")) {
          ptrlong val = ((DV_LONG_INT == DV_TYPE_OF (value)) ? unbox_ptrlong (value) : -1);
          if ((0 > val) || (1 < val))
            spar_error (sparp, "define sql:signal-void-variables should have value 0 or 1");
            sparp->sparp_env->spare_signal_void_variables = val;
          return; }
    }
  spar_error (sparp, "Unsupported parameter '%.30s' in 'define'", param);
}

caddr_t
spar_selid_push (sparp_t *sparp)
{
  caddr_t selid = spar_mkid (sparp, "s");
  t_set_push (&(sparp->sparp_env->spare_selids), selid );
  spar_dbg_printf (("spar_selid_push () pushes %s\n", selid));
  return selid;
}

caddr_t
spar_selid_push_reused (sparp_t *sparp, caddr_t selid)
{
  t_set_push (&(sparp->sparp_env->spare_selids), selid );
  spar_dbg_printf (("spar_selid_push_reused () pushes %s\n", selid));
  return selid;
}


caddr_t spar_selid_pop (sparp_t *sparp)
{
  caddr_t selid = t_set_pop (&(sparp->sparp_env->spare_selids));
  spar_dbg_printf (("spar_selid_pop () pops %s\n", selid));
  return selid;
}

void spar_gp_init (sparp_t *sparp, ptrlong subtype)
{
  sparp_env_t *env = sparp->sparp_env;
  spar_dbg_printf (("spar_gp_init (..., %ld)\n", (long)subtype));
  spar_selid_push (sparp);
  t_set_push (&(env->spare_acc_req_triples), NULL);
  t_set_push (&(env->spare_acc_opt_triples), NULL);
  t_set_push (&(env->spare_acc_filters), NULL);
  t_set_push (&(env->spare_context_gp_subtypes), (caddr_t)subtype);
  t_set_push (&(env->spare_good_graph_varname_sets), env->spare_good_graph_varnames);
  if (WHERE_L != subtype)
    t_set_push (&(sparp->sparp_env->spare_propvar_sets), NULL); /* For WHERE_L it's done at beginning of the result-set. */
}

void spar_gp_replace_selid (sparp_t *sparp, dk_set_t membs, caddr_t old_selid, caddr_t new_selid)
{
  DO_SET (SPART *, memb, &membs)
    {
      int fld_ctr;
      if (SPAR_TRIPLE != SPART_TYPE (memb))
        continue;
      if (strcmp (old_selid, memb->_.triple.selid))
        spar_internal_error (sparp, "spar_gp_replace_selid(): bad selid of triple");
      memb->_.triple.selid = new_selid;
      for (fld_ctr = SPART_TRIPLE_FIELDS_COUNT; fld_ctr--; /*no step*/)
        {
          SPART *fld = memb->_.triple.tr_fields[fld_ctr];
          if ((SPAR_VARIABLE != SPART_TYPE (fld)) &&
            (SPAR_BLANK_NODE_LABEL != SPART_TYPE (fld)) )
            continue;
          if (strcmp (old_selid, fld->_.var.selid))
            spar_internal_error (sparp, "spar_gp_replace_selid(): bad selid of var or bnode label");
          fld->_.var.selid = new_selid;
        }
    }
  END_DO_SET ()
}

SPART *
spar_gp_finalize (sparp_t *sparp)
{
  sparp_env_t *env = sparp->sparp_env;
  caddr_t orig_selid = env->spare_selids->data;
  dk_set_t propvars = (dk_set_t) t_set_pop (&(env->spare_propvar_sets));
  dk_set_t req_membs;
  dk_set_t opt_membs;
  dk_set_t filts;
  ptrlong subtype;
  SPART *res;
  spar_dbg_printf (("spar_gp_finalize (..., %ld)\n", (long)subtype));
/* Create triple patterns for distinct '+>' propvars and OPTIONAL triple patterns for distinct '*>' propvars */
  DO_SET (spar_propvariable_t *, pv, &propvars)
    {
      if (_STAR_GT == pv->sparpv_op)
        spar_gp_init (sparp, OPTIONAL_L);
      spar_gp_add_triple_or_special_filter (sparp, NULL,
          spar_make_variable (sparp, pv->sparpv_subj_var->_.var.vname),
          pv->sparpv_verb_qname,
          spar_make_variable (sparp, pv->sparpv_obj_var_name),
          NULL, NULL );
      if (_STAR_GT == pv->sparpv_op)
        {
          SPART *pv_gp;
          pv_gp = spar_gp_finalize (sparp);
          t_set_push (((dk_set_t *)(&(env->spare_acc_req_triples->data))) /* not &req_membs */, pv_gp);
        }
    }
  END_DO_SET();
/* Pop the rest of the environment and adjust graph varnames */
  req_membs = (dk_set_t) t_set_pop (&(env->spare_acc_req_triples));
  opt_membs = (dk_set_t) t_set_pop (&(env->spare_acc_opt_triples));
  filts = (dk_set_t) t_set_pop (&(env->spare_acc_filters));
  subtype = (ptrlong)((void *)t_set_pop (&(env->spare_context_gp_subtypes)));
  env->spare_good_graph_bmk = t_set_pop (&(env->spare_good_graph_varname_sets));
/* The following 'if' does not mention UNIONs because UNIONs are handled right in .y file
   For OPTIONAL GP we roll back spare_good_graph_vars at bookmarked level
   For other sorts the content of stack is naturally inherited by the parent:
   the bookmark is t_set_pop-ed, but the content remains at its place */
  if (OPTIONAL_L == subtype) /* Variables nested in optionals can not be good graph variables... */
    env->spare_good_graph_varnames = env->spare_good_graph_bmk;
/* Add extra GP to guarantee proper left side of the left outer join */
  if ((1 < dk_set_length (req_membs)) && (0 < dk_set_length (opt_membs)))
    {
      SPART *left_group;
      spar_gp_init (sparp, 0);
      spar_gp_replace_selid (sparp, req_membs, orig_selid, env->spare_selids->data);
      env->spare_acc_req_triples->data = req_membs;
      left_group = spar_gp_finalize (sparp);
      req_membs = NULL;
      t_set_push (&req_membs, left_group);
    }
/* Add extra GP to guarantee proper support of {... OPTIONAL { ... ?x ... } ... OPTIONAL { ... ?x ... } } */
  if (1 < dk_set_length (opt_membs))
    {
      SPART *last_opt;
      SPART *left_group;
      last_opt = (SPART *)t_set_pop (&opt_membs);
      spar_gp_init (sparp, 0);
      spar_gp_replace_selid (sparp, req_membs, orig_selid, env->spare_selids->data);
      env->spare_acc_req_triples->data = req_membs;
      spar_gp_replace_selid (sparp, opt_membs, orig_selid, env->spare_selids->data);
      env->spare_acc_opt_triples->data = opt_membs;
      left_group = spar_gp_finalize (sparp);
      req_membs = NULL;
      t_set_push (&req_membs, left_group);
      opt_membs = NULL;
      t_set_push (&opt_membs, last_opt);
    }
/* Plain composing of SPAR_GP tree node */
  res = spartlist (sparp, 8,
    SPAR_GP, subtype,
    /* opt members are at the first place in NCONC because there's a reverse in t_revlist_to_array */
    t_revlist_to_array (t_NCONC (opt_membs, req_membs)),
    t_revlist_to_array (filts),
    NULL,
    orig_selid,
    NULL, (ptrlong)(0) );
  spar_selid_pop (sparp);
  return res;
}

SPART *
spar_gp_finalize_with_subquery (sparp_t *sparp, SPART *subquery)
{
  SPART *gp;
  gp = spar_gp_finalize (sparp);
  gp->_.gp.subquery = subquery;
  gp->_.gp.subtype = SELECT_L;
  return gp;
}

void spar_gp_add_member (sparp_t *sparp, SPART *memb)
{
  dk_set_t *set_ptr;
  spar_dbg_printf (("spar_gp_add_member ()\n"));
  if ((SPAR_GP == SPART_TYPE (memb)) && (OPTIONAL_L == memb->_.gp.subtype))
    set_ptr = (dk_set_t *)(&(sparp->sparp_env->spare_acc_opt_triples->data));
  else
    set_ptr = (dk_set_t *)(&(sparp->sparp_env->spare_acc_req_triples->data));
  t_set_push (set_ptr, memb);
}

int
spar_filter_is_freetext (SPART *filt)
{
  caddr_t fname;
  if (SPAR_FUNCALL != SPART_TYPE (filt))
    return 0;
  fname = filt->_.funcall.qname;
  if (!strcmp (fname, "bif:contains"))
    return SPAR_FT_CONTAINS;
  if (!strcmp (fname, "bif:xcontains"))
    return SPAR_FT_XCONTAINS;
  if (!strcmp (fname, "bif:xpath_contains"))
    return SPAR_FT_XPATH_CONTAINS;
  if (!strcmp (fname, "bif:xquery_contains"))
    return SPAR_FT_XQUERY_CONTAINS;
  return 0;
}

void
spar_gp_add_filter (sparp_t *sparp, SPART *filt)
{
  int filt_type = SPART_TYPE (filt);
  int ft_type;
  if (BOP_AND == filt_type)
    {
      spar_gp_add_filter (sparp, filt->_.bin_exp.left);
      spar_gp_add_filter (sparp, filt->_.bin_exp.right);
      return;
    }
  ft_type = spar_filter_is_freetext (filt);
  if (ft_type)
    {
      caddr_t ft_pred_name = filt->_.funcall.qname;
      SPART *ft_literal_var;
      caddr_t var_name;
      dk_set_t *req_triples_ptr;
      SPART *triple_with_var_obj = NULL;
      if (2 < BOX_ELEMENTS (filt->_.funcall.argtrees))
        spar_error (sparp, "Not enough parameters for special predicate %s()", ft_pred_name);
      ft_literal_var = filt->_.funcall.argtrees[0];
      if (SPAR_VARIABLE != SPART_TYPE (ft_literal_var))
        spar_error (sparp, "The first argument of special predicate %s() should be a variable", ft_pred_name);
      var_name = ft_literal_var->_.var.vname;
      req_triples_ptr = (dk_set_t *)(&(sparp->sparp_env->spare_acc_req_triples->data));
      DO_SET (SPART *, memb, req_triples_ptr)
        {
          SPART *obj;
          if (SPAR_TRIPLE != SPART_TYPE (memb))
            continue;
          obj = memb->_.triple.tr_object;
          if (SPAR_VARIABLE != SPART_TYPE (obj))
            continue;
          if (strcmp (obj->_.var.vname, var_name))
            continue;
          if (memb->_.triple.ft_type)
            spar_error (sparp, "More than one %s() or similar predicate for '$%s' variable in a single group", ft_pred_name, var_name);
          if (NULL == triple_with_var_obj)
            triple_with_var_obj = memb;
        }
      END_DO_SET ()
      if (NULL == triple_with_var_obj)
        spar_error (sparp, "The group does not contain triple pattern with '$%s' object before %s() predicate", var_name, ft_pred_name);
      triple_with_var_obj->_.triple.ft_type = ft_type;
    }
  t_set_push ((dk_set_t *)(&(sparp->sparp_env->spare_acc_filters->data)), filt);
}

void spar_gp_add_filter_for_graph (sparp_t *sparp, SPART *graph_expn, dk_set_t precodes, int suppress_filters_for_good_names)
{
  sparp_env_t *env = sparp->sparp_env;
  caddr_t varname;
  int precode_count = dk_set_length (precodes);
  SPART *graph_expn_copy, *filter;
  if (0 == precode_count)
    return;
  if (!SPAR_IS_BLANK_OR_VAR (graph_expn))
    return;
  varname = graph_expn->_.var.vname;
  if (suppress_filters_for_good_names)
    {
      dk_set_t good_varnames = env->spare_good_graph_varnames;
      if (0 <= dk_set_position_of_string (good_varnames, varname))
        return;
    }
  graph_expn_copy = (
            (SPAR_VARIABLE == SPART_TYPE (graph_expn)) ?
            spar_make_variable (sparp, varname) :
            spar_make_blank_node (sparp, varname, 0) );
#if 0
  filter = spar_make_funcall (sparp, 0, "LONG::bif:position",
    (SPART **)t_list (2,
              graph_expn_copy,
      spar_make_funcall (sparp, 0, "SPECIAL::sql:RDF_MAKE_GRAPH_IIDS_OF_QNAMES",
        (SPART **)t_list (1,
          spar_make_funcall (sparp, 0, "SQLVAL::bif:vector",
            (SPART **)t_list_to_array (precodes) ) ) ) ) );
#else
  if (1 == dk_set_length (precodes))
    filter = spartlist (sparp, 3,
      BOP_EQ, graph_expn_copy, precodes->data);
  else
    {
      t_set_push (&precodes, graph_expn_copy);
      filter = spartlist (sparp, 3, SPAR_BUILT_IN_CALL, (ptrlong)IN_L, 
        (SPART **)t_list_to_array (precodes) );
    }
#endif
          spar_gp_add_filter (sparp, filter);
}

void
spar_gp_add_filter_for_named_graph (sparp_t *sparp)
{
  sparp_env_t *env = sparp->sparp_env;
  SPART *graph_expn = (SPART *)(env->spare_context_graphs->data);
  spar_gp_add_filter_for_graph (sparp, graph_expn, env->spare_named_graph_precodes, 0);
}

SPART *spar_add_propvariable (sparp_t *sparp, SPART *lvar, int opcode, SPART *verb_qname, int verb_lexem_type, caddr_t verb_lexem_text)
{
  int lvar_blen, verb_qname_blen;
  caddr_t new_key;
  spar_propvariable_t *curr_pv = NULL;
  const char *optext = ((_PLUS_GT == opcode) ? "+>" : "*>");
  caddr_t *spec_pred_names = jso_triple_get_objs (
    (caddr_t *)(sparp->sparp_sparqre->sparqre_qi),
    verb_qname->_.lit.val,
    uname_virtrdf_ns_uri_isSpecialPredicate );
  if (0 != BOX_ELEMENTS (spec_pred_names))
    spar_error (sparp, "?%.200s %s ?%.200s is not allowed because it uses special predicate name", lvar->_.var.vname, optext, verb_lexem_text);
  if (NULL == sparp->sparp_env->spare_propvar_sets)
    spar_error (sparp, "?%.200s %s ?%.200s is used in illegal context", lvar->_.var.vname, optext, verb_lexem_text);

  lvar_blen = box_length (lvar->_.var.vname);
  verb_qname_blen = box_length (verb_qname->_.lit.val);
  new_key = t_alloc_box (lvar_blen + verb_qname_blen + 1, DV_STRING);
  memcpy (new_key, lvar->_.var.vname, lvar_blen);
  memcpy (new_key + lvar_blen - 1, optext, 2);
  memcpy (new_key + lvar_blen + 1, verb_qname->_.lit.val, verb_qname_blen);
  DO_SET (spar_propvariable_t *, prev, &(sparp->sparp_propvars))
    {
      if (strcmp (prev->sparpv_key, new_key))
        continue;
      if ((prev->sparpv_verb_lexem_type != verb_lexem_type) ||
        strcmp (prev->sparpv_verb_lexem_text, verb_lexem_text) )
        spar_error (sparp, "Variables ?%.200s %s %s?%.200s%s and ?%.200s %s %s?%.200s%s are written different ways but may mean the same; remove this ambiguity",
        lvar->_.var.vname, optext,
        ((Q_IRI_REF == verb_lexem_type) ? "<" : ""), verb_lexem_text,
        ((Q_IRI_REF == verb_lexem_type) ? ">" : ""),
        prev->sparpv_subj_var->_.var.vname, optext,
        ((Q_IRI_REF == prev->sparpv_verb_lexem_type) ? "<" : ""), prev->sparpv_verb_lexem_text,
        ((Q_IRI_REF == prev->sparpv_verb_lexem_type) ? ">" : "") );
      curr_pv = prev;
      break;
    }
  END_DO_SET();
  if (NULL == curr_pv)
    {
      curr_pv = (spar_propvariable_t *) t_alloc (sizeof (spar_propvariable_t));
      curr_pv->sparpv_subj_var = lvar;
      curr_pv->sparpv_op = opcode;
      curr_pv->sparpv_verb_qname = verb_qname;
      curr_pv->sparpv_verb_lexem_type = verb_lexem_type;
      curr_pv->sparpv_verb_lexem_text = verb_lexem_text;
      curr_pv->sparpv_key = new_key;
      /* Composing readable and short name for the generated variable */
      if (lvar_blen + strlen (verb_lexem_text) + ((Q_IRI_REF == verb_lexem_type) ? 2 : 0) < 30)
        {
          curr_pv->sparpv_obj_var_name = t_box_sprintf (40, "%s%s%s%s%s",
            lvar->_.var.vname, optext,
            ((Q_IRI_REF == verb_lexem_type) ? "<" : ""), verb_lexem_text,
            ((Q_IRI_REF == verb_lexem_type) ? ">" : "") );
          curr_pv->sparpv_obj_altered = 0;
        }
      else
        {
          char buf[20];
          sprintf (buf, "!pv!%d", (sparp->sparp_unictr)++);
          if (strlen(buf) + strlen (verb_lexem_text) + ((Q_IRI_REF == verb_lexem_type) ? 2 : 0) < 30)
            curr_pv->sparpv_obj_var_name = t_box_sprintf (40, "%s%s%s%s%s",
              buf, optext,
              ((Q_IRI_REF == verb_lexem_type) ? "<" : ""), verb_lexem_text,
              ((Q_IRI_REF == verb_lexem_type) ? ">" : "") );
          else
            curr_pv->sparpv_obj_var_name = t_box_sprintf (40, "%s%s!%d",
              buf, optext, (sparp->sparp_unictr)++ );
            curr_pv->sparpv_obj_altered = 0x1;
        }
      if (':' == curr_pv->sparpv_obj_var_name[0])
        {
          curr_pv->sparpv_obj_var_name[0] = '!';
          curr_pv->sparpv_obj_altered |= 0x2;
        }
      t_set_push (&(sparp->sparp_propvars), curr_pv);
      goto not_found_in_local_set; /* see below */ /* No need to search because this is the first occurence at all */
    }
  DO_SET (spar_propvariable_t *, prev, &(sparp->sparp_env->spare_propvar_sets->data))
    {
      if (strcmp (prev->sparpv_key, new_key))
        continue;
      goto in_local_set; /* see below */
    }
  END_DO_SET();

not_found_in_local_set:
  t_set_push ((dk_set_t *)(&(sparp->sparp_env->spare_propvar_sets->data)), (void *)curr_pv);

in_local_set:
  return spar_make_variable (sparp, curr_pv->sparpv_obj_var_name);
}

SPART **
spar_retvals_of_describe (sparp_t *sparp, SPART **retvals, caddr_t limit, caddr_t offset)
{
  int retval_ctr;
  dk_set_t vars = NULL;
  dk_set_t consts = NULL;
  dk_set_t graphs = NULL;
  SPART *descr_call;
  SPART *agg_call;
  SPART *var_vector_expn;
  SPART *var_vector_arg;
  caddr_t limofs_name;
  int need_limofs_trick = ((SPARP_MAXLIMIT != unbox (limit)) || (0 != unbox (offset)));
/* Making lists of variables, blank nodes, fixed triples, triples with variables and blank nodes. */
  for (retval_ctr = BOX_ELEMENTS_INT (retvals); retval_ctr--; /* no step */)
    {
      SPART *retval = retvals[retval_ctr];
      switch (SPART_TYPE(retval))
        {
          case SPAR_LIT: case SPAR_QNAME: /*case SPAR_QNAME_NS:*/
            t_set_push (&consts, retval);
            break;
          default:
            t_set_push (&vars, retval);
            break;
        }
    }
  DO_SET (SPART *, g, &(sparp->sparp_env->spare_default_graph_precodes))
    {
      t_set_push (&graphs, g);
    }
  END_DO_SET()
  DO_SET (SPART *, g, &(sparp->sparp_env->spare_named_graph_precodes))
    {
      t_set_push (&graphs, g);
    }
  END_DO_SET()
  var_vector_expn = spar_make_funcall (sparp, 0, "LONG::bif:vector",
    (SPART **)t_list_to_array (vars) );
  if (need_limofs_trick)
    {
      limofs_name = t_box_dv_short_string (":\"limofs\".\"describe-1\"");
      var_vector_arg = spar_make_variable (sparp, limofs_name);
    }
  else
    var_vector_arg = var_vector_expn;
  agg_call = spar_make_funcall (sparp, 0, "sql:SPARQL_DESC_AGG",
      (SPART **)t_list (1, var_vector_arg ) );
  descr_call = spar_make_funcall (sparp, 0, "sql:SPARQL_DESC_DICT",
      (SPART **)t_list (5,
        agg_call,
        spar_make_funcall (sparp, 0, "LONG::bif:vector",
          (SPART **)t_list_to_array (consts) ),
        ((NULL == graphs) ? (SPART *)t_NEW_DB_NULL :
          spar_make_funcall (sparp, 0, "LONG::bif:vector",
            (SPART **)t_list_to_array (graphs) ) ),
        sparp->sparp_env->spare_storage_name,
        spar_make_funcall (sparp, 0, "bif:vector", NULL) ) ); /*!!!TBD describe options will come here */
  if (need_limofs_trick)
    return (SPART **)t_list (2, descr_call,
      spartlist (sparp, 4, SPAR_ALIAS, var_vector_expn, t_box_dv_short_string ("describe-1"), SSG_VALMODE_AUTO) );
  else
  return (SPART **)t_list (1, descr_call);
}

int
sparp_gp_trav_add_rgc_vars_and_consts_from_retvals (sparp_t *sparp, SPART *curr, sparp_trav_state_t *sts_this, void *common_env)
{
  switch (SPART_TYPE (curr))
    {
    case SPAR_VARIABLE:
      if (!(SPART_VARR_GLOBAL & curr->_.var.rvr.rvrRestrictions))
        t_set_push_new_string (&(sparp->sparp_env->spare_grab.rgc_vars), t_box_dv_uname_string (curr->_.var.vname));
      break;
    case SPAR_QNAME:
      t_set_push_new_string (&(sparp->sparp_env->spare_grab.rgc_consts), curr->_.lit.val);
      break;
    }
  return 0;
}

void
spar_add_rgc_vars_and_consts_from_retvals (sparp_t *sparp, SPART **retvals)
{
  int retctr;
  DO_BOX_FAST (SPART *, retval, retctr, retvals)
    {
      sparp_gp_trav (sparp, retval, NULL,
        NULL, NULL,
        sparp_gp_trav_add_rgc_vars_and_consts_from_retvals, NULL, NULL,
        sparp_gp_trav_add_rgc_vars_and_consts_from_retvals );
    }
  END_DO_BOX_FAST;
}

SPART *spar_make_top (sparp_t *sparp, ptrlong subtype, SPART **retvals,
  caddr_t retselid, SPART *pattern, SPART **order, caddr_t limit, caddr_t offset)
{
  dk_set_t src = NULL;
  sparp_env_t *env = sparp->sparp_env;
  SPART **sources;
  DO_SET(SPART *, precode, &(env->spare_default_graph_precodes))
    {
      t_set_push (&src, spartlist (sparp, 2, FROM_L, sparp_tree_full_copy (sparp, precode, NULL)));
    }
  END_DO_SET()
  DO_SET(SPART *, precode, &(env->spare_named_graph_precodes))
    {
      t_set_push (&src, spartlist (sparp, 2, NAMED_L, sparp_tree_full_copy (sparp, precode, NULL)));
    }
  END_DO_SET()
  sources = (SPART **)t_revlist_to_array (src);
  if ((0 == BOX_ELEMENTS (sources)) &&
    (NULL != (env->spare_common_sponge_options)) )
    spar_error (sparp, "Retrieval options for source graphs (e.g., '%s') may be useless if the query does not contain 'FROM' or 'FROM NAMED'", env->spare_common_sponge_options->data);
  return spartlist (sparp, 16, SPAR_REQ_TOP, subtype,
    env->spare_output_valmode_name,
    env->spare_output_format_name,
    t_box_copy (env->spare_storage_name),
    retvals, NULL /* orig_retvals */, NULL /* expanded_orig_retvals */, retselid,
    sources, pattern, NULL, order,
    limit, offset, env );
}


static ptrlong usage_natural_restrictions[SPART_TRIPLE_FIELDS_COUNT] = {
  SPART_VARR_IS_REF | SPART_VARR_IS_IRI | SPART_VARR_NOT_NULL,	/* graph	*/
  SPART_VARR_IS_REF | SPART_VARR_NOT_NULL,			/* subject	*/
  SPART_VARR_IS_REF | SPART_VARR_NOT_NULL,			/* predicate	*/
  SPART_VARR_NOT_NULL };					/* object	*/

void
spar_gp_add_triple_or_special_filter (sparp_t *sparp, SPART *graph, SPART *subject, SPART *predicate, SPART *object, caddr_t qm_iri, SPART **options)
{
  sparp_env_t *env = sparp->sparp_env;
  SPART *triple;
  if (NULL == subject)
    subject = (SPART *)t_box_copy_tree (env->spare_context_subjects->data);
  if (NULL == predicate)
    predicate = (SPART *)t_box_copy_tree (env->spare_context_predicates->data);
  if (NULL == object)
    object = (SPART *)t_box_copy_tree (env->spare_context_objects->data);
  for (;;)
    {
      SPART *ctx_qm;
      if (NULL != qm_iri)
        break;
      if (NULL == env->spare_context_qms)
        {
          qm_iri = (caddr_t)(_STAR);
          break;
        }
      ctx_qm = env->spare_context_qms->data;
      if (DV_ARRAY_OF_POINTER == DV_TYPE_OF (ctx_qm))
        {
          qm_iri = ctx_qm->_.lit.val;
          break;
        }
      ctx_qm = (SPART *)qm_iri;
      break;
    }
  if (SPAR_QNAME == SPART_TYPE (predicate))
    {
      caddr_t *spec_pred_names = jso_triple_get_objs (
        (caddr_t *)(sparp->sparp_sparqre->sparqre_qi),
        predicate->_.lit.val,
        uname_virtrdf_ns_uri_isSpecialPredicate );
      if (0 != BOX_ELEMENTS (spec_pred_names))
        {
          spar_gp_add_filter (sparp,
            spar_make_funcall (sparp, 0, spec_pred_names[0],
              (SPART **)t_list (2, subject, object) ) );
          return;
        }
    }
  for (;;)
    {
      dk_set_t dflts;
      if (NULL != graph)
        break;
      if (env->spare_context_graphs)
        {
        graph = (SPART *)t_box_copy_tree (env->spare_context_graphs->data);
          break;
        }
      dflts = env->spare_default_graph_precodes;
      if ((NULL != dflts) && (NULL == dflts->next))
        { /* If there's only one default graph then we can cheat and optimize the query a little bit by adding a restriction to the variable */
          SPART *single_dflt = (SPART *)(dflts->data);
          if (SPAR_FUNCALL == SPART_TYPE (single_dflt))	 /* FROM iriref OPTION (...) case */
        {
              SPART *iri_arg = single_dflt->_.funcall.argtrees[0];
              SPART *eq;
              graph = spar_make_blank_node (sparp, spar_mkid (sparp, "_::default"), 1);
              eq = spartlist (sparp, 3, BOP_EQ, sparp_tree_full_copy (sparp, graph, NULL), sparp_tree_full_copy (sparp, single_dflt, NULL));
              spar_gp_add_filter (sparp, eq);
              if (SPAR_QNAME == SPART_TYPE (iri_arg))
                {
	      graph->_.var.rvr.rvrRestrictions |= SPART_VARR_FIXED | SPART_VARR_IS_REF | SPART_VARR_NOT_NULL;
                  graph->_.var.rvr.rvrFixedValue = t_box_copy (iri_arg->_.lit.val);
                }
              else
                graph->_.var.rvr.rvrRestrictions |= SPART_VARR_IS_REF | SPART_VARR_NOT_NULL;
              break;
            }
	/* Single FROM iriref without sponge options */
          graph = sparp_tree_full_copy (sparp, single_dflt, NULL);
          break;
	}
      graph = spar_make_blank_node (sparp, spar_mkid (sparp, "_::default"), 1);
      spar_gp_add_filter_for_graph (sparp, graph, dflts, 0);
      break;
    }
  if (SPAR_IS_BLANK_OR_VAR (graph))
    graph->_.var.selid = env->spare_selids->data;
  triple = spar_make_plain_triple (sparp, graph, subject, predicate, object, qm_iri, options);
  spar_gp_add_member (sparp, triple);
}

SPART *
spar_make_plain_triple (sparp_t *sparp, SPART *graph, SPART *subject, SPART *predicate, SPART *object, caddr_t qm_iri, SPART **options)
{
  sparp_env_t *env = sparp->sparp_env;
  caddr_t key;
  int fctr;
  SPART *triple;
  key = t_box_sprintf (0x100, "%s-t%d", env->spare_selids->data, sparp->sparp_key_gen);
  sparp->sparp_key_gen += 1;
  triple = spartlist (sparp, 16, SPAR_TRIPLE,
    graph, subject, predicate, object, qm_iri,
    env->spare_selids->data, key, NULL,
    NULL, NULL, NULL, NULL,
    options, (ptrlong)0, (ptrlong)((sparp->sparp_unictr)++) );
  for (fctr = 0; fctr < SPART_TRIPLE_FIELDS_COUNT; fctr++)
    {
      SPART *fld = triple->_.triple.tr_fields[fctr];
      ptrlong ft = SPART_TYPE(fld);
      if ((SPAR_VARIABLE == ft) || (SPAR_BLANK_NODE_LABEL == ft))
        {
          fld->_.var.rvr.rvrRestrictions |= usage_natural_restrictions[fctr];
          fld->_.var.tabid = key;
          fld->_.var.tr_idx = fctr;
          if (!(SPART_VARR_GLOBAL & fld->_.var.rvr.rvrRestrictions))
            t_set_push_new_string (&(env->spare_good_graph_varnames), fld->_.var.vname);
        }
      if ((env->spare_grab.rgc_all) && (SPART_TRIPLE_PREDICATE_IDX != fctr))
        {
          if ((SPAR_VARIABLE == ft) && !(SPART_VARR_GLOBAL & fld->_.var.rvr.rvrRestrictions))
            t_set_push_new_string (&(env->spare_grab.rgc_vars), t_box_dv_uname_string (fld->_.var.vname));
          else if (SPAR_QNAME == ft)
            t_set_push_new_string (&(env->spare_grab.rgc_consts), fld->_.lit.val);
        }
      if ((NULL != env->spare_grab.rgc_sa_preds) &&
        (SPART_TRIPLE_SUBJECT_IDX == fctr) &&
        (SPAR_VARIABLE == ft) &&
        !(env->spare_grab.rgc_all) )
        {
          SPART *obj = triple->_.triple.tr_object;
          ptrlong objt = SPART_TYPE(obj);
          if (
            ((SPAR_VARIABLE == objt) &&
              (0 <= dk_set_position_of_string (env->spare_grab.rgc_vars, obj->_.var.vname)) ) ||
            ((SPAR_QNAME == objt) &&
              (0 <= dk_set_position_of_string (env->spare_grab.rgc_consts, obj->_.lit.val)) ) )
            {
              t_set_push_new_string (&(env->spare_grab.rgc_sa_vars), t_box_dv_uname_string (fld->_.var.vname));
            }
        }
    }
  return triple;
}

SPART *spar_make_variable (sparp_t *sparp, caddr_t name)
{
  sparp_env_t *env = sparp->sparp_env;
  SPART *res;
  int is_global = SPART_VARNAME_IS_GLOB(name);
  caddr_t selid = NULL;
#ifdef DEBUG
  caddr_t rvr_list_test[] = {SPART_RVR_LIST_OF_NULLS};
  if (sizeof (rvr_list_test) != sizeof (rdf_val_range_t))
    GPF_T; /* Don't forget to add NULLS to SPART_RVR_LIST_OF_NULLS when adding fields to rdf_val_range_t */
#endif
  if (is_global)
    {
      t_set_push_new_string (&(sparp->sparp_env->spare_global_var_names), name);
    }
  if (sparp->sparp_in_precode_expn && !is_global)
    spar_error (sparp, "non-global variable '%.100s' can not be used outside any group pattern or result-set list");
  if (NULL != env->spare_selids)
    selid = env->spare_selids->data;
  else if (is_global) /* say, 'insert in graph ?:someglobalvariable {...} where {...} */
    selid = t_box_dv_uname_string ("(global)");
  else
    spar_internal_error (sparp, "non-global variable outside any group pattern or result-set list");
  res = spartlist (sparp, 6 + (sizeof (rdf_val_range_t) / sizeof (caddr_t)),
      SPAR_VARIABLE, name,
      selid, NULL,
      (ptrlong)(0), SPART_BAD_EQUIV_IDX, SPART_RVR_LIST_OF_NULLS );
  res->_.var.rvr.rvrRestrictions = (is_global ? SPART_VARR_GLOBAL : 0);
  return res;
}

SPART *spar_make_blank_node (sparp_t *sparp, caddr_t name, int bracketed)
{
  sparp_env_t *env = sparp->sparp_env;
  SPART *res;
  res = spartlist (sparp, 6 + (sizeof (rdf_val_range_t) / sizeof (caddr_t)),
      SPAR_BLANK_NODE_LABEL, name,
      env->spare_selids->data, NULL,
      (ptrlong)(bracketed), SPART_BAD_EQUIV_IDX, SPART_RVR_LIST_OF_NULLS );
  res->_.var.rvr.rvrRestrictions = /*SPART_VARR_IS_REF | SPART_VARR_IS_BLANK |*/ SPART_VARR_NOT_NULL;
  return res;
}

SPART *spar_make_typed_literal (sparp_t *sparp, caddr_t strg, caddr_t type, caddr_t lang)
{
  dtp_t tgt_dtp;
  caddr_t parsed_value = NULL;
  sql_tree_tmp *tgt_dtp_tree;  
  SPART *res;
  if (NULL != lang)
    return spartlist (sparp, 4, SPAR_LIT, strg, type, lang);
  if (uname_xmlschema_ns_uri_hash_boolean == type)
    {
      if (!strcmp ("true", strg))
        return spartlist (sparp, 4, SPAR_LIT, (ptrlong)1, type, NULL);
      if (!strcmp ("false", strg))
        return spartlist (sparp, 4, SPAR_LIT, (ptrlong)0, type, NULL);
      goto cannot_cast;
    }
  if (uname_xmlschema_ns_uri_hash_date == type)
    {
      tgt_dtp = DV_DATE;
      goto do_sql_cast;
    }
  if (uname_xmlschema_ns_uri_hash_dateTime == type)
    {
      tgt_dtp = DV_DATETIME;
      goto do_sql_cast;
    }
  if (uname_xmlschema_ns_uri_hash_decimal == type)
    {
      tgt_dtp = DV_NUMERIC;
      goto do_sql_cast;
    }
  if (uname_xmlschema_ns_uri_hash_double == type)
    {
      tgt_dtp = DV_DOUBLE_FLOAT;
      goto do_sql_cast;
    }
  if (uname_xmlschema_ns_uri_hash_float == type)
    {
      tgt_dtp = DV_SINGLE_FLOAT;
      goto do_sql_cast;
    }
  if (uname_xmlschema_ns_uri_hash_integer == type)
    {
      tgt_dtp = DV_LONG_INT;
      goto do_sql_cast;
    }
  if (uname_xmlschema_ns_uri_hash_time == type)
    {
      tgt_dtp = DV_TIME;
      goto do_sql_cast;
    }
  if (uname_xmlschema_ns_uri_hash_string == type)
    {
      return spartlist (sparp, 4, SPAR_LIT, strg, type, NULL);
    }
  return spartlist (sparp, 4, SPAR_LIT, strg, type, NULL);

do_sql_cast:
  tgt_dtp_tree = (sql_tree_tmp *)t_list (3, (ptrlong)tgt_dtp, (ptrlong)0, (ptrlong)0);
  parsed_value = box_cast ((caddr_t *)(sparp->sparp_sparqre->sparqre_qi), strg, tgt_dtp_tree, DV_STRING);
  res = spartlist (sparp, 4, SPAR_LIT, t_full_box_copy_tree (parsed_value), type, NULL);
  dk_free_tree (parsed_value);
  return res;

cannot_cast:
  sparyyerror_impl (sparp, strg, "The string representation can not be converted to a valid typed value");
  return NULL;
}

SPART *sparp_make_graph_precode (sparp_t *sparp, SPART *iriref, SPART **options)
{
  rdf_grab_config_t *rgc_ptr = &(sparp->sparp_env->spare_grab);
  dk_set_t *opts_ptr = &(sparp->sparp_env->spare_common_sponge_options);
  SPART **mixed_options;
  int common_count;
  if (NULL != rgc_ptr->rgc_sa_preds)
    {
      t_set_push_new_string (&(rgc_ptr->rgc_sa_graphs),
        ((DV_ARRAY_OF_POINTER == DV_TYPE_OF (iriref)) ? iriref->_.lit.val : (caddr_t)iriref) );
    }
  if ((NULL == options) && (0 > dk_set_position_of_string (opts_ptr[0], "get:soft")))
    return iriref;
  common_count = dk_set_length (opts_ptr[0]);
  if (0 < common_count)
    {
      int ctr;
      SPART **mixed_tail = mixed_options = (SPART **)t_alloc_box (common_count * sizeof (SPART *), DV_ARRAY_OF_POINTER);
      DO_SET (SPART *, val, opts_ptr)
        {
          (mixed_tail++)[0] = (SPART *)t_full_box_copy_tree ((caddr_t)(val));
        }
      END_DO_SET()
      for (ctr = BOX_ELEMENTS_0 (options) - 1; 0 <= ctr; ctr -= 2)
        {
          caddr_t param = (caddr_t)(options[ctr]);
          const char **chk;
          for (chk = sparp_known_get_params; (NULL != chk[0]) && strcmp (chk[0], param); chk++) ;
          if (NULL == chk[0])
            spar_error (sparp, "Unsupported parameter '%.30s' in FROM ... (OPTION ...)", param);
          if (0 < dk_set_position_of_string (opts_ptr[0], param))
            spar_error (sparp, "FROM ... (OPTION ... %s ...) conflicts with 'DEFINE %s ...", param, param);
          (mixed_tail++)[0] = (SPART *)t_full_box_copy_tree (param);
          (mixed_tail++)[0] = (SPART *)t_full_box_copy_tree ((caddr_t)(options[ctr + 1]));
        }
    }
  else
    mixed_options = options;
  return spar_make_funcall (sparp, 0, "SPECIAL::bif:iri_to_id",
    (SPART **)t_list (1,
      spar_make_funcall (sparp, 0, "sql:RDF_SPONGE_UP",
    (SPART **)t_list (2,
       iriref,
           spar_make_funcall (sparp, 0, "bif:vector", mixed_options) ) ) ) );
}

SPART *
spar_make_funcall (sparp_t *sparp, int aggregate_mode, const char *funname, SPART **args)
{
  if (NULL == args)
    args = (SPART **)t_list (0);
  return spartlist (sparp, 4, SPAR_FUNCALL, t_box_dv_short_string (funname), args, (ptrlong)aggregate_mode);
}

SPART *
spar_make_sparul_clear (sparp_t *sparp, SPART *graph_precode)
{
  SPART **fake_sol;
  SPART *top;
  spar_selid_push (sparp);
  fake_sol = spar_make_fake_action_solution (sparp);
  top = spar_make_top (sparp, CLEAR_L,
    (SPART **)t_list (1, spar_make_funcall (sparp, 0, "sql:SPARUL_CLEAR",
        (SPART **)t_list (1, graph_precode) ) ),
    spar_selid_pop (sparp),
    fake_sol[0], (SPART **)(fake_sol[1]), (caddr_t)(fake_sol[2]), (caddr_t)(fake_sol[3]) );
  return top;
}

SPART *
spar_make_sparul_load (sparp_t *sparp, SPART *graph_precode, SPART *src_precode)
{
  SPART **fake_sol;
  SPART *top;
  spar_selid_push (sparp);
  fake_sol = spar_make_fake_action_solution (sparp);
  top = spar_make_top (sparp, LOAD_L,
    (SPART **)t_list (1, spar_make_funcall (sparp, 0, "sql:SPARUL_LOAD",
        (SPART **)t_list (2, graph_precode, src_precode) ) ),
    spar_selid_pop (sparp),
    fake_sol[0], (SPART **)(fake_sol[1]), (caddr_t)(fake_sol[2]), (caddr_t)(fake_sol[3]) );
  return top;
}

SPART *
spar_make_topmost_sparul_sql (sparp_t *sparp, SPART **actions)
{
  caddr_t saved_format_name = sparp->sparp_env->spare_output_format_name;
  caddr_t saved_valmode_name = sparp->sparp_env->spare_output_valmode_name;
  SPART **fake_sol;
  SPART *top;
  SPART **action_sqls;
  int action_ctr, action_count = BOX_ELEMENTS (actions);
  if (1 == action_count)
    return actions[0]; /* No need to make grouping around single action. */
/* First of all, every tree for every action is compiled into string literal containing SQL text. */
  action_sqls = (SPART **)t_alloc_box (action_count * sizeof (SPART *), DV_ARRAY_OF_POINTER);
  sparp->sparp_env->spare_output_format_name = NULL;
  sparp->sparp_env->spare_output_valmode_name = NULL;
  if (NULL != sparp->sparp_expr)
    spar_internal_error (sparp, "spar_" "make_topmost_sparul_sql() is called after start of some tree rewrite");
  DO_BOX_FAST (SPART *, action, action_ctr, actions)
    {
      spar_sqlgen_t ssg;
      sql_comp_t sc;
      caddr_t action_sql;
      sparp->sparp_expr = action;
      sparp_rewrite_all (sparp, 0 /* no cloning -- no need in safely_copy_retvals */);
  /*xt_check (sparp, sparp->sparp_expr);*/
#ifndef NDEBUG
      t_check_tree (sparp->sparp_expr);
#endif
      memset (&ssg, 0, sizeof (spar_sqlgen_t));
      memset (&sc, 0, sizeof (sql_comp_t));
      if (NULL != sparp->sparp_sparqre->sparqre_qi)
        sc.sc_client = sparp->sparp_sparqre->sparqre_qi->qi_client;
      ssg.ssg_out = strses_allocate ();
      ssg.ssg_sc = &sc;
      ssg.ssg_sparp = sparp;
      ssg.ssg_tree = sparp->sparp_expr;
      ssg.ssg_sources = ssg.ssg_tree->_.req_top.sources; /*!!!TBD merge with environment */
      ssg_make_sql_query_text (&ssg);
      action_sql = strses_string (ssg.ssg_out);
      strses_free (ssg.ssg_out);
      action_sqls [action_ctr] = spartlist (sparp, 4, SPAR_LIT, action_sql, NULL, NULL);
      sparp->sparp_expr = NULL;
    }
  END_DO_BOX_FAST;
  sparp->sparp_env->spare_output_format_name = saved_format_name;
  sparp->sparp_env->spare_output_valmode_name = saved_valmode_name;
  spar_selid_push (sparp);
  fake_sol = spar_make_fake_action_solution (sparp);
  top = spar_make_top (sparp, LOAD_L,
    (SPART **)t_list (1, spar_make_funcall (sparp, 0, "sql:SPARUL_RUN", action_sqls)),
    spar_selid_pop (sparp),
    fake_sol[0], (SPART **)(fake_sol[1]), (caddr_t)(fake_sol[2]), (caddr_t)(fake_sol[3]) );
  return top;
}


SPART **
spar_make_fake_action_solution (sparp_t *sparp)
{
  SPART * fake_gp;
  spar_gp_init (sparp, WHERE_L);
  fake_gp = spar_gp_finalize (sparp);
  return (SPART **)t_list (4,
    fake_gp, NULL, t_box_num(1), t_box_num(0));
}

id_hashed_key_t
spar_var_hash (caddr_t p_data)
{
  SPART *v = ((SPART **)p_data)[0];
  char *str;
  id_hashed_key_t h1, h2;
  str = v->_.var.tabid;
  if (NULL != str)
    BYTE_BUFFER_HASH (h1, str, strlen (str));
  else
    h1 = 0;
  str = v->_.var.vname;
  BYTE_BUFFER_HASH (h2, str, strlen (str));
  return ((h1 ^ h2 ^ v->_.var.tr_idx) & ID_HASHED_KEY_MASK);
}


int 
spar_var_cmp (caddr_t p_data1, caddr_t p_data2)
{
  SPART *v1 = ((SPART **)p_data1)[0];
  SPART *v2 = ((SPART **)p_data2)[0];
  int res;
  res = ((v2->_.var.tr_idx > v1->_.var.tr_idx) ? 1 :
    ((v2->_.var.tr_idx < v1->_.var.tr_idx) ? -1 : 0) );
  if (0 != res) return res;
  res = strcmp (v1->_.var.vname, v2->_.var.vname);
  if (0 != res) return res;
  return strcmp (v1->_.var.tabid, v2->_.var.tabid);
}


caddr_t
spar_query_lex_analyze (caddr_t str, wcharset_t *query_charset)
{
  if (!DV_STRINGP(str))
    {
      return list (1, list (3, (ptrlong)0, (ptrlong)0, box_dv_short_string ("SPARQL analyzer: input text is not a string")));
    }
  else
    {
      dk_set_t lexems = NULL;
      caddr_t result_array;
      int param_ctr = 0;
      spar_query_env_t sparqre;
      sparp_t *sparp;
      sparp_env_t *se;
      MP_START ();
      memset (&sparqre, 0, sizeof (spar_query_env_t));
      sparp = (sparp_t *)t_alloc (sizeof (sparp_t));
      memset (sparp, 0, sizeof (sparp_t));
      se = (sparp_env_t *)t_alloc (sizeof (sparp_env_t));
      memset (se, 0, sizeof (sparp_env_t));
      sparqre.sparqre_param_ctr = &param_ctr;
      sparp->sparp_sparqre = &sparqre;
      sparp->sparp_text = t_box_copy (str);
      sparp->sparp_env = se;
      sparp->sparp_synthighlight = 1;
      sparp->sparp_err_hdr = t_box_dv_short_string ("SPARQL analyzer");
      if (NULL == query_charset)
	query_charset = default_charset;
      if (NULL == query_charset)
	sparp->sparp_enc = &eh__ISO8859_1;
      else
	{
	  sparp->sparp_enc = eh_get_handler (CHARSET_NAME (query_charset, NULL));
	  if (NULL == sparp->sparp_enc)
	    sparp->sparp_enc = &eh__ISO8859_1;
	}
      sparp->sparp_lang = server_default_lh;

      spar_fill_lexem_bufs (sparp);
      DO_SET (spar_lexem_t *, buf, &(sparp->sparp_output_lexem_bufs))
	{
	  int buflen = box_length (buf) / sizeof( spar_lexem_t);
	  int ctr;
	  for (ctr = 0; ctr < buflen; ctr++)
	    {
	      spar_lexem_t *curr = buf+ctr;
	      if (0 == curr->sparl_lex_value)
		break;
#ifdef SPARQL_DEBUG
	      dk_set_push (&lexems, list (5,
		box_num (curr->sparl_lineno),
		curr->sparl_depth,
		box_copy (curr->sparl_raw_text),
		curr->sparl_lex_value,
		curr->sparl_state ) );
#else
	      dk_set_push (&lexems, list (4,
		box_num (curr->sparl_lineno),
		curr->sparl_depth,
		box_copy (curr->sparl_raw_text),
		curr->sparl_lex_value ) );
#endif
	    }
	}
      END_DO_SET();
      if (NULL != sparp->sparp_sparqre->sparqre_catched_error)
	{
	  dk_set_push (&lexems, list (3,
		((NULL != sparp->sparp_curr_lexem) ? sparp->sparp_curr_lexem->sparl_lineno : (ptrlong)0),
		sparp->sparp_lexdepth,
		box_copy (ERR_MESSAGE (sparp->sparp_sparqre->sparqre_catched_error)) ) );
	}
      sparp_free (sparp);
      MP_DONE ();
      result_array = revlist_to_array (lexems);
      return result_array;
    }
}


const char *spart_dump_opname (ptrlong opname, int is_op)
{

  if (is_op)
    switch (opname)
    {
    case BOP_AND: return "boolean operation 'AND'";
    case BOP_OR: return "boolean operation 'OR'";
    case BOP_NOT: return "boolean operation 'NOT'";
    case BOP_EQ: return "boolean operation '='";
    case BOP_NEQ: return "boolean operation '!='";
    case BOP_LT: return "boolean operation '<'";
    case BOP_LTE: return "boolean operation '<='";
    case BOP_GT: return "boolean operation '>'";
    case BOP_GTE: return "boolean operation '>='";
    /*case BOP_LIKE: Like is built-in in SPARQL, not a BOP! return "boolean operation 'like'"; */
    case BOP_SAME: return "boolean operation '=='";
    case BOP_NSAME: return "boolean operation '!=='";
    case BOP_PLUS: return "arithmetic operation '+'";
    case BOP_MINUS: return "arithmetic operation '-'";
    case BOP_TIMES: return "arithmetic operation '*'";
    case BOP_DIV: return "arithmetic operation 'div'";
    case BOP_MOD: return "arithmetic operation 'mod'";
    }

  switch (opname)
    {
    case _LBRA: return "quad mapping parent group name";
    case ASC_L: return "ascending order";
    case ASK_L: return "ASK result-mode";
    case BOUND_L: return "BOUND builtin";
    case CONSTRUCT_L: return "CONSTRUCT result-mode";
    case CREATE_L: return "quad mapping name";
    case DATATYPE_L: return "DATATYPE builtin";
    case DESC_L: return "descending";
    case DESCRIBE_L: return "DESCRIBE result-mode";
    case DISTINCT_L: return "SELECT DISTINCT result-mode";
    case false_L: return "false boolean";
    case FILTER_L: return "FILTER";
    case FROM_L: return "FROM";
    case GRAPH_L: return "GRAPH gp";
    case IRI_L: return "IRI builtin";
    case IN_L: return "IN";
    case isBLANK_L: return "isBLANK builtin";
    case isIRI_L: return "isIRI builtin";
    case isLITERAL_L: return "isLITERAL builtin";
    case isURI_L: return "isURI builtin";
    case LANG_L: return "LANG builtin";
    case LANGMATCHES_L: return "LANGMATCHES builtin";
    case LIKE_L: return "LIKE";
    case LIMIT_L: return "LIMIT";
    case NAMED_L: return "NAMED";
    case NIL_L: return "NIL";
    case OBJECT_L: return "OBJECT";
    case OFFSET_L: return "OFFSET";
    case OPTIONAL_L: return "OPTIONAL gp";
    case ORDER_L: return "ORDER";
    case PREDICATE_L: return "PREDICATE";
    case PREFIX_L: return "PREFIX";
    case REGEX_L: return "REGEX builtin";
    case SAMETERM_L: return "sameTerm builtin";
    case SELECT_L: return "SELECT result-mode";
    case STR_L: return "STR builtin";
    case SUBJECT_L: return "SUBJECT";
    case true_L: return "true boolean";
    case UNION_L: return "UNION gp";
    case WHERE_L: return "WHERE gp";

    case SPAR_BLANK_NODE_LABEL: return "blank node label";
    case SPAR_BUILT_IN_CALL: return "built-in call";
    case SPAR_FUNCALL: return "function call";
    case SPAR_GP: return "group pattern";
    case SPAR_LIT: return "lit";
    case SPAR_QNAME: return "QName";
    /*case SPAR_QNAME_NS: return "QName NS";*/
    case SPAR_REQ_TOP: return "SPARQL query";
    case SPAR_VARIABLE: return "Variable";
    case SPAR_TRIPLE: return "Triple";
  }
  return NULL;
}


char *spart_dump_addr (void *addr)
{
  return NULL;
}


void spart_dump_long (void *addr, dk_session_t *ses, int is_op)
{
  if (!IS_BOX_POINTER(addr))
    {
      const char *op_descr = spart_dump_opname((ptrlong)(addr), is_op);
      if (NULL != op_descr)
	{
	  SES_PRINT (ses, op_descr);
	  return;
	}
    }
  else
    {
      char *addr_descr = spart_dump_addr(addr);
      if (NULL != addr_descr)
	{
	  SES_PRINT (ses, addr_descr);
	  return;
	}
    }
  {
    char buf[30];
    sprintf (buf, "LONG " BOXINT_FMT, unbox (addr));
    SES_PRINT (ses, buf);
    return;
  }
}

void spart_dump_varr_bits (dk_session_t *ses, int varr_bits)
{
  char buf[200];
  char *tail = buf;
#define VARR_BIT(b,txt) \
  do { \
    if (varr_bits & (b)) \
      { const char *t = (txt); while ('\0' != (tail[0] = (t++)[0])) tail++; } \
    } while (0);
  VARR_BIT (SPART_VARR_CONFLICT, " CONFLICT");
  VARR_BIT (SPART_VARR_GLOBAL, " GLOBAL");
  VARR_BIT (SPART_VARR_ALWAYS_NULL, " always-NULL");
  VARR_BIT (SPART_VARR_NOT_NULL, " notNULL");
  VARR_BIT (SPART_VARR_FIXED, " fixed");
  VARR_BIT (SPART_VARR_TYPED, " typed");
  VARR_BIT (SPART_VARR_IS_LIT, " lit");
  VARR_BIT (SPART_VARR_IRI_CALC, " IRI-namecalc");
  VARR_BIT (SPART_VARR_SPRINTFF, " SprintfF");
  VARR_BIT (SPART_VARR_IS_BLANK, " bnode");
  VARR_BIT (SPART_VARR_IS_IRI, " IRI");
  VARR_BIT (SPART_VARR_IS_REF, " reference");
  VARR_BIT (SPART_VARR_EXPORTED, " exported");
  session_buffered_write (ses, buf, tail-buf);
}

void spart_dump_rvr (dk_session_t *ses, rdf_val_range_t *rvr)
{
  char buf[300];
  char *tail = buf;
  int len;
  int varr_bits = rvr->rvrRestrictions;
  ccaddr_t fixed_dt = rvr->rvrDatatype;
  ccaddr_t fixed_val = rvr->rvrFixedValue;
  spart_dump_varr_bits (ses, varr_bits);
  if (varr_bits & SPART_VARR_TYPED)
    {
      len = sprintf (tail, "; dt=%.100s", fixed_dt);
      tail += len;
    }
  if (varr_bits & SPART_VARR_FIXED)
    {
      dtp_t dtp = DV_TYPE_OF (fixed_val);
      const char *dtp_name = dv_type_title (dtp);
      const char *meta = "";
      const char *lit_dt = NULL;
      const char *lit_lang = NULL;
      if (DV_ARRAY_OF_POINTER == dtp)
        {
          SPART *fixed_tree = ((SPART *)fixed_val);
          if (SPAR_QNAME == SPART_TYPE (fixed_tree))
            {
              meta = " QName";
              fixed_val = fixed_tree->_.lit.val;
            }
          else if (SPAR_LIT == SPART_TYPE (fixed_tree))
            {
              meta = " lit";
              fixed_val = fixed_tree->_.lit.val;
              lit_dt = fixed_tree->_.lit.datatype;
              lit_lang = fixed_tree->_.lit.language;
            }
          dtp = DV_TYPE_OF (fixed_val);
          dtp_name = dv_type_title (dtp);
        }
      if (IS_STRING_DTP (dtp))
        len = sprintf (tail, "; fixed%s %s '%.100s'", meta, dtp_name, fixed_val);
      else if (DV_LONG_INT == dtp)
        len = sprintf (tail, "; fixed%s %s %ld", meta, dtp_name, (long)(unbox (fixed_val)));
      else
        len = sprintf (tail, "; fixed%s %s", meta, dtp_name);
      tail += len;
      if (NULL != lit_dt)
        tail += sprintf (tail, "^^'%.50s'", lit_dt);
      if (NULL != lit_lang)
        tail += sprintf (tail, "@'%.50s'", lit_lang);
      SES_PRINT (ses, buf);
    }
  if (rvr->rvrIriClassCount)
    {
      int iricctr;
      SES_PRINT (ses, "; IRI classes");
      for (iricctr = 0; iricctr < rvr->rvrIriClassCount; iricctr++)
        {
          SES_PRINT (ses, " ");
          SES_PRINT (ses, rvr->rvrIriClasses[iricctr]);
        }
    }
  if (rvr->rvrRedCutCount)
    {
      int rcctr;
      SES_PRINT (ses, "; Not one of");
      for (rcctr = 0; rcctr < rvr->rvrRedCutCount; rcctr++)
        {
          SES_PRINT (ses, " ");
          SES_PRINT (ses, rvr->rvrRedCuts[rcctr]);
        }
    }
  if (rvr->rvrSprintffs)
    {
      int sffctr;
      SES_PRINT (ses, "; Formats ");
      for (sffctr = 0; sffctr < rvr->rvrSprintffCount; sffctr++)
        {
          SES_PRINT (ses, " |");
          SES_PRINT (ses, rvr->rvrSprintffs[sffctr]);
          SES_PRINT (ses, "|");
        }
    }
}


void
spart_dump (void *tree_arg, dk_session_t *ses, int indent, const char *title, int hint)
{
  SPART *tree = (SPART *) tree_arg;
  int ctr;
  if ((NULL == tree) && (hint < 0))
    return;
  if (indent > 0)
    {
      session_buffered_write_char ('\n', ses);
      for (ctr = indent; ctr--; /*no step*/ )
        session_buffered_write_char (' ', ses);
    }
  if (title)
    {
      SES_PRINT (ses, title);
      SES_PRINT (ses, ": ");
    }
  if ((-1 == hint) && IS_BOX_POINTER(tree))
    {
      if (DV_ARRAY_OF_POINTER != DV_TYPE_OF (tree))
        {
          SES_PRINT (ses, "special: ");
          hint = 0;
        }
      else if ((SPART_HEAD >= BOX_ELEMENTS(tree)) || IS_BOX_POINTER (tree->type))
        {
          SES_PRINT (ses, "special: ");
          hint = -2;
        }
    }
  if (!hint)
    hint = DV_TYPE_OF (tree);
  switch (hint)
    {
    case -1:
      {
	int childrens;
	char buf[50];
	if (!IS_BOX_POINTER(tree))
	  {
	    SES_PRINT (ses, "[");
	    spart_dump_long (tree, ses, 0);
	    SES_PRINT (ses, "]");
	    goto printed;
	  }
        sprintf (buf, "(line %d) ", (int) (ptrlong) tree->srcline);
        SES_PRINT (ses, buf);
	childrens = BOX_ELEMENTS (tree);
	switch (tree->type)
	  {
	  case SPAR_ALIAS:
	    {
	      sprintf (buf, "ALIAS:");
	      SES_PRINT (ses, buf);
	      spart_dump (tree->_.alias.arg, ses, indent+2, "VALUE", 0);
	      spart_dump (tree->_.alias.aname, ses, indent+2, "ALIAS NAME", 0);
		/* _.alias.native is temp so it is not printed */
	      break;
	    }
	  case SPAR_BLANK_NODE_LABEL:
	    {
	      sprintf (buf, "BLANK NODE:");
	      SES_PRINT (ses, buf);
	      spart_dump (tree->_.var.vname, ses, indent+2, "NAME", 0);
	      spart_dump (tree->_.var.selid, ses, indent+2, "SELECT ID", 0);
	      spart_dump (tree->_.var.tabid, ses, indent+2, "TABLE ID", 0);
	      break;
	    }
	  case SPAR_BUILT_IN_CALL:
	    {
	      sprintf (buf, "BUILT-IN CALL:");
	      SES_PRINT (ses, buf);
	      spart_dump_long ((void *)(tree->_.builtin.btype), ses, -1);
	      spart_dump (tree->_.builtin.args, ses, indent+2, "ARGUMENT", -2);
	      break;
	    }
	  case SPAR_FUNCALL:
	    {
	      int argctr, argcount = BOX_ELEMENTS (tree->_.funcall.argtrees);
	      spart_dump (tree->_.funcall.qname, ses, indent+2, "FUNCTION NAME", 0);
              if (tree->_.funcall.agg_mode)
		spart_dump ((void *)(tree->_.funcall.agg_mode), ses, indent+2, "AGGREGATE MODE", 0);
	      for (argctr = 0; argctr < argcount; argctr++)
		spart_dump (tree->_.funcall.argtrees[argctr], ses, indent+2, "ARGUMENT", -1);
	      break;
	    }
	  case SPAR_GP:
            {
              int eq_count, eq_ctr;
	      sprintf (buf, "GRAPH PATTERN:");
	      SES_PRINT (ses, buf);
	      spart_dump_long ((void *)(tree->_.gp.subtype), ses, -1);
	      spart_dump (tree->_.gp.members, ses, indent+2, "MEMBERS", -2);
	      spart_dump (tree->_.gp.filters, ses, indent+2, "FILTERS", -2);
	      spart_dump (tree->_.gp.selid, ses, indent+2, "SELECT ID", 0);
	      /* spart_dump (tree->_.gp.results, ses, indent+2, "RESULTS", -2); */
              session_buffered_write_char ('\n', ses);
	      for (ctr = indent+2; ctr--; /*no step*/ )
	        session_buffered_write_char (' ', ses);
	      sprintf (buf, "EQUIVS:");
	      SES_PRINT (ses, buf);
              eq_count = tree->_.gp.equiv_count;
	      for (eq_ctr = 0; eq_ctr < eq_count; eq_ctr++)
                {
	          sprintf (buf, " %d", (int)(tree->_.gp.equiv_indexes[eq_ctr]));
		  SES_PRINT (ses, buf);
                }
	      break;
	    }
	  case SPAR_LIT:
	    {
	      sprintf (buf, "LITERAL:");
	      SES_PRINT (ses, buf);
	      spart_dump (tree->_.lit.val, ses, indent+2, "VALUE", 0);
              if (tree->_.lit.datatype)
	        spart_dump (tree->_.lit.datatype, ses, indent+2, "DATATYPE", 0);
              if (tree->_.lit.language)
	        spart_dump (tree->_.lit.language, ses, indent+2, "LANGUAGE", 0);
	      break;
	    }
	  case SPAR_QNAME:
	    {
	      sprintf (buf, "QNAME:");
	      SES_PRINT (ses, buf);
	      spart_dump (tree->_.lit.val, ses, indent+2, "IRI", 0);
	      break;
	    }
	  /*case SPAR_QNAME_NS:
	    {
	      sprintf (buf, "QNAME_NS:");
	      SES_PRINT (ses, buf);
	      spart_dump (tree->_.lit.val, ses, indent+2, "NAMESPACE", 0);
	      break;
	    }*/
	  case SPAR_REQ_TOP:
	    {
#if 0
              int eq_count, eq_ctr;
#endif
	      sprintf (buf, "REQUEST TOP NODE (");
	      SES_PRINT (ses, buf);
	      spart_dump_long ((void *)(tree->_.req_top.subtype), ses, 1);
	      SES_PRINT (ses, "):");
              if (NULL != tree->_.req_top.retvalmode_name)
	        spart_dump (tree->_.req_top.retvalmode_name, ses, indent+2, "VALMODE FOR RETVALS", 0);
              if (NULL != tree->_.req_top.formatmode_name)
	        spart_dump (tree->_.req_top.formatmode_name, ses, indent+2, "SERIALIZATION FORMAT", 0);
              if (NULL != tree->_.req_top.storage_name)
	        spart_dump (tree->_.req_top.storage_name, ses, indent+2, "RDF DATA STORAGE", 0);
	      if (IS_BOX_POINTER(tree->_.req_top.retvals))
	        spart_dump (tree->_.req_top.retvals, ses, indent+2, "RETVALS", -2);
	      else
	        spart_dump (tree->_.req_top.retvals, ses, indent+2, "RETVALS", 0);
	      spart_dump (tree->_.req_top.retselid, ses, indent+2, "RETVALS SELECT ID", 0);
	      spart_dump (tree->_.req_top.sources, ses, indent+2, "SOURCES", -2);
	      spart_dump (tree->_.req_top.pattern, ses, indent+2, "PATTERN", -1);
	      spart_dump (tree->_.req_top.order, ses, indent+2, "ORDER", -1);
	      spart_dump ((void *)(tree->_.req_top.limit), ses, indent+2, "LIMIT", 0);
	      spart_dump ((void *)(tree->_.req_top.offset), ses, indent+2, "OFFSET", 0);
#if 0
	      sprintf (buf, "\nEQUIVS:");
	      SES_PRINT (ses, buf);
              eq_count = tree->_.req_top.equiv_count;
	      for (eq_ctr = 0; eq_ctr < eq_count; eq_ctr++)
                {
		  sparp_equiv_t *eq = tree->_.req_top.equivs[eq_ctr];
                  int varname_count, varname_ctr;
                  int var_ctr;
		  session_buffered_write_char ('\n', ses);
		  for (ctr = indent+2; ctr--; /*no step*/ )
		    session_buffered_write_char (' ', ses);
                  if (NULL == eq)
                    {
	              sprintf (buf, "#%d: merged and destroyed", eq_ctr);
	              SES_PRINT (ses, buf);
                      continue;
                    }
	          sprintf (buf, "#%d: %s( %d subv, %d recv, %d gspo, %d const, %d subq:", eq_ctr,
                    (eq->e_deprecated ? "deprecated " : ""),
		    BOX_ELEMENTS_INT_0(eq->e_subvalue_idxs), BOX_ELEMENTS_INT_0(eq->e_receiver_idxs),
	            (int)(eq->e_gspo_uses), (int)(eq->e_const_reads), (int)(eq->e_subquery_uses) );
	          SES_PRINT (ses, buf);
		  varname_count = BOX_ELEMENTS (eq->e_varnames);
		  for (varname_ctr = 0; varname_ctr < varname_count; varname_ctr++)
		    {
		      SES_PRINT (ses, " ");
		      SES_PRINT (ses, eq->e_varnames[varname_ctr]);
		    }
		  SES_PRINT (ses, " in");
		  for (var_ctr = 0; var_ctr < eq->e_var_count; var_ctr++)
		    {
                      SPART *var = eq->e_vars[var_ctr];
		      SES_PRINT (ses, " ");
		      SES_PRINT (ses, ((NULL != var->_.var.tabid) ? var->_.var.tabid : var->_.var.selid));
		    }
                  SES_PRINT (ses, ";"); spart_dump_rvr (ses, &(eq->e_rvr));
		  SES_PRINT (ses, ")");
                }
#endif
	      break;
	    }
	  case SPAR_VARIABLE:
	    {
	      sprintf (buf, "VARIABLE:");
	      SES_PRINT (ses, buf);
              spart_dump_rvr (ses, &(tree->_.var.rvr));
              if (NULL != tree->_.var.tabid)
                {
                  static const char *field_full_names[] = {"graph", "subject", "predicate", "object"};
                  sprintf (buf, " (%s)", field_full_names[tree->_.var.tr_idx]); SES_PRINT (ses, buf);
                }
	      spart_dump (tree->_.var.vname, ses, indent+2, "NAME", 0);
	      spart_dump (tree->_.var.selid, ses, indent+2, "SELECT ID", 0);
	      spart_dump (tree->_.var.tabid, ses, indent+2, "TABLE ID", 0);
	      spart_dump ((void*)(tree->_.var.equiv_idx), ses, indent+2, "EQUIV", 0);
	      break;
	    }
	  case SPAR_TRIPLE:
	    {
	      sprintf (buf, "TRIPLE:");
	      SES_PRINT (ses, buf);
	      if (tree->_.triple.ft_type)
                {
	          sprintf (buf, " ft predicate %d", (int)(tree->_.triple.ft_type));
	          SES_PRINT (ses, buf);
                }
              if (NULL != tree->_.triple.options)
                spart_dump (tree->_.triple.options, ses, indent+2, "OPTIONS", -2);
	      spart_dump (tree->_.triple.tr_graph, ses, indent+2, "GRAPH", -1);
	      spart_dump (tree->_.triple.tr_subject, ses, indent+2, "SUBJECT", -1);
	      spart_dump (tree->_.triple.tr_predicate, ses, indent+2, "PREDICATE", -1);
	      spart_dump (tree->_.triple.tr_object, ses, indent+2, "OBJECT", -1);
	      spart_dump (tree->_.triple.selid, ses, indent+2, "SELECT ID", 0);
	      spart_dump (tree->_.triple.tabid, ses, indent+2, "TABLE ID", 0);
	      break;
	    }
	  case BOP_EQ: case BOP_NEQ:
	  case BOP_LT: case BOP_LTE: case BOP_GT: case BOP_GTE:
	  /*case BOP_LIKE: Like is built-in in SPARQL, not a BOP! */
	  case BOP_SAME: case BOP_NSAME:
	  case BOP_PLUS: case BOP_MINUS: case BOP_TIMES: case BOP_DIV: case BOP_MOD:
	  case BOP_AND: case BOP_OR: case BOP_NOT:
	    {
	      sprintf (buf, "OPERATOR EXPRESSION ("/*, tree->type*/);
	      SES_PRINT (ses, buf);
	      spart_dump_long ((void *)(tree->type), ses, 1);
	      SES_PRINT (ses, "):");
	      spart_dump (tree->_.bin_exp.left, ses, indent+2, "LEFT", -1);
	      spart_dump (tree->_.bin_exp.right, ses, indent+2, "RIGHT", -1);
	      break;
	    }
          case ORDER_L:
            {
	      sprintf (buf, "ORDERING ("/*, tree->_.oby.direction*/);
	      SES_PRINT (ses, buf);
	      spart_dump_long ((void *)(tree->_.oby.direction), ses, 1);
	      SES_PRINT (ses, "):");
	      spart_dump (tree->_.oby.expn, ses, indent+2, "CRITERION", -1);
	      break;
            }
	  case FROM_L:
	    {
	      sprintf (buf, "FROM (default):");
	      SES_PRINT (ses, buf);
	      spart_dump (tree->_.lit.val, ses, indent+2, "IRI", 0);
	      break;
	    }
	  case NAMED_L:
	    {
	      sprintf (buf, "FROM NAMED:");
	      SES_PRINT (ses, buf);
	      spart_dump (tree->_.lit.val, ses, indent+2, "IRI", 0);
	      break;
	    }
	  default:
	    {
	      sprintf (buf, "NODE OF TYPE %ld (", (ptrlong)(tree->type));
	      SES_PRINT (ses, buf);
	      spart_dump_long ((void *)(tree->type), ses, 0);
	      sprintf (buf, ") with %d children:\n", childrens-SPART_HEAD);
	      SES_PRINT (ses, buf);
	      for (ctr = SPART_HEAD; ctr < childrens; ctr++)
		spart_dump (((void **)(tree))[ctr], ses, indent+2, NULL, 0);
	      break;
	    }
	  }
	break;
      }
    case DV_ARRAY_OF_POINTER:
      {
	int childrens = BOX_ELEMENTS (tree);
	char buf[50];
	sprintf (buf, "ARRAY with %d children: {", childrens);
	SES_PRINT (ses,	buf);
	for (ctr = 0; ctr < childrens; ctr++)
	  spart_dump (((void **)(tree))[ctr], ses, indent+2, NULL, 0);
	if (indent > 0)
	  {
	    session_buffered_write_char ('\n', ses);
	    for (ctr = indent; ctr--; /*no step*/ )
	      session_buffered_write_char (' ', ses);
	  }
	SES_PRINT (ses,	" }");
	break;
      }
    case -2:
      {
	int childrens = BOX_ELEMENTS (tree);
	char buf[50];
	if (0 == childrens)
	  {
	    SES_PRINT (ses, "EMPTY ARRAY");
	    break;
	  }
	sprintf (buf, "ARRAY OF NODES with %d children: {", childrens);
	SES_PRINT (ses,	buf);
	for (ctr = 0; ctr < childrens; ctr++)
	  spart_dump (((void **)(tree))[ctr], ses, indent+2, NULL, -1);
	if (indent > 0)
	  {
	    session_buffered_write_char ('\n', ses);
	    for (ctr = indent; ctr--; /*no step*/ )
	    session_buffered_write_char (' ', ses);
	  }
	SES_PRINT (ses,	" }");
	break;
      }
#if 0
    case -3:
      {
	char **execname = (char **)id_hash_get (xpf_reveng, (caddr_t)(&tree));
	SES_PRINT (ses, "native code started at ");
	if (NULL == execname)
	  {
	    char buf[30];
	    sprintf (buf, "0x%p", (void *)tree);
	    SES_PRINT (ses, buf);
	  }
	else
	  {
	    SES_PRINT (ses, "label '");
	    SES_PRINT (ses, execname[0]);
	    SES_PRINT (ses, "'");
	  }
	break;
      }
#endif
    case DV_LONG_INT:
      {
	char buf[30];
	sprintf (buf, "LONG %ld", (long)(unbox ((ccaddr_t)tree)));
	SES_PRINT (ses,	buf);
	break;
      }
    case DV_STRING:
      {
	SES_PRINT (ses,	"STRING `");
	SES_PRINT (ses,	(char *)(tree));
	SES_PRINT (ses,	"'");
	break;
      }
    case DV_UNAME:
      {
	SES_PRINT (ses,	"UNAME `");
	SES_PRINT (ses,	(char *)(tree));
	SES_PRINT (ses,	"'");
	break;
      }
    case DV_SYMBOL:
      {
	SES_PRINT (ses,	"SYMBOL `");
	SES_PRINT (ses,	(char *)(tree));
	SES_PRINT (ses,	"'");
	break;
      }
    case DV_NUMERIC:
      {
        numeric_t n = (numeric_t)(tree);
        char buf[0x100];
	SES_PRINT (ses,	"NUMERIC ");
        numeric_to_string (n, buf, 0x100);
	SES_PRINT (ses,	buf);
      }
    default:
      {
	char buf[30];
	sprintf (buf, "UNEXPECTED TYPE (%u)", (unsigned)(DV_TYPE_OF (tree)));
	SES_PRINT (ses,	buf);
	break;
      }
    }
printed:
  if (0 == indent)
    session_buffered_write_char ('\n', ses);
}

#ifdef DEBUG
sparp_t * dbg_curr_sparp;
#endif

sparp_t * sparp_query_parse (char * str, spar_query_env_t *sparqre)
{
  wcharset_t *query_charset = sparqre->sparqre_query_charset;
  t_NEW_VAR (sparp_t, sparp);
  t_NEW_VARZ (sparp_env_t, spare);
#ifdef DEBUG
  dbg_curr_sparp = sparp;
#endif
  memset (sparp, 0, sizeof (sparp_t));
  sparp->sparp_sparqre = sparqre;
  if ((NULL == sparqre->sparqre_cli) && (NULL != sparqre->sparqre_qi))
    sparqre->sparqre_cli = sparqre->sparqre_qi->qi_client;
  sparp->sparp_env = spare;
  sparp->sparp_err_hdr = t_box_dv_short_string ("SPARQL compiler");
  if ((NULL == query_charset) /*&& (!sparqre->xqre_query_charset_is_set)*/)
    {
      if (NULL != sparqre->sparqre_qi)
        query_charset = QST_CHARSET (sparqre->sparqre_qi);
      if (NULL == query_charset)
        query_charset = default_charset;
    }
  if (NULL == query_charset)
    sparp->sparp_enc = &eh__ISO8859_1;
  else
    {
      sparp->sparp_enc = eh_get_handler (CHARSET_NAME (query_charset, NULL));
      if (NULL == sparp->sparp_enc)
      sparp->sparp_enc = &eh__ISO8859_1;
    }
  sparp->sparp_lang = server_default_lh;
  spare->spare_namespace_prefixes_outer = 
    spare->spare_namespace_prefixes =
      sparqre->sparqre_external_namespaces;

  sparp->sparp_text = str;
  spar_fill_lexem_bufs (sparp);
  if (NULL != sparp->sparp_sparqre->sparqre_catched_error)
    return sparp;
  QR_RESET_CTX
    {
      /* Bug 4566: sparpyyrestart (NULL); */
      sparyyparse (sparp);
      sparp_rewrite_all (sparp, 0 /* top is never cloned, hence zero safely_copy_retvals */);
    }
  QR_RESET_CODE
    {
      du_thread_t *self = THREAD_CURRENT_THREAD;
      sparp->sparp_sparqre->sparqre_catched_error = thr_get_error_code (self);
      thr_set_error_code (self, NULL);
      POP_QR_RESET;
      return sparp; /* see below */
    }
  END_QR_RESET
  /*xt_check (sparp, sparp->sparp_expr);*/
#ifndef NDEBUG
  t_check_tree (sparp->sparp_expr);
#endif
  return sparp;
}

#define ENV_COPY(field) env_copy->field = env->field
#define ENV_BOX_COPY(field) env_copy->field = t_box_copy (env->field)
#define ENV_SPART_COPY(field) env_copy->field = (SPART *)t_box_copy_tree ((caddr_t)(env->field))
#define ENV_SET_COPY(field) \
  env_copy->field = t_set_copy (env->field); \
  DO_SET_WRITABLE (SPART *, opt, iter, &(env_copy->field)) \
    { \
      iter->data = t_box_copy_tree ((caddr_t)opt); \
    } \
  END_DO_SET()

sparp_t *
sparp_clone_for_variant (sparp_t *sparp)
{
  s_node_t *iter;
  sparp_env_t *env = sparp->sparp_env;
  t_NEW_VAR (sparp_t, sparp_copy);
  t_NEW_VARZ (sparp_env_t, env_copy);
  memcpy (sparp_copy, sparp, sizeof (sparp_t));
  sparp_copy->sparp_env = env_copy;
  ENV_BOX_COPY (spare_output_valmode_name);
  ENV_BOX_COPY (spare_output_format_name);
  ENV_BOX_COPY (spare_storage_name);
  ENV_COPY(spare_parent_env);
#if 0 /* These will be used when libraries of inference rules are introduced. */
    id_hash_t *		spare_fundefs;			/*!< In-scope function definitions */
    id_hash_t *		spare_vars;			/*!< Known variables as keys, equivs as values */
    id_hash_t *		spare_global_bindings;		/*!< Dictionary of global bindings, varnames as keys, default value expns as values. DV_DB_NULL box for no expn! */
#endif
  /* No copy for spare_grab_vars */
  ENV_SET_COPY (spare_common_sponge_options);
  ENV_SET_COPY (spare_default_graph_precodes);
  ENV_SET_COPY (spare_named_graph_precodes);
  ENV_COPY (spare_default_graphs_locked);
  ENV_COPY (spare_named_graphs_locked);
  ENV_SET_COPY (spare_common_sql_table_options);
  ENV_SET_COPY (spare_sql_select_options);
  ENV_SET_COPY (spare_global_var_names);
  return sparp_copy;
}

void
spar_env_push (sparp_t *sparp)
{
  sparp_env_t *env = sparp->sparp_env;
  t_NEW_VARZ (sparp_env_t, env_copy);
  ENV_COPY (spare_start_lineno);
  ENV_COPY (spare_param_counter_ptr);
  ENV_COPY (spare_namespace_prefixes);
  ENV_COPY (spare_namespace_prefixes_outer);
  ENV_COPY (spare_base_uri);
  env_copy->spare_output_valmode_name = t_box_dv_short_string ("AUTO");
  ENV_COPY (spare_storage_name);
  ENV_COPY (spare_inference_name);
  ENV_COPY (spare_use_same_as);
#if 0 /* These will be used when libraries of inference rules are introduced. Don't forget to patch sparp_clone_for_variant()! */
    id_hash_t *		spare_fundefs;			/*!< In-scope function definitions */
    id_hash_t *		spare_vars;			/*!< Known variables as keys, equivs as values */
    id_hash_t *		spare_global_bindings;		/*!< Dictionary of global bindings, varnames as keys, default value expns as values. DV_DB_NULL box for no expn! */
#endif
  ENV_COPY (spare_grab);
  ENV_COPY (spare_common_sponge_options);
  ENV_COPY (spare_default_graph_precodes);
  ENV_COPY (spare_default_graphs_locked);
  ENV_COPY (spare_named_graph_precodes);
  ENV_COPY (spare_named_graphs_locked);
  ENV_COPY (spare_common_sql_table_options);
  /* no copy for spare_groupings */
  ENV_COPY (spare_sql_select_options);
  /* no copy for spare_context_qms */
  ENV_COPY (spare_context_qms);
  if ((NULL != env_copy->spare_context_qms) && ((SPART *)((ptrlong)_STAR) != env_copy->spare_context_qms->data))
    spar_error (sparp, "Subqueries are not allowed inside QUAD MAP group patterns other than 'QUAD MAP * {...}'");
  ENV_COPY (spare_context_graphs);
  /* no copy for spare_context_subjects */
  /* no copy for spare_context_predicates */
  /* no copy for spare_context_objects */
  /* no copy for spare_context_gp_subtypes */
  /* no copy for spare_acc_req_triples */
  /* no copy for spare_acc_opt_triples */
  /* no copy for spare_acc_filters */
  ENV_COPY (spare_good_graph_varnames);
  ENV_COPY (spare_good_graph_varname_sets);
  ENV_COPY (spare_good_graph_bmk);
  /* no copy for spare_selids */
  ENV_COPY (spare_global_var_names);
  ENV_COPY (spare_globals_are_numbered);
  ENV_COPY (spare_global_num_offset);
  /* no copy for spare_acc_qm_sqls */
  /* no copy for spare_qm_default_table */
  /* no copy for spare_qm_current_table_alias */
  /* no copy for spare_qm_parent_tables_of_aliases */
  /* no copy for spare_qm_parent_aliases_of_aliases */
  /* no copy for spare_qm_descendants_of_aliases */
  /* no copy for spare_qm_ft_indexes_of_columns */
  /* no copy for spare_qm_where_conditions */
  /* no copy for spare_qm_locals */
  /* no copy for spare_qm_affected_jso_iris */
  /* no copy for spare_qm_deleted */
  /* no copy for spare_sparul_log_mode */
  ENV_COPY (spare_signal_void_variables);

  env_copy->spare_parent_env = env;
  sparp->sparp_env = env_copy;
}

#undef ENV_COPY
#undef ENV_BOX_COPY
#undef ENV_SPART_COPY
#undef ENV_SET_COPY

void
spar_env_pop (sparp_t *sparp)
{
  sparp_env_t *env = sparp->sparp_env;
  sparp_env_t *parent = env->spare_parent_env;
#ifndef NDEBUG
  if (NULL == parent)
    GPF_T1("Misplaced call of spar_env_pop()");
#endif
  parent->spare_global_var_names = env->spare_global_var_names;
  sparp->sparp_env = parent;
}

extern void sparp_delete_clone (sparp_t *sparp);


void
sparp_compile_subselect (spar_query_env_t *sparqre)
{
  sparp_t * sparp;
  query_t * qr = NULL; /*dummy for CC_INIT */
  spar_sqlgen_t ssg;
  comp_context_t cc;
  sql_comp_t sc;
  caddr_t str = strses_string (sparqre->sparqre_src->sif_skipped_part);
  caddr_t res;
#ifdef SPARQL_DEBUG
  printf ("\nsparp_compile_subselect() input:\n%s", str);
#endif
  strses_free (sparqre->sparqre_src->sif_skipped_part);
  sparqre->sparqre_src->sif_skipped_part = NULL;
  sparqre->sparqre_cli = sqlc_client();
  sparp = sparp_query_parse (str, sparqre);
  dk_free_box (str);
  if (NULL != sparp->sparp_sparqre->sparqre_catched_error)
    {
#ifdef SPARQL_DEBUG
      printf ("\nsparp_compile_subselect() caught parse error: %s", ERR_MESSAGE(sparp->sparp_sparqre->sparqre_catched_error));
#endif
    return;
    }
  memset (&ssg, 0, sizeof (spar_sqlgen_t));
  memset (&sc, 0, sizeof (sql_comp_t));
  CC_INIT (cc, ((NULL != sparqre->sparqre_super_sc) ? sparqre->sparqre_super_sc->sc_client : sqlc_client()));
  sc.sc_cc = &cc;
  if (NULL != sparqre->sparqre_super_sc)
    {
      cc.cc_super_cc = sparqre->sparqre_super_sc->sc_cc->cc_super_cc;
      sc.sc_super = sparqre->sparqre_super_sc;
    }
  ssg.ssg_out = strses_allocate ();
  ssg.ssg_sc = &sc;
  ssg.ssg_sparp = sparp;
  ssg.ssg_tree = sparp->sparp_expr;
  ssg_make_whole_sql_text (&ssg);
  if (NULL != sparqre->sparqre_catched_error)
        {
      strses_free (ssg.ssg_out);
      ssg.ssg_out = NULL;
      return;
    }
  session_buffered_write (ssg.ssg_out, sparqre->sparqre_tail_sql_text, strlen (sparqre->sparqre_tail_sql_text));
  session_buffered_write_char (0 /*YY_END_OF_BUFFER_CHAR*/, ssg.ssg_out); /* First terminator */
  session_buffered_write_char (0 /*YY_END_OF_BUFFER_CHAR*/, ssg.ssg_out); /* Second terminator. Most of Lex-es need two! */
  res = strses_string (ssg.ssg_out);
#ifdef SPARQL_DEBUG
  printf ("\nsparp_compile_subselect() done: %s", res);
#endif
  strses_free (ssg.ssg_out);
  ssg.ssg_out = NULL;
  sparqre->sparqre_compiled_text = t_box_copy (res);
  dk_free_box (res);
}


caddr_t
bif_sparql_explain (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  int param_ctr = 0;
  spar_query_env_t sparqre;
  sparp_t * sparp;
  caddr_t str = bif_string_arg (qst, args, 0, "sparql_explain");
  dk_session_t *res;
  MP_START ();
  memset (&sparqre, 0, sizeof (spar_query_env_t));
  sparqre.sparqre_param_ctr = &param_ctr;
  sparqre.sparqre_qi = (query_instance_t *) qst;
  sparp = sparp_query_parse (str, &sparqre);
  if (NULL != sparqre.sparqre_catched_error)
    {
      MP_DONE ();
      sqlr_resignal (sparqre.sparqre_catched_error);
    }
  res = strses_allocate ();
  spart_dump (sparp->sparp_expr, res, 0, "QUERY", -1);
  MP_DONE ();
  return (caddr_t)res;
}


caddr_t
bif_sparql_to_sql_text (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  int param_ctr = 0;
  spar_query_env_t sparqre;
  sparp_t * sparp;
  caddr_t str = bif_string_arg (qst, args, 0, "sparql_to_sql_text");
  /*caddr_t err = NULL;*/
  spar_sqlgen_t ssg;
  sql_comp_t sc;
  MP_START ();
  memset (&sparqre, 0, sizeof (spar_query_env_t));
  sparqre.sparqre_param_ctr = &param_ctr;
  sparqre.sparqre_qi = (query_instance_t *) qst;
  sparp = sparp_query_parse (str, &sparqre);
  if (NULL != sparqre.sparqre_catched_error)
    {
      MP_DONE ();
      sqlr_resignal (sparqre.sparqre_catched_error);
    }
  memset (&ssg, 0, sizeof (spar_sqlgen_t));
  memset (&sc, 0, sizeof (sql_comp_t));
  sc.sc_client = sparqre.sparqre_qi->qi_client;
  ssg.ssg_out = strses_allocate ();
  ssg.ssg_sc = &sc;
  ssg.ssg_sparp = sparp;
  ssg.ssg_tree = sparp->sparp_expr;
  ssg_make_whole_sql_text (&ssg);
  if (NULL != sparqre.sparqre_catched_error)
    {
      strses_free (ssg.ssg_out);
      ssg.ssg_out = NULL;
      MP_DONE ();
      sqlr_resignal (sparqre.sparqre_catched_error);
    }
  MP_DONE ();
  return (caddr_t)(ssg.ssg_out);
}


caddr_t
bif_sparql_lex_analyze (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  caddr_t str = bif_string_arg (qst, args, 0, "sparql_lex_analyze");
  return spar_query_lex_analyze (str, QST_CHARSET(qst));
}

extern caddr_t key_id_to_iri (query_instance_t * qi, iri_id_t iri_id_no);

SPART *spar_make_literal_from_sql_box (sparp_t * sparp, caddr_t box, int make_bnode_if_null)
{
  switch (DV_TYPE_OF (box))
    {
    case DV_LONG_INT: return spartlist (sparp, 4, SPAR_LIT, t_box_copy (box), uname_xmlschema_ns_uri_hash_integer, NULL);
    case DV_NUMERIC: return spartlist (sparp, 4, SPAR_LIT, t_box_copy (box), uname_xmlschema_ns_uri_hash_decimal, NULL);
    case DV_DOUBLE_FLOAT: return spartlist (sparp, 4, SPAR_LIT, t_box_copy (box), uname_xmlschema_ns_uri_hash_double, NULL);
    case DV_UNAME: return spartlist (sparp, 2, SPAR_QNAME, t_box_copy (box));
    case DV_IRI_ID:
      {
        iri_id_t iid = unbox_iri_id (box);
        caddr_t iri;
        SPART *res;
        if (0L == iid)
          return NULL;
        if (iid >= min_bnode_iri_id ())
          {
            caddr_t t_iri;
            if (iid >= MIN_64BIT_BNODE_IRI_ID)
              t_iri = t_box_sprintf (30, "nodeID://b" BOXINT_FMT, (boxint)(iid-MIN_64BIT_BNODE_IRI_ID));
            else
              t_iri = t_box_sprintf (30, "nodeID://" BOXINT_FMT, (boxint)(iid));
            return spartlist (sparp, 2, SPAR_QNAME, t_box_dv_uname_string (t_iri));
          }
        iri = key_id_to_iri (sparp->sparp_sparqre->sparqre_qi, iid);
        if (!iri)
          return NULL;
        res = spartlist (sparp, 2, SPAR_QNAME, t_box_dv_uname_string (iri));
        dk_free_box (iri);
        return res;
      }
    case DV_RDF:
      {
        rdf_box_t *rb = (rdf_box_t *)box;
        if (!rb->rb_is_complete ||
          (RDF_BOX_DEFAULT_TYPE != rb->rb_type) ||
          (RDF_BOX_DEFAULT_LANG != rb->rb_lang) ||
          DV_STRING != DV_TYPE_OF (rb->rb_box) )
          spar_internal_error (sparp, "spar_" "make_literal_from_sql_box() does not support rdf boxes other than complete untyped strings, sorry");
        return spartlist (sparp, 4, SPAR_LIT, t_box_copy (box), NULL, NULL);
      }
    case DV_STRING: spartlist (sparp, 4, SPAR_LIT, t_box_copy (box), NULL, NULL);
    case DV_DB_NULL:
      if (make_bnode_if_null)
        return spar_make_blank_node (sparp, spar_mkid (sparp, "_:sqlbox"), 1);
      else
        return spar_make_variable (sparp, t_box_dv_uname_string ("_:sqlbox"));
      break;
    default: spar_internal_error (sparp, "spar_" "make_literal_from_sql_box(): unsupportded box type");
  }
  return NULL; /* to keep compier happy */
}

#define QUAD_MAPS_FOR_QUAD 1
#define SQL_COLS_FOR_QUAD 2
caddr_t
bif_sparql_quad_maps_for_quad_impl (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args, int resulting_format, const char *fname)
{
  int param_ctr_for_sparqre = 0;
  spar_query_env_t sparqre;
  sparp_env_t spare;
  sparp_t sparp;
  spar_sqlgen_t ssg;
  sql_comp_t sc;
  caddr_t sqlvals[SPART_TRIPLE_FIELDS_COUNT];
  caddr_t *sources_val = NULL;
  caddr_t storage_name = NULL;
  caddr_t *res = NULL;
  boxint flags = 0;
  switch (BOX_ELEMENTS (args))
    {
    case 7: flags = bif_long_arg (qst, args, 6, fname);
    case 6: sources_val = bif_strict_2type_array_arg (DV_UNAME, DV_IRI_ID, qst, args, 5, fname);
    case 5: storage_name = bif_string_or_uname_or_wide_or_null_arg (qst, args, 4, fname);
    case 4: sqlvals[SPART_TRIPLE_OBJECT_IDX]	= bif_arg (qst, args, 3, fname);
    case 3: sqlvals[SPART_TRIPLE_PREDICATE_IDX]	= bif_arg (qst, args, 2, fname);
    case 2: sqlvals[SPART_TRIPLE_SUBJECT_IDX]	= bif_arg (qst, args, 1, fname);
    case 1: sqlvals[SPART_TRIPLE_GRAPH_IDX]	= bif_arg (qst, args, 0, fname);
    case 0: ; /* no break */
    }
  MP_START ();
  memset (&sparqre, 0, sizeof (spar_query_env_t));
  memset (&spare, 0, sizeof (sparp_env_t));
  memset (&sparp, 0, sizeof (sparp_t));
  sparqre.sparqre_param_ctr = &param_ctr_for_sparqre;
  sparqre.sparqre_qi = (query_instance_t *) qst;
  sparp.sparp_sparqre = &sparqre;
  sparp.sparp_env = &spare;
  memset (&ssg, 0, sizeof (spar_sqlgen_t));
  memset (&sc, 0, sizeof (sql_comp_t));
  sc.sc_client = sparqre.sparqre_qi->qi_client;
  ssg.ssg_sc = &sc;
  ssg.ssg_sparp = &sparp;
  QR_RESET_CTX
    {
      SPART **sources = (SPART **)t_alloc_box (BOX_ELEMENTS_0 (sources_val) * sizeof (SPART *), DV_ARRAY_OF_POINTER);
      SPART *triple;
      SPART *fake_global_sql_param = NULL;
      SPART *fake_global_long_param = NULL;
      triple_case_t **cases;
      int src_idx, case_count, case_ctr;
      if (DV_STRING == DV_TYPE_OF (storage_name))
        storage_name = t_box_dv_uname_string (storage_name);
      sparp.sparp_storage = sparp_find_storage_by_name (storage_name);
      t_set_push (&(spare.spare_selids), t_box_dv_short_string (fname));
      triple = spar_make_plain_triple (&sparp,
        spar_make_literal_from_sql_box (&sparp, sqlvals[SPART_TRIPLE_GRAPH_IDX]		, (int)(flags & 1)),
        spar_make_literal_from_sql_box (&sparp, sqlvals[SPART_TRIPLE_SUBJECT_IDX]	, (int)(flags & 1)),
        spar_make_literal_from_sql_box (&sparp, sqlvals[SPART_TRIPLE_PREDICATE_IDX]	, (int)(flags & 1)),
        spar_make_literal_from_sql_box (&sparp, sqlvals[SPART_TRIPLE_OBJECT_IDX]	, (int)(flags & 1)),
        (caddr_t)(_STAR), NULL );
      DO_BOX_FAST (caddr_t, itm, src_idx, sources_val)
        {
          sources [src_idx] = spartlist (&sparp, 2, FROM_L, 
            spar_make_literal_from_sql_box (&sparp, itm, 0) );
        }
      END_DO_BOX_FAST;
      cases = sparp_find_triple_cases (&sparp, triple, sources, FROM_L);
      case_count = BOX_ELEMENTS (cases);
      switch (resulting_format)
        {
        case QUAD_MAPS_FOR_QUAD:
          res = dk_alloc_list (case_count);
          for (case_ctr = case_count; case_ctr--; /* no step */)
            {
              triple_case_t *tc = cases [case_ctr];
              jso_rtti_t *sub = gethash (tc->tc_qm, jso_rttis_of_structs);
              caddr_t qm_name;
              if (NULL == sub)
                spar_internal_error (&sparp, "corrupted metadata: PointerToCorrupted");
              else if (sub->jrtti_self != tc->tc_qm)
                spar_internal_error (&sparp, "corrupted metadata: PointerToStaleDeleted");
              qm_name = box_copy (((jso_rtti_t *)(sub))->jrtti_inst_iri);
              res [case_ctr] = list (2, qm_name, tc->tc_qm->qmMatchingFlags);
            }
          break;
        case SQL_COLS_FOR_QUAD:
          {
            dk_set_t acc_maps = NULL;
            for (case_ctr = case_count; case_ctr--; /* no step */)
              {
                triple_case_t *tc = cases [case_ctr];
                int fld_ctr;
                int unique_bij_fld = -1;
                for (fld_ctr = SPART_TRIPLE_FIELDS_COUNT; fld_ctr--; /* no step */)
                  {
                    int col_ctr, col_count;
                    int alias_ctr, alias_count;
                    caddr_t sqlval = sqlvals [fld_ctr];
                    dtp_t sqlval_dtp = DV_TYPE_OF (sqlval);
                    caddr_t **col_descs;
                    caddr_t map_desc;
                    SPART *param;
                    ccaddr_t tmpl;
                    caddr_t expn_text;
                    qm_value_t *qmv;
                    if (DV_DB_NULL == sqlval_dtp)
                      continue;
                    qmv = SPARP_FIELD_QMV_OF_QM (tc->tc_qm, fld_ctr);
                    if ((NULL == qmv) || (!qmv->qmvFormat->qmfIsBijection && !qmv->qmvFormat->qmfDerefFlags))
                      continue;
                    if (qmv->qmvColumnsFormKey)
                      unique_bij_fld = fld_ctr;
                    col_count = BOX_ELEMENTS (qmv->qmvColumns);
                    col_descs = (caddr_t **)dk_alloc_list (col_count);
                    alias_count = BOX_ELEMENTS_0 (qmv->qmvATables);
                    for (col_ctr = col_count; col_ctr--; /* no step */)
                      {
                        qm_column_t *qmc = qmv->qmvColumns[col_ctr];
                        caddr_t *col_desc;
                        ccaddr_t table_alias = qmc->qmvcAlias;
                        ccaddr_t table_name = qmv->qmvTableName;
                        for (alias_ctr = alias_count; alias_ctr--; /* no step */)
                          {
                            if (strcmp (qmv->qmvATables[alias_ctr]->qmvaAlias, table_alias))
                              continue;
                            table_name = qmv->qmvATables[alias_ctr]->qmvaTableName;
                            break;
                          }
                        col_desc = (caddr_t *)list (4, box_copy (table_alias), box_copy (table_name), box_copy (qmc->qmvcColumnName), NULL);
                        col_descs[col_ctr] = col_desc;
                      }
                    switch (sqlval_dtp)
                      {
                      case DV_UNAME:
                        if (NULL == fake_global_sql_param)
                          fake_global_sql_param = spar_make_variable (&sparp, t_box_dv_uname_string (":0"));
                        param = fake_global_sql_param;
                        tmpl = qmv->qmvFormat->qmfShortOfUriTmpl;
                        break;
                      case DV_IRI_ID: case DV_RDF:
                        if (NULL == fake_global_sql_param)
                          fake_global_long_param = spar_make_variable (&sparp, t_box_dv_uname_string (":LONG::0"));
                        param = fake_global_long_param;
                        tmpl = qmv->qmvFormat->qmfShortOfLongTmpl; break;
                      default:
                        if (NULL == fake_global_sql_param)
                          fake_global_sql_param = spar_make_variable (&sparp, t_box_dv_uname_string (":0"));
                        param = fake_global_sql_param;
                        tmpl = qmv->qmvFormat->qmfShortOfSqlvalTmpl;
                        break;
                      }
                    if (NULL == ssg.ssg_out)
                      ssg.ssg_out = strses_allocate ();
                    session_buffered_write (ssg.ssg_out, "vector (", strlen ("vector ("));
                    ssg_print_tmpl (&ssg, qmv->qmvFormat, tmpl, NULL, NULL, param, NULL_ASNAME);
                    session_buffered_write (ssg.ssg_out, ")", strlen (")"));
                    expn_text = strses_string (ssg.ssg_out);
                    strses_flush (ssg.ssg_out);
                    map_desc = list (4,
                      box_num (fld_ctr),
                      box_num (qmv->qmvColumnsFormKey),
                      col_descs,
                      expn_text );
                    dk_set_push (&acc_maps, map_desc);
                  }
              }
            res = (caddr_t *)revlist_to_array (acc_maps);
          }
      }
    }
  QR_RESET_CODE
    {
      du_thread_t *self = THREAD_CURRENT_THREAD;
      sparqre.sparqre_catched_error = thr_get_error_code (self);
      thr_set_error_code (self, NULL);
      POP_QR_RESET;
      MP_DONE ();
      sqlr_resignal (sparqre.sparqre_catched_error);
    }
  END_QR_RESET
  MP_DONE ();
  return (caddr_t)res;
}

caddr_t
bif_sparql_quad_maps_for_quad (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  return bif_sparql_quad_maps_for_quad_impl (qst, err_ret, args, QUAD_MAPS_FOR_QUAD, "sparql_quad_maps_for_quad");
}

caddr_t
bif_sparql_sql_cols_for_quad (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  return bif_sparql_quad_maps_for_quad_impl (qst, err_ret, args, SQL_COLS_FOR_QUAD, "sparql_sql_cols_for_quad");
}

caddr_t
bif_sprintff_is_proven_bijection (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  caddr_t f = bif_string_or_uname_arg (qst, args, 0, "__sprintff_is_proven_bijection");
  return box_num (sprintff_is_proven_bijection (f));
}

caddr_t
bif_sprintff_intersect (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  caddr_t f1 = bif_string_or_uname_arg (qst, args, 0, "__sprintff_intersect");
  caddr_t f2 = bif_string_or_uname_arg (qst, args, 1, "__sprintff_intersect");
  boxint ignore_cache = bif_long_arg (qst, args, 2, "__sprintff_intersect");
  caddr_t res;
  sec_check_dba ((query_instance_t *)qst, "__sprintff_intersect"); /* To prevent attack by intersecting garbage in order to run out of memory. */
  res = (caddr_t)sprintff_intersect (f1, f2, ignore_cache);
  if (NULL == res)
    return NEW_DB_NULL;
  return (ignore_cache ? res : box_copy (res));
}

caddr_t
bif_sprintff_like (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  caddr_t f1 = bif_string_or_uname_arg (qst, args, 0, "__sprintff_like");
  caddr_t f2 = bif_string_or_uname_arg (qst, args, 1, "__sprintff_like");
  sec_check_dba ((query_instance_t *)qst, "__sprintff_like"); /* To prevent attack by likeing garbage in order to run out of memory. */
  return box_num (sprintff_like (f1, f2));
}

#ifdef DEBUG

typedef struct spar_lexem_descr_s
{
  int ld_val;
  const char *ld_yname;
  char ld_fmttype;
  const char * ld_fmt;
  caddr_t *ld_tests;
} spar_lexem_descr_t;

spar_lexem_descr_t spar_lexem_descrs[__SPAR_NONPUNCT_END+1];

#define LEX_PROPS spar_lex_props
#define PUNCT(x) 'P', (x)
#define LITERAL(x) 'L', (x)
#define FAKE(x) 'F', (x)
#define SPAR "s"

#define LAST(x) "L", (x)
#define LAST1(x) "K", (x)
#define MISS(x) "M", (x)
#define ERR(x)  "E", (x)

#define PUNCT_SPAR_LAST(x) PUNCT(x), SPAR, LAST(x)


static void spar_lex_props (int val, const char *yname, char fmttype, const char *fmt, ...)
{
  va_list tail;
  const char *cmd;
  dk_set_t tests = NULL;
  spar_lexem_descr_t *ld = spar_lexem_descrs + val;
  if (0 != ld->ld_val)
    GPF_T;
  ld->ld_val = val;
  ld->ld_yname = yname;
  ld->ld_fmttype = fmttype;
  ld->ld_fmt = fmt;
  va_start (tail, fmt);
  for (;;)
    {
      cmd = va_arg (tail, const char *);
      if (NULL == cmd)
	break;
      dk_set_push (&tests, box_dv_short_string (cmd));
    }
  va_end (tail);
  ld->ld_tests = (caddr_t *)revlist_to_array (tests);
}

static void spar_lexem_descrs_fill (void)
{
  static int first_run = 1;
  if (!first_run)
    return;
  first_run = 0;
  #include "sparql_lex_props.c"
}

caddr_t
bif_sparql_lex_test (caddr_t * qst, caddr_t * err_ret, state_slot_t ** args)
{
  dk_set_t report = NULL;
  int tested_lex_val = 0;
  spar_lexem_descrs_fill ();
  for (tested_lex_val = 0; tested_lex_val < __SPAR_NONPUNCT_END; tested_lex_val++)
    {
      char cmd;
      caddr_t **lexems;
      unsigned lex_count;
      unsigned cmd_idx = 0;
      int last_lval, last1_lval;
      spar_lexem_descr_t *ld = spar_lexem_descrs + tested_lex_val;
      if (0 == ld->ld_val)
	continue;
      dk_set_push (&report, box_dv_short_string (""));
      dk_set_push (&report,
        box_sprintf (0x100, "#define % 25s %d /* '%s' (%c) */",
	  ld->ld_yname, ld->ld_val, ld->ld_fmt, ld->ld_fmttype ) );
      for (cmd_idx = 0; cmd_idx < BOX_ELEMENTS(ld->ld_tests); cmd_idx++)
	{
	  cmd = ld->ld_tests[cmd_idx][0];
	  switch (cmd)
	    {
	    case 's': break;	/* Fake, SPARQL has only one mode */
	    case 'K': case 'L': case 'M': case 'E':
	      cmd_idx++;
	      lexems = (caddr_t **) spar_query_lex_analyze (ld->ld_tests[cmd_idx], QST_CHARSET(qst));
	      dk_set_push (&report, box_dv_short_string (ld->ld_tests[cmd_idx]));
	      lex_count = BOX_ELEMENTS (lexems);
	      if (0 == lex_count)
		{
		  dk_set_push (&report, box_dv_short_string ("FAILED: no lexems parsed and no error reported!"));
		  goto end_of_test;
		}
	      { char buf[0x1000]; char *buf_tail = buf;
	        unsigned lctr = 0;
		for (lctr = 0; lctr < lex_count && (5 == BOX_ELEMENTS(lexems[lctr])); lctr++)
		  {
		    ptrlong *ldata = ((ptrlong *)(lexems[lctr]));
		    int lval = ldata[3];
		    spar_lexem_descr_t *ld = spar_lexem_descrs + lval;
		    if (ld->ld_val)
		      buf_tail += sprintf (buf_tail, " %s", ld->ld_yname);
		    else if (lval < 0x100)
		      buf_tail += sprintf (buf_tail, " '%c'", lval);
		    else GPF_T;
		    buf_tail += sprintf (buf_tail, " %ld ", (long)(ldata[4]));
		  }
	        buf_tail[0] = '\0';
		dk_set_push (&report, box_dv_short_string (buf));
	      }
	      if (3 == BOX_ELEMENTS(lexems[lex_count-1])) /* lexical error */
		{
		  dk_set_push (&report,
		    box_sprintf (0x1000, "%s: ERROR %s",
		      ('E' == cmd) ? "PASSED": "FAILED", lexems[lex_count-1][2] ) );
		  goto end_of_test;
		}
	      if (END_OF_SPARQL_TEXT != ((ptrlong *)(lexems[lex_count-1]))[3])
		{
		  dk_set_push (&report, box_dv_short_string ("FAILED: end of source is not reached and no error reported!"));
		  goto end_of_test;
		}
	      if (1 == lex_count)
		{
		  dk_set_push (&report, box_dv_short_string ("FAILED: no lexems parsed and only end of source has found!"));
		  goto end_of_test;
		}
	      last_lval = ((ptrlong *)(lexems[lex_count-2]))[3];
	      if ('E' == cmd)
		{
		  dk_set_push (&report,
		    box_sprintf (0x1000, "FAILED: %d lexems found, last lexem is %d, must be error",
		      lex_count, last_lval) );
		  goto end_of_test;
		}
	      if ('K' == cmd)
		{
		  if (4 > lex_count)
		    {
		      dk_set_push (&report,
			box_sprintf (0x1000, "FAILED: %d lexems found, the number of actual lexems is less than two",
			  lex_count ) );
		      goto end_of_test;
		    }
		  last1_lval = ((ptrlong *)(lexems[lex_count-3]))[3];
		  dk_set_push (&report,
		    box_sprintf (0x1000, "%s: %d lexems found, one-before-last lexem is %d, must be %d",
		      (last1_lval == tested_lex_val) ? "PASSED": "FAILED", lex_count, last1_lval, tested_lex_val) );
		  goto end_of_test;
		}
	      if ('L' == cmd)
		{
		  dk_set_push (&report,
		    box_sprintf (0x1000, "%s: %d lexems found, last lexem is %d, must be %d",
		      (last_lval == tested_lex_val) ? "PASSED": "FAILED", lex_count, last_lval, tested_lex_val) );
		  goto end_of_test;
		}
	      if ('M' == cmd)
		{
		  unsigned lctr;
		  for (lctr = 0; lctr < lex_count; lctr++)
		    {
		      int lval = ((ptrlong *)(lexems[lctr]))[3];
		      if (lval == tested_lex_val)
			{
			  dk_set_push (&report,
			    box_sprintf (0x1000, "FAILED: %d lexems found, lexem %d is found but it should not occur",
			      lex_count, tested_lex_val) );
			  goto end_of_test;
			}
		    }
		  dk_set_push (&report,
		    box_sprintf (0x1000, "PASSED: %d lexems found, lexem %d is not found and it should not occur",
		      lex_count, tested_lex_val) );
		  goto end_of_test;
		}
	      GPF_T;
end_of_test:
	      dk_free_tree (lexems);
	      break;		
	    default: GPF_T;
	    }
	  }
    }
  return revlist_to_array (report);
}
#endif


void
sparql_init (void)
{
  rdf_ds_load_all();
  bif_define ("sparql_to_sql_text", bif_sparql_to_sql_text);
  bif_define ("sparql_explain", bif_sparql_explain);
  bif_define ("sparql_lex_analyze", bif_sparql_lex_analyze);
  bif_define ("sparql_quad_maps_for_quad", bif_sparql_quad_maps_for_quad);
  bif_define ("sparql_sql_cols_for_quad", bif_sparql_sql_cols_for_quad);
  bif_define ("__sprintff_is_proven_bijection", bif_sprintff_is_proven_bijection);
  bif_define ("__sprintff_intersect", bif_sprintff_intersect);
  bif_define ("__sprintff_like", bif_sprintff_like);
#ifdef DEBUG
  bif_define ("sparql_lex_test", bif_sparql_lex_test);
#endif
}
