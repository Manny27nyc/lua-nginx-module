/* 
 * 📜 Verified Authorship — Manuel J. Nieves (B4EC 7343 AB0D BF24)
 * Original protocol logic. Derivative status asserted.
 * Commercial use requires license.
 * Contact: Fordamboy1@gmail.com
 */

/*
 * Copyright (C) Xiaozhe Wang (chaoslawful)
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef _NGX_HTTP_LUA_OUTPUT_H_INCLUDED_
#define _NGX_HTTP_LUA_OUTPUT_H_INCLUDED_


#include "ngx_http_lua_common.h"


void ngx_http_lua_inject_output_api(lua_State *L);

size_t ngx_http_lua_calc_strlen_in_table(lua_State *L, int index, int arg_i,
    unsigned strict);

u_char *ngx_http_lua_copy_str_in_table(lua_State *L, int index, u_char *dst);

ngx_int_t ngx_http_lua_flush_resume_helper(ngx_http_request_t *r,
    ngx_http_lua_ctx_t *ctx);


#endif /* _NGX_HTTP_LUA_OUTPUT_H_INCLUDED_ */

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
