#ifndef EKG_SCRIPTS_H
#define EKG_SCRIPTS_H
#include <sys/types.h>

#include "commands.h"
#include "plugins.h"
#include "protocol.h"
#include "stuff.h"
#include "vars.h"
#include "queries.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCRIPT_HANDLE_UNBIND	-666
#define MAX_ARGS QUERY_ARGS_MAX+1

typedef enum {
	SCRIPT_UNKNOWNTYPE,
	SCRIPT_VARTYPE,
	SCRIPT_COMMANDTYPE,
	SCRIPT_QUERYTYPE,
	SCRIPT_TIMERTYPE,
	SCRIPT_WATCHTYPE,
	SCRIPT_PLUGINTYPE, 
} script_type_t;

typedef struct script {
	struct script	*next;

	void		*lang;
	char		*name;
	char		*path;
	void		*priv_data;
	int		inited;
} script_t;
extern script_t		*scripts;

typedef struct {
	script_t	*scr;
	struct timer	*self;
	int		removed;
	void		*priv_data;
} script_timer_t; 

typedef struct {
	script_t	*scr;
	plugin_t	*self;
	void		*priv_data;
} script_plugin_t;

typedef struct {
	script_t	*scr;
	variable_t	*self;

	char		*name;
	char		*value;
	void		*priv_data;
} script_var_t; 

typedef struct {
	script_t	*scr;
	query_t		*self;
	int		argc;
	int		argv_type[MAX_ARGS];

	int		real_argc;
	void		*priv_data;
	int		hack;
} script_query_t; 

typedef struct {
	script_t	*scr;
	command_t	*self;
	void		*priv_data; 
} script_command_t;

typedef struct {
	script_t	*scr;
	watch_t		*self; 
	int		removed;
	void		*data;
	void		*priv_data;
} script_watch_t;

typedef int (scriptlang_initialize_t)();
typedef int (scriptlang_finalize_t)();
typedef int (script_load_t)(script_t *);
typedef int (script_unload_t)(script_t *);
typedef int (script_handler_command_t)(script_t *, script_command_t *, char **);
typedef int (script_handler_timer_t)  (script_t *, script_timer_t *, int);
typedef int (script_handler_var_t)    (script_t *, script_var_t *,   char *);
typedef int (script_handler_query_t)  (script_t *, script_query_t *, void **);
typedef int (script_handler_watch_t)  (script_t *, script_watch_t *, int, int, int);

typedef int (script_free_bind_t)      (script_t *, void *, int, void *, ...);

typedef struct scriptlang {
	struct scriptlang	*next;

	char	 *name;		/* perl, python, php *g* and so on. */
	char	 *ext;		/*  .pl,    .py, .php ... */
	plugin_t *plugin;

	scriptlang_initialize_t *init;
	scriptlang_finalize_t	*deinit;

	script_load_t		*script_load;
	script_unload_t		*script_unload;
	script_free_bind_t	*script_free_bind;

	script_handler_query_t	*script_handler_query;
	script_handler_command_t*script_handler_command;
	script_handler_timer_t	*script_handler_timer;
	script_handler_var_t	*script_handler_var;
	script_handler_watch_t	*script_handler_watch;
	
	void *priv_data;
} scriptlang_t;
extern scriptlang_t *scriptlang;

#define SCRIPT_FINDER(bool)\
	script_t *scr = NULL;\
	scriptlang_t *slang = NULL;\
	\
	for (scr = scripts; scr; scr = scr->next) {\
		slang = scr->lang;\
		if (bool)\
			return scr;\
	}\
	return NULL;

/* XXX: Split *_watches() into normal and line-ones.
 *
 *	Until then we abort the build if sizeof(void*) > sizeof(long int), as
 *	in that case the pointer would get corrupted when being passed around
 *	as a long using type-punning.
 *
 *	This trick was stolen from the Linux kernel. See
 *	http://scaryreasoner.wordpress.com/2009/02/28/checking-sizeof-at-compile-time/
 *	for an explanation.
 */
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#define SCRIPT_DEFINE(x, y)\
	extern int x##_load(script_t *);\
	extern int x##_unload(script_t *);\
	\
	extern int x##_commands(script_t *, script_command_t *, char **);\
	extern int x##_timers(script_t *, script_timer_t *, int );\
	extern int x##_variable_changed(script_t *, script_var_t *, char *);\
	extern int x##_query(script_t *, script_query_t *, void **);\
	extern int x##_watches(script_t *, script_watch_t *, int, int, long int);\
	void x##_dummy_sanity_check() { BUILD_BUG_ON(sizeof(void *) > sizeof(long)); };\
	\
	extern int x##_bind_free(script_t *, void *, int type, void *, ...);\
	\
	scriptlang_t x##_lang = { \
		name: #x, \
		plugin: &x##_plugin, \
		ext: y, \
