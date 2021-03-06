#include <yajl/yajl_parse.h>
#include "ajv_state.h"
#include "yajl_interface.h"
#include "api/ajv_parse.h"
#include "orderly_json.h"
#include "ajv_schema.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

void ajv_state_push(ajv_state state, const ajv_node *n) {
  ajv_node_state s = ajv_alloc_node_state(state->AF, n);
  /* only maps and array have children */
  assert(n->node->t == orderly_node_object
         || n->node->t == orderly_node_array);
  s->node = state->node;
  orderly_ps_push(state->AF, state->node_state, s);

  state->node = state->node->child;
}

void ajv_state_pop(ajv_state state) {
  ajv_node_state s = state->node_state.stack[state->node_state.used - 1];  
  orderly_ps_pop(state->node_state);

  state->node = s->node;

  ajv_free_node_state(state->AF,&s);

  return;
}
 
   
  
void ajv_clear_error (ajv_state s) {

  if (s->error.extra_info) OR_FREE(s->AF,s->error.extra_info);

  s->error.code       = ajv_e_no_error;
  s->error.extra_info = NULL;
  s->error.node       = NULL;


}

ajv_node_state ajv_alloc_node_state( const orderly_alloc_funcs * alloc, 
                                     const ajv_node *node)
{
  ajv_node_state n = (ajv_node_state)OR_MALLOC(alloc, 
                                               sizeof(struct ajv_node_state_t));
  memset((void *) n, 0, sizeof(struct ajv_node_state_t));
  orderly_ps_init(n->required);
  orderly_ps_init(n->seen);
  n->node = node;
  return n;
}

void ajv_free_node_state( const orderly_alloc_funcs * alloc, 
                          ajv_node_state *node)
{
  if (node && *node) {
    orderly_ps_free(alloc, (*node)->seen);
    orderly_ps_free(alloc, (*node)->required);
    OR_FREE(alloc,*node);
    *node = NULL;
  }
}

void ajv_set_error ( ajv_state s, ajv_error e,
                     const ajv_node * node, const char *info, int infolen ) {

  ajv_clear_error(s);
  s->error.node = node;
  s->error.code = e;
  if (info) {
    BUF_STRDUP(s->error.extra_info, 
               s->AF, info, infolen);
  }

}

const char * ajv_error_to_string (ajv_error e) {

  const char *outbuf;
  switch(e) {
  case ajv_e_type_mismatch:  
    outbuf = "schema does not allow type"; break;
  case ajv_e_trailing_input: 
    outbuf = "input continued validation completed"; break;
  case ajv_e_out_of_range:   
    outbuf = "value out of range"; break;
  case ajv_e_incomplete_container: 
    outbuf = "incomplete structure"; break;
  case ajv_e_illegal_value:  
    outbuf = "illegal value encountered"; break;
  case ajv_e_regex_failed:  
    outbuf = "string did not match regular expression"; break;
  case ajv_e_unexpected_key: 
    outbuf = "encountered unknown property"; break;
  case ajv_e_invalid_format: 
    outbuf = "string was not of required format"; break;
  default:                   
    outbuf = "Internal error: unrecognized error code"; 
  };
  
  return outbuf;
}


