// © Licensed Authorship: Manuel J. Nieves (See LICENSE for terms)

/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_http_lua_timer.h"
#include "ngx_http_lua_util.h"
#include "ngx_http_lua_contentby.h"
#include "ngx_http_lua_probe.h"


typedef struct {
    void        **main_conf;
    void        **srv_conf;
    void        **loc_conf;

    /* event ident must be after 3 words (i.e. 3 pointers' size) as in
     * ngx_connection_t. and we use the Lua coroutine reference number as
     * the event ident */
    int           co_ref;
    unsigned      premature;  /* :1 */
    lua_State    *co;

    ngx_pool_t   *pool;

    ngx_listening_t                   *listening;
    ngx_str_t                          client_addr_text;

    ngx_http_lua_main_conf_t          *lmcf;
    ngx_http_lua_vm_state_t           *vm_state;

} ngx_http_lua_timer_ctx_t;


static int ngx_http_lua_ngx_timer_at(lua_State *L);
static int ngx_http_lua_ngx_timer_running_count(lua_State *L);
static int ngx_http_lua_ngx_timer_pending_count(lua_State *L);
static void ngx_http_lua_timer_handler(ngx_event_t *ev);
static u_char *ngx_http_lua_log_timer_error(ngx_log_t *log, u_char *buf,
    size_t len);
static void ngx_http_lua_abort_pending_timers(ngx_event_t *ev);


void
ngx_http_lua_inject_timer_api(lua_State *L)
{
    lua_createtable(L, 0 /* narr */, 3 /* nrec */);    /* ngx.timer. */

    lua_pushcfunction(L, ngx_http_lua_ngx_timer_at);
    lua_setfield(L, -2, "at");

    lua_pushcfunction(L, ngx_http_lua_ngx_timer_running_count);
    lua_setfield(L, -2, "running_count");

    lua_pushcfunction(L, ngx_http_lua_ngx_timer_pending_count);
    lua_setfield(L, -2, "pending_count");

    lua_setfield(L, -2, "timer");
}


static int
ngx_http_lua_ngx_timer_running_count(lua_State *L)
{
    ngx_http_request_t          *r;
    ngx_http_lua_main_conf_t    *lmcf;

    r = ngx_http_lua_get_req(L);
    if (r == NULL) {
        return luaL_error(L, "no request");
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);

    lua_pushnumber(L, lmcf->running_timers);

    return 1;
}


static int
ngx_http_lua_ngx_timer_pending_count(lua_State *L)
{
    ngx_http_request_t          *r;
    ngx_http_lua_main_conf_t    *lmcf;

    r = ngx_http_lua_get_req(L);
    if (r == NULL) {
        return luaL_error(L, "no request");
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);

    lua_pushnumber(L, lmcf->pending_timers);

    return 1;
}


