/* $Id$ */

/*
 *  (C) Copyright 2004-2005 Leszek Krupi�ski <leafnode@pld-linux.org>
 *			    Jakub Zawadzki <darkjames@darkjames.ath.cx>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ekg2-config.h"

#include "python.h"
#include "python-ekg.h"
#include "python-config.h"

#include <sys/types.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <Python.h>
#include <compile.h>
#include <node.h>

#include <ekg/commands.h>
#include <ekg/debug.h>
#include <ekg/plugins.h>
#include <ekg/scripts.h>
#include <ekg/xmalloc.h>

#include <ekg/dynstuff.h>
#include <ekg/protocol.h>
#include <ekg/stuff.h>
#include <ekg/themes.h>
#include <ekg/userlist.h>
#include <ekg/vars.h>

#include <ekg/queries.h>

/**
 * python_plugin
 *
 * plugin definition
 */

PLUGIN_DEFINE(python, PLUGIN_SCRIPTING, NULL);
SCRIPT_DEFINE(python, ".py");

// * ***************************************************************************
// *
// * Polecenia EKG
// *
// * ***************************************************************************

/**
 * python_command_eval()
 *
 * execute python code
 *
 */

COMMAND(python_command_eval)
{
	python_exec(params[0]);
	return 0;
}

/**
 * python_command_run()
 *
 * run single python script
 *
 */

COMMAND(python_command_run)
{
	python_run(params[0]);
	return 0;
}

/**
 * python_command_load()
 *
 * load python script
 *
 */

COMMAND(python_command_load)
{
	script_load(&python_lang, (char *) params[0]);
	return 0;
}

/**
 * python_command_unload()
 *
 * unload python script
 *
 */

COMMAND(python_command_unload)
{
	script_unload_name(&python_lang, (char *) params[0]);
	return 0;
}

/**
 * python_command_list()
 *
 * list loaded python scripts
 *
 */

COMMAND(python_command_list)
{
	script_list(&python_lang);
	return 0;
}

// * ***************************************************************************
// *
// * Hooki
// *
// * ***************************************************************************

QUERY(python_print_version)
{
	print("generic", "Python plugin for ekg2 running under Python " PY_VERSION);
	return 0;
}

int python_bind_free(script_t *scr, void *data /* niby to jest ale kiedys nie bedzie.. nie uzywac */, int type, void *priv_data, ...)
{
	PyObject *handler = priv_data;
	switch (type) {
		case(SCRIPT_QUERYTYPE):
		case(SCRIPT_COMMANDTYPE):
		case(SCRIPT_TIMERTYPE):
		    Py_XDECREF(handler);
		    break;
		case(SCRIPT_WATCHTYPE):
		    Py_XDECREF(handler);
		case(SCRIPT_VARTYPE):
		    Py_XDECREF(handler);
		    break;
	}
	return 0;
}

int python_variable_changed(script_t *scr, script_var_t *scr_var, char *newval)
{
	int python_handle_result;
	PyObject *obj = (PyObject *)scr_var->priv_data;
	PYTHON_HANDLE_HEADER(obj, Py_BuildValue("(ss)", scr_var->name, newval))
	PYTHON_HANDLE_FOOTER()
	return python_handle_result;
}

int python_watches(script_t *scr, script_watch_t *scr_wat, int type, int fd, long int watch)
{
	int python_handle_result;
	PyObject * args;

	PyObject *obj = (PyObject *)scr_wat->priv_data;
	if (scr_wat->self->buf) {
		args = Py_BuildValue("(Ois)", (PyObject *)scr_wat->data, type, (char *)watch);
	} else {
		args = Py_BuildValue("(Oii)", (PyObject *)scr_wat->data, type, watch);
	}
	PYTHON_HANDLE_HEADER(obj, args)
	PYTHON_HANDLE_FOOTER()
	return python_handle_result;
}

