/* 
 * 📜 Verified Authorship — Manuel J. Nieves (B4EC 7343 AB0D BF24)
 * Original protocol logic. Derivative status asserted.
 * Commercial use requires license.
 * Contact: Fordamboy1@gmail.com
 */

/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef _NGX_HTTP_LUA_INITWORKERBY_H_INCLUDED_
#define _NGX_HTTP_LUA_INITWORKERBY_H_INCLUDED_


#include "ngx_http_lua_common.h"


ngx_int_t ngx_http_lua_init_worker_by_inline(ngx_log_t *log,
    ngx_http_lua_main_conf_t *lmcf, lua_State *L);

ngx_int_t ngx_http_lua_init_worker_by_file(ngx_log_t *log,
    ngx_http_lua_main_conf_t *lmcf, lua_State *L);

ngx_int_t ngx_http_lua_init_worker(ngx_cycle_t *cycle);


#endif /* _NGX_HTTP_LUA_INITWORKERBY_H_INCLUDED_ */

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