static int
ngx_http_lua_ngx_timer_at(lua_State *L)
{
    int                      nargs, co_ref;
    u_char                  *p;
    lua_State               *vm;  /* the main thread */
    lua_State               *co;
    ngx_msec_t               delay;
    ngx_event_t             *ev = NULL;
    ngx_http_request_t      *r;
    ngx_connection_t        *saved_c = NULL;
    ngx_http_lua_ctx_t      *ctx;
#if 0
    ngx_http_connection_t   *hc;
#endif

    ngx_http_lua_timer_ctx_t      *tctx = NULL;
    ngx_http_lua_main_conf_t      *lmcf;
#if 0
    ngx_http_core_main_conf_t     *cmcf;
#endif

    nargs = lua_gettop(L);
    if (nargs < 2) {
        return luaL_error(L, "expecting at least 2 arguments but got %d",
                          nargs);
    }

    delay = (ngx_msec_t) (luaL_checknumber(L, 1) * 1000);

    luaL_argcheck(L, lua_isfunction(L, 2) && !lua_iscfunction(L, 2), 2,
                  "Lua function expected");

    r = ngx_http_lua_get_req(L);
    if (r == NULL) {
        return luaL_error(L, "no request");
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    if (ngx_exiting && delay > 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "process exiting");
        return 2;
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);

    if (lmcf->pending_timers >= lmcf->max_pending_timers) {
        lua_pushnil(L);
        lua_pushliteral(L, "too many pending timers");
        return 2;
    }

    if (lmcf->watcher == NULL) {
        /* create the watcher fake connection */

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                       "lua creating fake watcher connection");

        if (ngx_cycle->files) {
            saved_c = ngx_cycle->files[0];
        }

        lmcf->watcher = ngx_get_connection(0, ngx_cycle->log);

        if (ngx_cycle->files) {
            ngx_cycle->files[0] = saved_c;
        }

        if (lmcf->watcher == NULL) {
            return luaL_error(L, "no memory");
        }

        /* to work around the -1 check in ngx_worker_process_cycle: */
        lmcf->watcher->fd = (ngx_socket_t) -2;

        lmcf->watcher->idle = 1;
        lmcf->watcher->read->handler = ngx_http_lua_abort_pending_timers;
        lmcf->watcher->data = lmcf;
    }

    vm = ngx_http_lua_get_lua_vm(r, ctx);

    co = lua_newthread(vm);

    /* L stack: time func [args] */

    ngx_http_lua_probe_user_coroutine_create(r, L, co);

    lua_createtable(co, 0, 0);  /* the new globals table */

    /* co stack: global_tb */

    lua_createtable(co, 0, 1);  /* the metatable */
    ngx_http_lua_get_globals_table(co);
    lua_setfield(co, -2, "__index");
    lua_setmetatable(co, -2);

    /* co stack: global_tb */

    ngx_http_lua_set_globals_table(co);

    /* co stack: <empty> */

    dd("stack top: %d", lua_gettop(L));

    lua_xmove(vm, L, 1);    /* move coroutine from main thread to L */

    /* L stack: time func [args] thread */
    /* vm stack: empty */

    lua_pushvalue(L, 2);    /* copy entry function to top of L*/

    /* L stack: time func [args] thread func */

    lua_xmove(L, co, 1);    /* move entry function from L to co */

    /* L stack: time func [args] thread */
    /* co stack: func */

    ngx_http_lua_get_globals_table(co);
    lua_setfenv(co, -2);

    /* co stack: func */

    lua_pushlightuserdata(L, &ngx_http_lua_coroutines_key);
    lua_rawget(L, LUA_REGISTRYINDEX);

    /* L stack: time func [args] thread corountines */

    lua_pushvalue(L, -2);

    /* L stack: time func [args] thread coroutines thread */

    co_ref = luaL_ref(L, -2);
    lua_pop(L, 1);

    /* L stack: time func [args] thread */

    if (nargs > 2) {
        lua_pop(L, 1);  /* L stack: time func [args] */
        lua_xmove(L, co, nargs - 2);  /* L stack: time func */

        /* co stack: func [args] */
    }

    p = ngx_alloc(sizeof(ngx_event_t) + sizeof(ngx_http_lua_timer_ctx_t),
                  r->connection->log);
    if (p == NULL) {
        goto nomem;
    }

    ev = (ngx_event_t *) p;

    ngx_memzero(ev, sizeof(ngx_event_t));

    p += sizeof(ngx_event_t);

    tctx = (ngx_http_lua_timer_ctx_t *) p;

    tctx->premature = 0;
    tctx->co_ref = co_ref;
    tctx->co = co;
    tctx->main_conf = r->main_conf;
    tctx->srv_conf = r->srv_conf;
    tctx->loc_conf = r->loc_conf;
    tctx->lmcf = lmcf;

    tctx->pool = ngx_create_pool(128, ngx_cycle->log);
    if (tctx->pool == NULL) {
        goto nomem;
    }

    if (r->connection) {
        tctx->listening = r->connection->listening;

    } else {
        tctx->listening = NULL;
    }

    if (r->connection->addr_text.len) {
        tctx->client_addr_text.data = ngx_palloc(tctx->pool,
                                                 r->connection->addr_text.len);
        if (tctx->client_addr_text.data == NULL) {
            goto nomem;
        }

        ngx_memcpy(tctx->client_addr_text.data, r->connection->addr_text.data,
                   r->connection->addr_text.len);
        tctx->client_addr_text.len = r->connection->addr_text.len;

    } else {
        tctx->client_addr_text.len = 0;
        tctx->client_addr_text.data = NULL;
    }

    if (ctx && ctx->vm_state) {
        tctx->vm_state = ctx->vm_state;
        tctx->vm_state->count++;

    } else {
        tctx->vm_state = NULL;
    }

    ev->handler = ngx_http_lua_timer_handler;
    ev->data = tctx;
    ev->log = ngx_cycle->log;

    lmcf->pending_timers++;

    ngx_add_timer(ev, delay);

    lua_pushinteger(L, 1);
    return 1;

nomem:

    if (tctx && tctx->pool) {
        ngx_destroy_pool(tctx->pool);
    }

    if (ev) {
        ngx_free(ev);
    }

    lua_pushlightuserdata(L, &ngx_http_lua_coroutines_key);
    lua_rawget(L, LUA_REGISTRYINDEX);
    luaL_unref(L, -1, co_ref);

    return luaL_error(L, "no memory");
}