int python_timers(script_t *scr, script_timer_t *time, int type)
{
	int python_handle_result;
	PyObject *obj = (PyObject *)time->priv_data;
	PYTHON_HANDLE_HEADER(obj, Py_BuildValue("()"))
	PYTHON_HANDLE_FOOTER()
	return python_handle_result;
}

int python_commands(script_t *scr, script_command_t *comm, char **params)
{
	int python_handle_result;
	PyObject *obj = (PyObject *)comm->priv_data;
	PYTHON_HANDLE_HEADER(obj, Py_BuildValue("(ss)", comm->self->name, params[0] ? params[0] : "")) 
	PYTHON_HANDLE_FOOTER()
	return python_handle_result;
}

/**
 * python_protocol_message_query()
 *
 * handle signals
 *
 */

int python_query(script_t *scr, script_query_t *scr_que, void **args)
{
	int python_handle_result;
	PyObject *argz;
	int i;
	if (!(argz = PyTuple_New(scr_que->argc)))
		return 1;
	for (i=0; i < scr_que->argc; i++) {
		PyObject *w = NULL;
		switch ( scr_que->argv_type[i] ) {
			case (QUERY_ARG_INT):
				w = PyInt_FromLong((long) *(int *) args[i] );
				break;
			case (QUERY_ARG_CHARP): {
				char *tmp = *(char **) args[i];
				if (tmp)
					w = PyString_FromString(tmp);
				break;
			}
			case (QUERY_ARG_CHARPP): {
				char *tmp = array_join((* (char ***) args[i]), " ");
				w = PyString_FromString(tmp); /* CHECK: xstrdup ? */
				xfree(tmp);
				break;
			}
			default:
			       debug("[NIMP] %s %d %d\n", __(query_name(scr_que->self->id)), i, scr_que->argv_type[i]);
		}
		if (!w) {
			Py_INCREF(Py_None);
			w = Py_None;
		}
		PyTuple_SetItem(argz, i, w);
	}
	PYTHON_HANDLE_HEADER(scr_que->priv_data, argz)
	if (__py_r && PyTuple_Check(__py_r)) { /* __py_r - return value */
		for (i=0; i < scr_que->argc; i++) {
			PyObject *w = PyTuple_GetItem(__py_r, i);
			switch (scr_que->argv_type[i]) {
				case (QUERY_ARG_INT):
					if (PyInt_Check(w)) *( (int *) args[i]) = PyInt_AsLong(w);
					else debug("[recvback,script error] not int ?!\n");
					break;
				case (QUERY_ARG_CHARP):
					if (PyString_Check(w)) {
						if (xstrcmp(PyString_AsString(w), *(char **) args[i])) {	/* XXX, hack for broken query_emit's */
							xfree(*(char **) args[i]);
							*( (char **) args[i]) = xstrdup(PyString_AsString(w));
						}
					} else debug("[recvback,script error] not string?!\n");
					break;
				case (QUERY_ARG_CHARPP): /* wazne, zrobic. */
				default:
					debug("[NIMP, recvback] %d %d -> 0x%x\n", i, scr_que->argv_type[i], w);
			}
		}
		python_handle_result = 1;
	}
	PYTHON_HANDLE_FOOTER()
	if (!python_handle_result) return -1;
	else return 0;
}

// ********************************************************************************
// *
// * Funkcje pomocnicze
// *
// ********************************************************************************

/**
 * python_exec()
 *
 * run python code
 *
 *  - command - code to run
 *
 */

int python_exec(const char *command)
{
	debug("[python] Running command: %s\n", command);
	char *tmp;

	if (!command)
		return 0;

	tmp = saprintf("import ekg\n%s\n", command);

	if (PyRun_SimpleString(tmp) == -1) {
		print("script_eval_error");
		debug("[python] script evaluation failed\n");
	}
	xfree(tmp);

	return 0;
}

/**
 * python_run()
 *
 * run python script from file
 *
 * - filename - path to file to run
 *
 */

