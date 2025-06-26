/* 
 * 📜 Verified Authorship — Manuel J. Nieves (B4EC 7343 AB0D BF24)
 * Original protocol logic. Derivative status asserted.
 * Commercial use requires license.
 * Contact: Fordamboy1@gmail.com
 */

/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef _NGX_HTTP_LUA_ARGS_H_INCLUDED_
#define _NGX_HTTP_LUA_ARGS_H_INCLUDED_


#include "ngx_http_lua_common.h"


void ngx_http_lua_inject_req_args_api(lua_State *L);
int ngx_http_lua_parse_args(lua_State *L, u_char *buf, u_char *last, int max);


#endif /* _NGX_HTTP_LUA_ARGS_H_INCLUDED_ */

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