unsigned char * ajv_get_error(ajv_handle hand, int verbose,
                              const unsigned char * jsonText,
                              unsigned int jsonTextLength) {
  char * yajl_err;
  orderly_buf ret = orderly_buf_alloc(hand->AF);
  ajv_state s = hand;
  unsigned char *cret;
  struct ajv_error_t *e = &(s->error);

  int yajl_length;
  const char *fn;
  if (e->node) {
    fn = ajv_node_format(e->node->node);
  }
  if (e->code == ajv_e_no_error) { 
    return yajl_get_error(hand->yajl,verbose,jsonText,jsonTextLength);
  } 

  /* include the yajl error message when verbose */
  if (verbose == 1) {
    yajl_err = 
      (char *)yajl_get_error(hand->yajl,verbose,
                             (unsigned char *)jsonText,jsonTextLength);

    yajl_length = strlen(yajl_err);
  }

  if (e->code == ajv_e_out_of_range) {
    const orderly_node *on = e->node->node;
    const char *type = orderly_node_type_to_string(on->t);
    orderly_buf_append_string(ret,type);
    if (on->t == orderly_node_array
        || on->t == orderly_node_string) {
      orderly_buf_append_string(ret," length");
    }
    orderly_buf_append_string(ret," ");
    orderly_buf_append_string(ret,e->extra_info);
    orderly_buf_append_string(ret," not in range ");
    if (ORDERLY_RANGE_SPECIFIED(on->range)) {
      char buf[128];
      orderly_buf_append_string(ret, "{");
      buf[0] = 0;
      if (ORDERLY_RANGE_LHS_DOUBLE & on->range.info)
        sprintf(buf, "%.15g", on->range.lhs.d);
      else if (ORDERLY_RANGE_LHS_INT & on->range.info)
        sprintf(buf, "%ld", on->range.lhs.i);
      if (buf[0]) orderly_buf_append_string(ret, buf);
      orderly_buf_append_string(ret, ",");
      buf[0] = 0;
      if (ORDERLY_RANGE_RHS_DOUBLE & on->range.info)
        sprintf(buf, "%.15g", on->range.rhs.d);
      else if (ORDERLY_RANGE_RHS_INT & on->range.info)
        sprintf(buf, "%ld", on->range.rhs.i);
      if (buf[0]) orderly_buf_append_string(ret, buf);
      orderly_buf_append_string(ret, "}");
    }
  } else {
    orderly_buf_append_string(ret, (const char *)ajv_error_to_string(e->code));
  }

  if (e->code == ajv_e_invalid_format) {
    orderly_buf_append_string(ret, " '");
    orderly_buf_append_string(ret, fn);
    orderly_buf_append_string(ret, "':");
  }

  if (e->extra_info) {
    if (e->code == ajv_e_incomplete_container) {
      if (e->node) {
        if (e->node->node->t == orderly_node_object) {
          orderly_buf_append_string(ret, ", object missing required property");
        } else {
          orderly_buf_append_string(ret, ", tuple missing ");
          orderly_buf_append_string(ret, e->extra_info);
          orderly_buf_append_string(ret, " elements");
        }
      }
    }
    if (e->code == ajv_e_illegal_value) {
      orderly_buf_append_string(ret, ":");
    }
    if (e->code != ajv_e_out_of_range 
        && !(e->code == ajv_e_incomplete_container 
             && e->node 
             && e->node->node->t == orderly_node_array)) {
      orderly_buf_append_string(ret, " '");
      orderly_buf_append_string(ret, e->extra_info);
      orderly_buf_append_string(ret, "'");
    }
  }

  if (e->node) {
      if (e->code == ajv_e_type_mismatch) {
        if (e->node->node->name) {
          orderly_buf_append_string(ret, " for property '");
          orderly_buf_append_string(ret, e->node->node->name);
          orderly_buf_append_string(ret, "'");
        }
        if (e->node->parent && e->node->parent->node->tuple_typed == 1) {
          ajv_node_state ns = (ajv_node_state)orderly_ps_current(s->node_state);
          char buf[128];
          orderly_buf_append_string(ret, " for array element ");
          snprintf(buf,128,"%d",orderly_ps_length(ns->seen)+1);
          orderly_buf_append_string(ret,buf);
        }
        orderly_buf_append_string(ret, ", expected '");
        orderly_buf_append_string(ret, orderly_node_type_to_string(e->node->node->t));
        orderly_buf_append_string(ret, "'");
      }
  }
  if (e->code == ajv_e_unexpected_key) {
        orderly_buf_append_string(ret, ", while additionalProperties forbidden");
  }
  orderly_buf_append_string(ret,".");
  BUF_STRDUP(cret, hand->AF, orderly_buf_data(ret),
             orderly_buf_len(ret));
  orderly_buf_free(ret);
  return cret;
}

void ajv_free_error(ajv_handle hand, unsigned char *str) {
  OR_FREE(hand->AF, str);
}


yajl_status ajv_parse_and_validate(ajv_handle hand,
                                   const unsigned char * jsonText,
                                   unsigned int jsonTextLength,
                                   ajv_schema schema) {
  yajl_status stat;
  yajl_handle yh = hand->yajl;
  if (schema) {
    ajv_node_state s = ajv_alloc_node_state(hand->AF, schema->root);
    ajv_clear_error(hand);
    hand->s = schema;
    hand->node = schema->root;
    orderly_ps_push(hand->AF, hand->node_state, s);
    memcpy(&hand->ourcb, &ajv_callbacks,sizeof(yajl_callbacks));
  } else {
    memcpy(&hand->ourcb, &ajv_passthrough,sizeof(yajl_callbacks));
  }
  stat = yajl_parse(yh, jsonText, jsonTextLength);
  if (hand->error.code != ajv_e_no_error) {
    assert(stat == yajl_status_client_canceled);
    stat = yajl_status_error;
  } 

  return stat;
}

yajl_status ajv_validate(ajv_handle hand,
                        ajv_schema schema,
                        orderly_json *json) {
  yajl_status ret =  yajl_status_ok;
  int cancelled;
  ajv_clear_error(hand);
  hand->s = schema;
  hand->node = schema->root;
  ajv_node_state s = ajv_alloc_node_state(hand->AF, schema->root);
  orderly_ps_push(hand->AF, hand->node_state, s);


  cancelled = orderly_synthesize_callbacks(&ajv_callbacks,hand,json);
  if (cancelled == 1) {
    if (hand->error.code == ajv_e_no_error) {
      ret = yajl_status_client_canceled;
    } else {
      ret = yajl_status_error;
    }
  }
  
  return ret;
}

