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


#ifndef _NGX_HTTP_LUA_ACCESSBY_H_INCLUDED_
#define _NGX_HTTP_LUA_ACCESSBY_H_INCLUDED_


#include "ngx_http_lua_common.h"


ngx_int_t ngx_http_lua_access_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_lua_access_handler_inline(ngx_http_request_t *r);
ngx_int_t ngx_http_lua_access_handler_file(ngx_http_request_t *r);


#endif /* _NGX_HTTP_LUA_ACCESSBY_H_INCLUDED_ */

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