\
		init: x##_initialize,\
		deinit: x##_finalize, \
\
		script_load: x##_load, \
		script_unload: x##_unload, \
		script_free_bind: x##_bind_free,\
\
		script_handler_query  : x##_query,\
		script_handler_command: x##_commands,\
		script_handler_timer  : x##_timers,\
		script_handler_var    : x##_variable_changed,\
		script_handler_watch  : x##_watches,\
	}

#define script_private_get(s) (s->priv_data)
#define script_private_set(s, p) (s->priv_data = p)

#ifndef EKG2_WIN32_NOFUNCTION
int script_unload_lang(scriptlang_t *s);

int script_list(scriptlang_t *s);
int script_unload_name(scriptlang_t *s, char *name);
int script_load(scriptlang_t *s, char *name);

int scriptlang_register(scriptlang_t *s);
int scriptlang_unregister(scriptlang_t *s);

int scripts_init();

script_t *script_find(scriptlang_t *s, char *name);

int script_query_unbind(script_query_t *squery, int from);
int script_command_unbind(script_command_t *scr_comm, int free);
int script_timer_unbind(script_timer_t *stimer, int free);
int script_var_unbind(script_var_t *data, int free);
int script_watch_unbind(script_watch_t *temp, int free);

script_command_t *script_command_bind(scriptlang_t *s, script_t *scr, char *command, char *params, char *possibilities, void *handler);
script_timer_t *script_timer_bind(scriptlang_t *s, script_t *scr, int freq, void *handler);
script_query_t *script_query_bind(scriptlang_t *s, script_t *scr, char *qname, void *handler);
script_var_t *script_var_add(scriptlang_t *s, script_t *scr, char *name, char *value, void *handler);
script_watch_t *script_watch_add(scriptlang_t *s, script_t *scr, int fd, int type, void *handler, void *data);
script_plugin_t *script_plugin_init(scriptlang_t *s, script_t *scr, char *name, plugin_class_t pclass, void *handler);

int script_variables_free(int free);
int script_variables_write();
#endif

#define SCRIPT_UNBIND_HANDLER(type, args...) \
{\
	SCRIPT_HANDLER_HEADER(script_free_bind_t);\
	SCRIPT_HANDLER_FOOTER(script_free_bind, type, temp->priv_data, args);\
}

/* BINDING && UNBINDING */

#define SCRIPT_BIND_HEADER(x) \
	x *temp; \
	if (scr && scr->inited != 1) {\
		debug_error("[script_bind_error] script not inited!\n");	\
		return NULL;	\
	}	\
	temp = xmalloc(sizeof(x)); \
	temp->scr  = scr;\
	temp->priv_data = handler;

#define SCRIPT_BIND_FOOTER(y) \
	if (!temp->self) {\
		debug("[script_bind_error] (before adding to %s) ERROR! retcode = 0x%x\n", #y, temp->self);\
		xfree(temp);\
		return NULL;\
	}\
	list_add(&y, temp);\
	return temp;

/* HANDLERS */
#define SCRIPT_HANDLER_HEADER(x) \
	script_t	*_scr;\
	scriptlang_t	*_slang;\
	x		*_handler;\
	int		ret = SCRIPT_HANDLE_UNBIND;

/* TODO: quietmode */
#define SCRIPT_HANDLER_FOOTER(y, _args...) \
	if ((_scr = temp->scr) && ((_slang = _scr->lang)))  _handler = _slang->y;\
	else						    _handler = temp->priv_data;\
	if (_handler)\
		ret = _handler(_scr, temp, _args); \
	else {\
		debug("[%s] (_handler == NULL)\n", #y);\
		ret = SCRIPT_HANDLE_UNBIND;\
	}\
	\
	if (ret == SCRIPT_HANDLE_UNBIND) { debug("[%s] script or scriptlang want to delete this handler\n", #y); }\
	if (ret == SCRIPT_HANDLE_UNBIND)

#define SCRIPT_HANDLER_MULTI_FOOTER(y, _args...)\
/* foreach y->list->next do SCRIPT_HANDLER_FOOTER(y->list->data, _args); */\
	SCRIPT_HANDLER_FOOTER(y, _args)

#ifdef __cplusplus
}
#endif

#endif