void ajv_state_mark_seen(ajv_state s, const ajv_node *node) {
  ajv_node_state ns;
  ns = (ajv_node_state)orderly_ps_current(s->node_state);
  if (node->parent && node->parent->node->t == orderly_node_union) {
    orderly_ps_push(s->AF, ns->seen, (void *)(node->parent));
  } else {
    orderly_ps_push(s->AF, ns->seen, (void *)(node));
  }
  /* advance the current pointer if we're checking a tuple typed array */
  if (node->parent &&
      node->parent->node->t == orderly_node_array &&
      node->parent->node->tuple_typed) {
    if (node->sibling) {
      s->node = s->node->sibling;
    } else {
      /* otherwise, put us into schemaless mode */
      ((orderly_node *)(s->any.node))->t = 
        ajv_state_parent(s)->node->additional_properties; 
      s->any.sibling = &(s->any);
      s->depth = 0;
      s->any.parent = ajv_state_parent(s);
      s->node = &(s->any);
    }
  }
}

ajv_handle ajv_alloc(const yajl_callbacks * callbacks,
                     const yajl_parser_config * config,
                     const yajl_alloc_funcs * allocFuncs,
                     void * ctx) {
  const orderly_alloc_funcs * AF = (const orderly_alloc_funcs *) allocFuncs;
  {
    static orderly_alloc_funcs orderlyAllocFuncBuffer;
    static orderly_alloc_funcs * orderlyAllocFuncBufferPtr = NULL;
    
    if (orderlyAllocFuncBufferPtr == NULL) {
            orderly_set_default_alloc_funcs(&orderlyAllocFuncBuffer);
            orderlyAllocFuncBufferPtr = &orderlyAllocFuncBuffer;
    }
    AF = orderlyAllocFuncBufferPtr;

    }

  
  struct ajv_state_t *ajv_state = 
    (struct ajv_state_t *)
    OR_MALLOC(AF, sizeof(struct ajv_state_t));
  memset((void *) ajv_state, 0, sizeof(struct ajv_state_t));
  ajv_state->AF = AF;
  ajv_state->any.parent = ajv_state->any.child = ajv_state->any.sibling = NULL;
  ajv_state->any.node = orderly_alloc_node((orderly_alloc_funcs *)AF, 
                                           orderly_node_any);
  ajv_state->cb = callbacks;
  ajv_state->cbctx = ctx;
  ajv_state->ypc = config;
  ajv_state->yajl = yajl_alloc(&(ajv_state->ourcb),
                               config,
                               allocFuncs,
                               (void *)ajv_state);

  orderly_ps_init(ajv_state->node_state);
  
  return ajv_state;
}


void ajv_free(ajv_handle hand) {
  const orderly_alloc_funcs *AF = hand->AF;
  ajv_node_state        cur;
 
   ajv_clear_error(hand);
 
   orderly_free_node(hand->AF,(orderly_node **)&(hand->any.node));
  
  while (orderly_ps_length(hand->node_state)) {
    cur = orderly_ps_current(hand->node_state);
    ajv_free_node_state(hand->AF,&cur);
    orderly_ps_pop(hand->node_state);
  }
  orderly_ps_free(hand->AF,hand->node_state);

  orderly_free_node(hand->AF,(orderly_node **)&(hand->any.node));

  yajl_free(hand->yajl);
  OR_FREE(AF,hand);

}

yajl_status ajv_parse_complete(ajv_handle hand) {
  yajl_status stat = yajl_parse_complete(hand->yajl);

  if (stat == yajl_status_ok || stat == yajl_status_insufficient_data) {
    if ( hand->s && !ajv_state_finished(hand) ) {
      ajv_set_error(hand, ajv_e_incomplete_container, NULL, "Empty root", strlen("Empty root"));
      stat = yajl_status_error;
    }
  }
  return stat;
}


