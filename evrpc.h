/*
 * Copyright (c) 2006 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _EVRPC_H_
#define _EVRPC_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This header files provides basic support for an RPC server and client.
 *
 * To support RPCs in a server, every supported RPC command needs to be
 * defined and registered.
 *
 * EVRPC_HEADER(SendCommand, Request, Reply);
 *
 *  SendCommand is the name of the RPC command.
 *  Request is the name of a structure generated by event_rpcgen.py.
 *    It contains all parameters relating to the SendCommand RPC.  The
 *    server needs to fill in the Reply structure.
 *  Reply is the name of a structure generated by event_rpcgen.py.  It
 *    contains the answer to the RPC.
 *
 * To register an RPC with an HTTP server, you need to first create an RPC
 * base with:
 *
 *   struct evrpc_base *base = evrpc_init(http);
 *
 * A specific RPC can then be registered with
 *
 * EVRPC_REGISTER(base, SendCommand, Request, Reply,  FunctionCB, arg);
 *
 * when the server receives an appropriately formatted RPC, the user callback
 * is invokved.   The callback needs to fill in the reply structure.
 *
 * void FunctionCB(EVRPC_STRUCT(SendCommand)* rpc, void *arg);
 *
 * To send the reply, call EVRPC_REQUEST_DONE(rpc);
 *
 * See the regression test for an example.
 */

struct evbuffer;
struct evrpc_req_generic;

/* Encapsulates a request */
struct evrpc {
	TAILQ_ENTRY(evrpc) next;

	/* the URI at which the request handler lives */
	const char* uri;

	/* creates a new request structure */
	void *(*request_new)(void);

	/* frees the request structure */
	void (*request_free)(void *);

	/* unmarshals the buffer into the proper request structure */
	int (*request_unmarshal)(void *, struct evbuffer *);

	/* creates a new reply structure */
	void *(*reply_new)(void);

	/* creates a new reply structure */
	void (*reply_free)(void *);

	/* verifies that the reply is valid */
	int (*reply_complete)(void *);
	
	/* marshals the reply into a buffer */
	void (*reply_marshal)(struct evbuffer*, void *);

	/* the callback invoked for each received rpc */
	void (*cb)(struct evrpc_req_generic *, void *);
	void *cb_arg;
};

#define EVRPC_STRUCT(rpcname) struct evrpc_req__##rpcname

struct evhttp_request;

/* We alias the RPC specific structs to this voided one */
struct evrpc_req_generic {
	/* the unmarshaled request object */
	void *request;

	/* the empty reply object that needs to be filled in */
	void *reply;

	/* 
	 * the static structure for this rpc; that can be used to
	 * automatically unmarshal and marshal the http buffers.
	 */
	struct evrpc* rpc;

	/*
	 * the http request structure on which we need to answer.
	 */
	struct evhttp_request* http_req;

	/*
	 * callback to reply and finish answering this rpc
	 */
	void (*done)(struct evrpc_req_generic* rpc); 
};

/*
 * You need to use EVRPC_HEADER to create structures and function prototypes
 * needed by the server and client implmentation.
 */
#define EVRPC_HEADER(rpcname, reqstruct, rplystruct) \
EVRPC_STRUCT(rpcname) {	\
	struct reqstruct* request; \
	struct rplystruct* reply; \
	struct evrpc* rpc; \
	void (*done)(struct evrpc* rpc, void *request, void *reply); \
};								     \
int evrpc_send_request_##rpcname(struct evrpc_pool *, \
    struct reqstruct *, struct rplystruct *, \
    void (*)(struct reqstruct *, struct rplystruct *, void *cbarg), \
    void *);

#define EVRPC_GENERATE(rpcname, reqstruct, rplystruct) \
int evrpc_send_request_##rpcname(struct evrpc_pool *pool, \
    struct reqstruct *request, struct rplystruct *reply, \
    void (*cb)(struct reqstruct *, struct rplystruct *, void *cbarg), \
    void *cbarg) { \
	struct evrpc_request_wrapper *ctx;			    \
	ctx = (struct evrpc_request_wrapper *) \
	    malloc(sizeof(struct evrpc_request_wrapper));	    \
	if (ctx == NULL) {					    \
		(*(cb))(request, reply, cbarg);			    \
		return (-1);					    \
	}							    \
	ctx->pool = pool;					    \
	ctx->evcon = NULL;					    \
	ctx->name = strdup(#rpcname);				    \
	if (ctx->name == NULL) {				    \
		free(ctx);					    \
		(*(cb))(request, reply, cbarg);			    \
		return (-1);					    \
	}							    \
	ctx->cb = (void (*)(void *, void *, void *))cb;		    \
	ctx->cb_arg = cbarg;					    \
	ctx->request = (void *)request;				    \
	ctx->reply = (void *)reply;				    \
	ctx->request_marshal = (void (*)(struct evbuffer *, void *))reqstruct##_marshal; \
	ctx->reply_clear = (void (*)(void *))rplystruct##_clear;    \
	ctx->reply_unmarshal = (int (*)(void *, struct evbuffer *))rplystruct##_unmarshal; \
	return (evrpc_make_request(ctx));			    \
}


/* 
 * EVRPC_REQUEST_DONE is used to answer a request; the reply is expected
 * to have been filled in.  The request and reply pointers become invalid
 * after this call has finished.
 */
#define EVRPC_REQUEST_DONE(rpc_req) do { \
  struct evrpc_req_generic *_req = (struct evrpc_req_generic *)(rpc_req); \
  _req->done(_req); \
} while (0)
  