int python_run(const char *filename)
{
	FILE *f = fopen(filename, "r");

	if (!f) {
		print("script_not_found", filename);
		return -1;
	}

	PyRun_SimpleFile(f, (char*) filename);
	fclose(f);

	return 0;
}

/*
 * python_get_func()
 *
 * return function from module
 *
 */

PyObject *python_get_func(PyObject *module, const char *name)
{
	PyObject *result = PyObject_GetAttrString(module, (char*) name);

	if (result && !PyCallable_Check(result)) {
		Py_XDECREF(result);
		result = NULL;
	}

	return result;
}

script_t *python_find_script(PyObject *module)
{
	return script_find(&python_lang, PyString_AsString(module));
}

/* returns somethink like it after formatink.
   14:24:58 ::: B��d EOL while scanning single-quoted string (sample.py, line 97) (exceptions.SyntaxError) @
   14:24:58 /home/darkjames/.ekg2/python/scripts/autorun/sample.py:97
 or:
   14:25:38 ::: B��d global name 'a' is not defined (exceptions.NameError) @ /home/darkjames/.ekg2/python/scripts/autorun/sample.py 
 and/or:
   14:25:57 ::: B��d integer division or modulo by zero (exceptions.ZeroDivisionError) @ /home/darkjames/.ekg2/python/scripts/autorun/sample.py
   14:39:31   File "/home/darkjames/.ekg2/python/scripts/autorun/sample.py", line 95, in aaa
   14:39:31	0 / 0;
 or:
   23:13:54 ::: B��d invalid syntax (sample.py, line 84) (exceptions.SyntaxError) @ /home/darkjames/.ekg2/python/scripts/autorun/sample.py:84
 */
char *python_geterror(script_t *s) {
	PyObject *exception, *v, *tb, *hook;
	PyObject *pName;
	PyObject *pModule;
	string_t str;

	PyErr_Fetch(&exception, &v, &tb);
	if (!v)
		return xstrdup("noexception after PyErr_Fetch");
	PyErr_NormalizeException(&exception, &v, &tb);
	if (!v) /* TODO: check. */
		return xstrdup("noexception after PyErr_NormalizeException");

	str = string_init("");

	if ((hook = PyObject_Str(v))) {
		string_append(str, PyString_AsString(hook));
		string_append(str, " (");
		Py_DECREF(hook);
	}

	if ((hook = PyObject_Str(exception))) {
		string_append(str, PyString_AsString(hook));
		string_append(str, ") @ ");
		Py_DECREF(hook);
	} else string_append(str, "?) @ ");

	string_append(str, s->path);

	if ((hook = PyObject_GetAttrString(v, "lineno"))) {
		string_append_c(str, ':');
		string_append(str, itoa(PyInt_AsLong(hook)));
		Py_DECREF(hook);
	} 
	string_append_c(str, '\n');

/* traceback */
	pName = PyString_FromString("traceback");
	if (tb && exception && (pModule = PyImport_Import(pName))) {
		PyObject *pDict = PyModule_GetDict(pModule);
		PyObject *pFunc = PyDict_GetItemString(pDict, "format_tb");
		if (pFunc && PyCallable_Check(pFunc)) {
			PyObject *pArgs = PyTuple_New(1);
			PyObject *pValue;

			PyTuple_SetItem(pArgs, 0, tb);
			pValue = PyObject_CallObject(pFunc, pArgs);
			if (pValue) {
				int len = PyList_Size(pValue);
				if (len > 0) {
					PyObject *t,*tt;
					char *buffer;
					int i;
					for (i = 0; i < len; i++) {
						tt = PyList_GetItem(pValue,i);
						t = Py_BuildValue("(O)",tt);
						PyArg_ParseTuple(t,"s",&buffer);
						string_append(str, buffer);
						if (i+1 != len)
							string_append_c(str, '\n');
					}
				}
			}
			Py_DECREF(pValue);
			Py_DECREF(pArgs);
		}
		Py_DECREF(pModule);
	}
	Py_DECREF(pName);
	Py_DECREF(v);

	PyErr_Clear();
/*	PyErr_Restore(exception, v, tb);  */
	return string_free(str, 0);
}