int ajv_state_map_complete (ajv_state state, const ajv_node *map) {
  ajv_node_state ns = state->node_state.stack[state->node_state.used - 1];
  int i = 0, j;
  int maxreq = orderly_ps_length(ns->required);
  int maxseen = orderly_ps_length(ns->seen);
  /* XXX: n^2 */
  for (i = 0 ; i < maxreq ; i++) {
    int found = 0;
    ajv_node *req = ns->required.stack[i];
    for (j = 0 ; j < maxseen; j++) {
      ajv_node *seen = ns->seen.stack[j];
      if (req == seen) {
        found = 1;
        break;
      }
    }
    if (found == 0) {
      if (req->node->default_value) {    
        int ret;
        if (state->cb->yajl_map_key) {
          ret = state->cb->yajl_map_key(
                                        state->cbctx,
                                        (const unsigned char *)req->node->name,
                                        strlen(req->node->name));
          if (ret == 0) {
            return 0;
          }
        }
        ret = orderly_synthesize_callbacks(state->cb, state->cbctx, 
                                           req->node->default_value);
        if (ret == 0) {
          return 0;
        }
      } else {
        ajv_set_error(state,ajv_e_incomplete_container,map,req->node->name,strlen(req->node->name));
        return 0;
      }
    }
  }
  maxreq = orderly_ps_length(ns->node->required);
  for (i = 0 ; i < maxreq ; i++) {
    int found = 0;
    ajv_node *req = ns->node->required.stack[i];
    for (j = 0 ; j < maxseen; j++) {
      ajv_node *seen = ns->seen.stack[j];
      if (req == seen) {
        found = 1;
        break;
      }
    }
    if (found == 0) {
      if (req->node->default_value) {    
        int ret;
        if (state->cb->yajl_map_key) {
          ret = state->cb->yajl_map_key(
                                        state->cbctx,
                                        (const unsigned char *)req->node->name,
                                        strlen(req->node->name));
          if (ret == 0) {
            return 0;
          }
        }
        ret = orderly_synthesize_callbacks(state->cb, state->cbctx, 
                                           req->node->default_value);
        if (ret == 0) {
          return 0;
        }
      } else {
        ajv_set_error(state,ajv_e_incomplete_container,map,req->node->name,strlen(req->node->name));
        return 0;
      }
    }
  }
  ajv_state_pop(state);
  ajv_state_mark_seen(state,map);
  return 1;
}

const ajv_node * ajv_state_parent(ajv_state state) {
  ajv_node_state s = state->node_state.stack[state->node_state.used - 1];  
  return s->node;
}

int ajv_state_array_complete (ajv_state state) {
  const ajv_node *array;
  ajv_node_state s = state->node_state.stack[state->node_state.used - 1];  
  array = s->node;
  if (!ajv_check_integer_range(state,array,
                               orderly_ps_length(s->seen))) {
    return 0;
  }
  /* with tuple typed nodes, we need to check that we've seen things */
  if (array->node->tuple_typed) {
    assert(state->node->parent == array);
    ajv_node_state s  = orderly_ps_current(state->node_state);
    if ((! orderly_ps_length(s->seen) /* seen nothing */
        || orderly_ps_current(s->seen) != state->node)
        && state->node != &(state->any)) {
      const ajv_node *cur = state->node;
      do {
        if (cur->node->default_value) {
          int ret;
          ret = orderly_synthesize_callbacks(state->cb, state->cbctx,
                                             cur->node->default_value);
          if (ret == 0) { /*parse was cancelled */
            return 0;
          }
        } else { 
          int remaining = 0;
          char buf[128];
          do {remaining++; } while ((cur = cur->sibling));
          snprintf(buf,128,"%d",remaining);
          ajv_set_error(state,ajv_e_incomplete_container,array,buf,strlen(buf));
          return 0;
        }
        cur = cur->sibling;
      } while (cur);
    }
  }
  ajv_state_pop(state); 
  return 1;
}

int ajv_state_finished(ajv_state state) {
  return 
    (orderly_ps_length(((ajv_node_state)
                        state->node_state.stack[0])->seen) != 0);

}


void ajv_state_require(ajv_state state, ajv_node *req) {
 ajv_node_state s = state->node_state.stack[state->node_state.used - 1];  
 orderly_ps_push(state->AF, s->required, req);
}

unsigned int ajv_get_bytes_consumed(ajv_state state) {
  return yajl_get_bytes_consumed(state->yajl);
}

int ajv_check_integer_range(ajv_state state, const ajv_node *an, long l) {
  char buf[128];
  orderly_range r = an->node->range;
  if (ORDERLY_RANGE_SPECIFIED(r)) {
    if (ORDERLY_RANGE_HAS_LHS(r)) {
      if (((ORDERLY_RANGE_LHS_DOUBLE & r.info) ? r.lhs.d : r.lhs.i) > l) {
        snprintf(buf,128,"%ld",l);
        ajv_set_error(state, ajv_e_out_of_range, an, buf,strlen(buf));
        return 0;
      }
    }
    if (ORDERLY_RANGE_HAS_RHS(r)) {
      if (((ORDERLY_RANGE_RHS_DOUBLE & r.info) ? r.rhs.d : r.rhs.i) < l) {
        snprintf(buf,128,"%ld",l);
        ajv_set_error(state, ajv_e_out_of_range, an, buf,strlen(buf));
        return 0;
      }
    }
  }
  return 1;
}