/* Takes a request object and fills it in with the right magic */
#define EVRPC_REGISTER_OBJECT(rpc, name, request, reply) \
  do { \
    (rpc)->uri = strdup(#name); \
    if ((rpc)->uri == NULL) {			 \
      fprintf(stderr, "failed to register object\n");	\
      exit(1);						\
    } \
    (rpc)->request_new = (void *(*)(void))request##_new; \
    (rpc)->request_free = (void (*)(void *))request##_free; \
    (rpc)->request_unmarshal = (int (*)(void *, struct evbuffer *))request##_unmarshal; \
    (rpc)->reply_new = (void *(*)(void))reply##_new; \
    (rpc)->reply_free = (void (*)(void *))reply##_free; \
    (rpc)->reply_complete = (int (*)(void *))reply##_complete; \
    (rpc)->reply_marshal = (void (*)(struct evbuffer*, void *))reply##_marshal; \
  } while (0)

struct evrpc_base;
struct evhttp;

/* functions to start up the rpc system */
struct evrpc_base *evrpc_init(struct evhttp *server);

/* this macro is used to register RPCs with the HTTP Server */
#define EVRPC_REGISTER(base, name, request, reply, callback, cbarg) \
  do { \
    struct evrpc* rpc = (struct evrpc *)calloc(1, sizeof(struct evrpc)); \
    EVRPC_REGISTER_OBJECT(rpc, name, request, reply); \
    evrpc_register_rpc(base, rpc, \
	(void (*)(struct evrpc_req_generic*, void *))callback, cbarg);	\
  } while (0)

int evrpc_register_rpc(struct evrpc_base *, struct evrpc *,
    void (*)(struct evrpc_req_generic*, void *), void *);

/*
 * Client-side RPC support
 */

struct evrpc_pool;
struct evhttp_connection;

struct evrpc_request_wrapper {
	TAILQ_ENTRY(evrpc_request_wrapper) next;

        /* pool on which this rpc request is being made */
        struct evrpc_pool *pool;

        /* connection on which the request is being sent */
	struct evhttp_connection *evcon;

	/* event for implementing request timeouts */
	struct event ev_timeout;

	/* the name of the rpc */
	char *name;

	/* callback */
	void (*cb)(void *request, void *reply, void *arg);
	void *cb_arg;

	void *request;
	void *reply;

	/* unmarshals the buffer into the proper request structure */
	void (*request_marshal)(struct evbuffer *, void *);

	/* removes all stored state in the reply */
	void (*reply_clear)(void *);

	/* marshals the reply into a buffer */
	int (*reply_unmarshal)(void *, struct evbuffer*);
};

#define EVRPC_MAKE_REQUEST(name, request, reply, cb, cbarg) \
	evrpc_send_request_##name(pool, request, reply, cb, cbarg)

int evrpc_make_request(struct evrpc_request_wrapper *);

/* 
 * a pool has a number of connections associated with it.
 * rpc requests are always made via a pool.
 */
struct evrpc_pool *evrpc_pool_new();
void evrpc_pool_free(struct evrpc_pool *);
void evrpc_pool_add_connection(struct evrpc_pool *, 
    struct evhttp_connection *);

/*
 * Sets the timeout in secs after which a request has to complete.  The
 * RPC is completely aborted if it does not complete by then.  Setting
 * the timeout to 0 means that it never timeouts and can be used to
 * implement callback type RPCs.
 *
 * Any connection already in the pool will be updated with the new
 * timeout.  Connections added to the pool after set_timeout has be
 * called receive the pool timeout only if no timeout has been set
 * for the connection itself.
 */
void evrpc_pool_set_timeout(struct evrpc_pool *, int timeout_in_secs);

#ifdef __cplusplus
}
#endif

#endif /* _EVRPC_H_ */