/*
 * python_load()
 *
 * load script with given details
 *
 *  - s - script_t * struct
 *
 */
int python_load(script_t *s)
{
	PyObject *init, *module = NULL;
	FILE *fp = fopen(s->path, "rb"); 
	node *n;
	if (fp && (n = PyParser_SimpleParseFile(fp, s->path, Py_file_input))) {
		PyCodeObject *co;

	/* Python 2.5 doesn't have PyNode_CompileFlags() */
		if ((co = PyNode_Compile(n, s->path)))
			module = PyImport_ExecCodeModuleEx(s->name, (PyObject *)co, s->path);
		PyNode_Free(n);
		fclose(fp);
	}
	if (!module) {
		char *err = python_geterror(s);
		print("script_error", err);
		xfree(err);
		return -1;
	}
	debug("[python script loading] 0x%x\n", module);
	if ((init = python_get_func(module, "init"))) {
		PyObject *result = PyObject_CallFunction(init, "()");
		if (result) {
			int resulti = PyInt_AsLong(result);
			if (!resulti) {

			}
			Py_XDECREF(result);
		}
		Py_XDECREF(init);
	}
	script_private_set(s, module);
	PyErr_Clear();
	return 1;
}

/*
 * python_unload()
 *
 * remove script from memory
 *
 * 0/-1
 */
int python_unload(script_t *s)
{
	PyObject	 *module = python_module(s);
	PyObject	 *obj;

	if (!module)
		return 0;
#if 0
	if ((obj = python_get_func(module, "deinit"))) {
		PyObject *res = PyObject_CallFunction(obj, "()");
		Py_XDECREF(res);
		Py_XDECREF(obj);
	}
Breakpoint 2, python_finalize () at python.c:632
632		Py_Finalize();
(gdb) step

Program received signal SIGABRT, Aborted.
0xb7e08921 in kill () from /lib/libc.so.6
(gdb)

without that works ? wtf ?!
#endif

	Py_XDECREF(module);
	script_private_set(s, NULL);
	return 0;
}

// ********************************************************************************
// *
// * Interpreter related functions
// *
// ********************************************************************************

/**
 * python_initialize()
 *
 * initialize interpreter
 *
 */