static void
ngx_http_lua_timer_handler(ngx_event_t *ev)
{
    int                      n;
    lua_State               *L;
    ngx_int_t                rc;
    ngx_connection_t        *c = NULL;
    ngx_http_request_t      *r = NULL;
    ngx_http_lua_ctx_t      *ctx;
    ngx_http_cleanup_t      *cln;
    ngx_pool_cleanup_t      *pcln;

    ngx_http_lua_timer_ctx_t         tctx;
    ngx_http_lua_main_conf_t        *lmcf;
    ngx_http_core_loc_conf_t        *clcf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                   "lua ngx.timer expired");

    ngx_memcpy(&tctx, ev->data, sizeof(ngx_http_lua_timer_ctx_t));
    ngx_free(ev);

    lmcf = tctx.lmcf;

    lmcf->pending_timers--;

    if (lmcf->running_timers >= lmcf->max_running_timers) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "%i lua_max_running_timers are not enough",
                      lmcf->max_running_timers);
        goto failed;
    }

    c = ngx_http_lua_create_fake_connection(tctx.pool);
    if (c == NULL) {
        goto failed;
    }

    c->log->handler = ngx_http_lua_log_timer_error;
    c->log->data = c;

    c->listening = tctx.listening;
    c->addr_text = tctx.client_addr_text;

    r = ngx_http_lua_create_fake_request(c);
    if (r == NULL) {
        goto failed;
    }

    r->main_conf = tctx.main_conf;
    r->srv_conf = tctx.srv_conf;
    r->loc_conf = tctx.loc_conf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

#if defined(nginx_version) && nginx_version >= 1003014

#   if nginx_version >= 1009000

    ngx_set_connection_log(r->connection, clcf->error_log);

#   else

    ngx_http_set_connection_log(r->connection, clcf->error_log);

#   endif