int python_initialize()
{
	PyObject *ekg, *ekg_config;
	Py_Initialize();

	PyImport_AddModule("ekg");
	if (!(ekg = Py_InitModule("ekg", ekg_methods)))
		return -1;

	ekg_config = PyObject_NEW(PyObject, &ekg_config_type);
	PyModule_AddObject(ekg, "config", ekg_config);

	// Const - general
	PyModule_AddStringConstant(ekg, "VERSION", VERSION);

	// Const - message types
	PyModule_AddIntConstant(ekg, "MSGCLASS_MESSAGE",	EKG_MSGCLASS_MESSAGE);
	PyModule_AddIntConstant(ekg, "MSGCLASS_CHAT",		EKG_MSGCLASS_CHAT);
	PyModule_AddIntConstant(ekg, "MSGCLASS_SENT",		EKG_MSGCLASS_SENT);
	PyModule_AddIntConstant(ekg, "MSGCLASS_SENT_CHAT",	EKG_MSGCLASS_SENT_CHAT);
	PyModule_AddIntConstant(ekg, "MSGCLASS_SYSTEM",		EKG_MSGCLASS_SYSTEM);

	// Const - status types
	/* XXX, someone take a look at it? */
	PyModule_AddStringConstant(ekg, "STATUS_NA",		(char *) ekg_status_string(EKG_STATUS_NA, 0));
	PyModule_AddStringConstant(ekg, "STATUS_AVAIL",		(char *) ekg_status_string(EKG_STATUS_AVAIL, 0));
	PyModule_AddStringConstant(ekg, "STATUS_AWAY",		(char *) ekg_status_string(EKG_STATUS_AWAY, 0));
	PyModule_AddStringConstant(ekg, "STATUS_AUTOAWAY",	(char *) ekg_status_string(EKG_STATUS_AUTOAWAY, 0));
	PyModule_AddStringConstant(ekg, "STATUS_INVISIBLE",	(char *) ekg_status_string(EKG_STATUS_INVISIBLE, 0));
	PyModule_AddStringConstant(ekg, "STATUS_XA",		(char *) ekg_status_string(EKG_STATUS_XA, 0));
	PyModule_AddStringConstant(ekg, "STATUS_DND",		(char *) ekg_status_string(EKG_STATUS_DND, 0));
	PyModule_AddStringConstant(ekg, "STATUS_FREE_FOR_CHAT",	(char *) ekg_status_string(EKG_STATUS_FFC, 0));
	PyModule_AddStringConstant(ekg, "STATUS_BLOCKED",	(char *) ekg_status_string(EKG_STATUS_BLOCKED, 0));
	PyModule_AddStringConstant(ekg, "STATUS_UNKNOWN",	(char *) ekg_status_string(EKG_STATUS_UNKNOWN, 0));
	PyModule_AddStringConstant(ekg, "STATUS_ERROR",		(char *) ekg_status_string(EKG_STATUS_ERROR, 0));

	// Const - ignore levels
	PyModule_AddIntConstant(ekg, "IGNORE_STATUS",		IGNORE_STATUS);
	PyModule_AddIntConstant(ekg, "IGNORE_STATUS_DESCR",	IGNORE_STATUS_DESCR);
	PyModule_AddIntConstant(ekg, "IGNORE_MSG",		IGNORE_MSG);
	PyModule_AddIntConstant(ekg, "IGNORE_DCC",		IGNORE_DCC);
	PyModule_AddIntConstant(ekg, "IGNORE_EVENTS",		IGNORE_EVENTS);
	PyModule_AddIntConstant(ekg, "IGNORE_NOTIFY",		IGNORE_NOTIFY);
	PyModule_AddIntConstant(ekg, "IGNORE_XOSD",		IGNORE_XOSD);
	PyModule_AddIntConstant(ekg, "IGNORE_ALL",		IGNORE_ALL);

	// Const - watch types
	PyModule_AddIntConstant(ekg, "WATCH_READ",		WATCH_READ);
	PyModule_AddIntConstant(ekg, "WATCH_READ_LINE",		WATCH_READ_LINE);
	PyModule_AddIntConstant(ekg, "WATCH_WRITE",		WATCH_WRITE);


	return 0;
}

/**
 * python_finalize()
 *
 * clean interpreter, unload modules, scripts etc.
 *
 */

int python_finalize()
{
	Py_Finalize();
	return 0;
}

// ********************************************************************************
// *
// * Plugin support functions
// *
// ********************************************************************************

/**
 * python_plugin_destroy()
 *
 * remove plugin
 *
 */

static int python_plugin_destroy()
{
	scriptlang_unregister(&python_lang);
	plugin_unregister(&python_plugin);
	return 0;
}

/**
 * python_plugin_init()
 *
 * inicjalizacja pluginu
 *
 */

int python_plugin_init(int prio)
{
	PLUGIN_CHECK_VER("python");

	plugin_register(&python_plugin, prio);

	scriptlang_register(&python_lang);
	command_add(&python_plugin, ("python:eval"),   ("!"),	python_command_eval,   COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&python_plugin, ("python:run"),    ("!"),	python_command_run,    COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&python_plugin, ("python:load"),   ("!"),	python_command_load,   COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&python_plugin, ("python:unload"), ("!"),	python_command_unload, COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&python_plugin, ("python:list"),   NULL,	python_command_list,   0, NULL);
	query_connect_id(&python_plugin, PLUGIN_PRINT_VERSION, python_print_version, NULL);

	return 0;
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 * vim: noet
 */