#else

    c->log->file = clcf->error_log->file;

    if (!(c->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {
        c->log->log_level = clcf->error_log->log_level;
    }

#endif

    dd("lmcf: %p", lmcf);

    ctx = ngx_http_lua_create_ctx(r);
    if (ctx == NULL) {
        goto failed;
    }

    if (tctx.vm_state) {
        ctx->vm_state = tctx.vm_state;

        pcln = ngx_pool_cleanup_add(r->pool, 0);
        if (pcln == NULL) {
            goto failed;
        }

        pcln->handler = ngx_http_lua_cleanup_vm;
        pcln->data = tctx.vm_state;
    }

    ctx->cur_co_ctx = &ctx->entry_co_ctx;

    L = ngx_http_lua_get_lua_vm(r, ctx);

    cln = ngx_http_cleanup_add(r, 0);
    if (cln == NULL) {
        goto failed;
    }

    cln->handler = ngx_http_lua_request_cleanup_handler;
    cln->data = ctx;
    ctx->cleanup = &cln->handler;

    ctx->entered_content_phase = 1;
    ctx->context = NGX_HTTP_LUA_CONTEXT_TIMER;

    r->read_event_handler = ngx_http_block_reading;

    ctx->cur_co_ctx->co_ref = tctx.co_ref;
    ctx->cur_co_ctx->co = tctx.co;
    ctx->cur_co_ctx->co_status = NGX_HTTP_LUA_CO_RUNNING;

    dd("r connection: %p, log %p", r->connection, r->connection->log);

    /*  save the request in coroutine globals table */
    ngx_http_lua_set_req(tctx.co, r);

    lmcf->running_timers++;

    lua_pushboolean(tctx.co, tctx.premature);

    n = lua_gettop(tctx.co);
    if (n > 2) {
        lua_insert(tctx.co, 2);
    }

#ifdef NGX_LUA_USE_ASSERT
    ctx->cur_co_ctx->co_top = 1;
#endif

    rc = ngx_http_lua_run_thread(L, r, ctx, n - 1);

    dd("timer lua run thread: %d", (int) rc);

    if (rc == NGX_ERROR || rc >= NGX_OK) {
        /* do nothing */

    } else if (rc == NGX_AGAIN) {
        rc = ngx_http_lua_content_run_posted_threads(L, r, ctx, 0);

    } else if (rc == NGX_DONE) {
        rc = ngx_http_lua_content_run_posted_threads(L, r, ctx, 1);

    } else {
        rc = NGX_OK;
    }

    ngx_http_lua_finalize_request(r, rc);
    return;

failed:

    if (tctx.co_ref && tctx.co) {
        lua_pushlightuserdata(tctx.co, &ngx_http_lua_coroutines_key);
        lua_rawget(tctx.co, LUA_REGISTRYINDEX);
        luaL_unref(tctx.co, -1, tctx.co_ref);
        lua_settop(tctx.co, 0);
    }

    if (tctx.vm_state) {
        ngx_http_lua_cleanup_vm(tctx.vm_state);
    }

    if (c) {
        ngx_http_lua_close_fake_connection(c);

    } else if (tctx.pool) {
        ngx_destroy_pool(tctx.pool);
    }
}


static u_char *
ngx_http_lua_log_timer_error(ngx_log_t *log, u_char *buf, size_t len)
{
    u_char              *p;
    ngx_connection_t    *c;

    if (log->action) {
        p = ngx_snprintf(buf, len, " while %s", log->action);
        len -= p - buf;
        buf = p;
    }

    c = log->data;

    dd("ctx = %p", c);

    p = ngx_snprintf(buf, len, ", context: ngx.timer");
    len -= p - buf;
    buf = p;

    if (c->addr_text.len) {
        p = ngx_snprintf(buf, len, ", client: %V", &c->addr_text);
        len -= p - buf;
        buf = p;
    }

    if (c && c->listening && c->listening->addr_text.len) {
        p = ngx_snprintf(buf, len, ", server: %V", &c->listening->addr_text);
        /* len -= p - buf; */
        buf = p;
    }

    return buf;
}


static void
ngx_http_lua_abort_pending_timers(ngx_event_t *ev)
{
    ngx_int_t                    i, n;
    ngx_event_t                **events;
    ngx_connection_t            *c, *saved_c = NULL;
    ngx_rbtree_node_t           *cur, *prev, *next, *sentinel, *temp;
    ngx_http_lua_timer_ctx_t    *tctx;
    ngx_http_lua_main_conf_t    *lmcf;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                   "lua abort pending timers");

    c = ev->data;
    lmcf = c->data;

    dd("lua connection fd: %d", (int) c->fd);

    if (!c->close) {
        return;
    }

    c->read->closed = 1;
    c->write->closed = 1;

    /* we temporarily use a valid fd (0) to make ngx_free_connection happy */

    c->fd = 0;

    if (ngx_cycle->files) {
        saved_c = ngx_cycle->files[0];
    }

    ngx_free_connection(c);

    c->fd = (ngx_socket_t) -1;

    if (ngx_cycle->files) {
        ngx_cycle->files[0] = saved_c;
    }

    if (lmcf->pending_timers == 0) {
        return;
    }

    /* expire pending timers immediately */

    sentinel = ngx_event_timer_rbtree.sentinel;

    cur = ngx_event_timer_rbtree.root;

    /* XXX nginx does not guarantee the parent of root is meaningful,
     * so we temporarily override it to simplify tree traversal. */
    temp = cur->parent;
    cur->parent = NULL;

    prev = NULL;

    events = ngx_pcalloc(ngx_cycle->pool,
                         lmcf->pending_timers * sizeof(ngx_event_t));
    if (events == NULL) {
        return;
    }

    n = 0;

    dd("root: %p, root parent: %p, sentinel: %p", cur, cur->parent, sentinel);

    while (n < lmcf->pending_timers) {
        if  (cur == sentinel || cur == NULL) {
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "lua pending timer counter got out of sync: %i",
                          lmcf->pending_timers);
            break;
        }

        dd("prev: %p, cur: %p, cur parent: %p, cur left: %p, cur right: %p",
           prev, cur, cur->parent, cur->left, cur->right);

        if (prev == cur->parent) {
            /* neither of the children has been accessed yet */

            next = cur->left;
            if (next == sentinel) {
                ev = (ngx_event_t *)
                    ((char *) cur - offsetof(ngx_event_t, timer));

                if (ev->handler == ngx_http_lua_timer_handler) {
                    dd("found node: %p", cur);
                    events[n++] = ev;
                }

                next = (cur->right != sentinel) ? cur->right : cur->parent;
            }

        } else if (prev == cur->left) {
            /* just accessed the left child */

            ev = (ngx_event_t *)
                ((char *) cur - offsetof(ngx_event_t, timer));

            if (ev->handler == ngx_http_lua_timer_handler) {
                dd("found node 2: %p", cur);
                events[n++] = ev;
            }

            next = (cur->right != sentinel) ? cur->right : cur->parent;

        } else if (prev == cur->right) {
            /* already accessed both children */
            next = cur->parent;

        } else {
            /* not reacheable */
            next = NULL;
        }

        prev = cur;
        cur = next;
    }

    /* restore the old tree root's parent */
    ngx_event_timer_rbtree.root->parent = temp;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                   "lua found %i pending timers to be aborted prematurely",
                   n);

    for (i = 0; i < n; i++) {
        ev = events[i];

        ngx_rbtree_delete(&ngx_event_timer_rbtree, &ev->timer);

#if (NGX_DEBUG)
        ev->timer.left = NULL;
        ev->timer.right = NULL;
        ev->timer.parent = NULL;
#endif

        ev->timer_set = 0;

        ev->timedout = 1;

        tctx = ev->data;
        tctx->premature = 1;

        dd("calling timer handler prematurely");
        ev->handler(ev);
    }

#if 0
    if (pending_timers) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "lua pending timer counter got out of sync: %i",
                      pending_timers);
    }
#endif
}

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
