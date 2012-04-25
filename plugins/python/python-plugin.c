/* $Id$ */

/*
 *  (C) Copyright 2004-2005 Leszek Krupi�ski <leafnode@pld-linux.org>
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

#include <sys/types.h>

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <Python.h>

#include <ekg/debug.h>
#include <ekg/plugins.h>

#include <ekg/commands.h>
#include <ekg/dynstuff.h>
#include <ekg/protocol.h>
#include <ekg/stuff.h>
#include <ekg/themes.h>
#include <ekg/userlist.h>
#include <ekg/vars.h>
#include <ekg/xmalloc.h>

#include "python.h"
#include "python-plugin.h"

// * ***************************************************************************
// *
// * plugin object
// *
// * ***************************************************************************

/**
 * ekg_plugin_init()
 *
 * initialization of plugin object
 *
 */

int ekg_plugin_init(ekg_pluginObj *self, PyObject *args, PyObject *kwds)
{
    PyObject * name;
    PyObject * prio;

    static char *kwlist[] = {"name", "prio", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "si", kwlist,
				      &name, &prio))
	return -1;

    self->name = PyString_AsString(name);
    self->prio = (int)PyInt_AsLong(prio);

    return 0;
}

/**
 * ekg_plugin_get_attr()
 *
 * get attribute from plugin object
 *
 */

PyObject *ekg_plugin_get_attr(ekg_pluginObj * self, char * attr)
{
	return Py_FindMethod(ekg_plugin_methods, (PyObject *) self, attr);
}

/**
 * ekg_plugin_dealloc()
 *
 * deallocation of plugin object
 *
 */

void ekg_plugin_dealloc(ekg_pluginObj * o)
{
	xfree(o->name);

}

/**
 * ekg_plugin_load()
 *
 * load plugin
 *
 */

PyObject *ekg_plugin_load(ekg_pluginObj * self, PyObject *args)
{
	int prio;

	if (!PyArg_ParseTuple(args, "i", &prio))
		return NULL;

	debug("[python] Loading plugin '%s' with prio %i\n", self->name, prio);

	if (plugin_find(self->name)) {
		PyErr_SetString(PyExc_RuntimeError, "Plugin already loaded");
		return NULL;
	}
	if (plugin_load(self->name, prio, 0) == -1) {
		Py_RETURN_FALSE;
	} else {
		self->loaded = 1;
		Py_RETURN_TRUE;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

/**
 * ekg_plugin_is_loaded()
 *
 * load plugin
 *
 */

PyObject *ekg_plugin_is_loaded(ekg_pluginObj * self, PyObject *args)
{

	debug("[python] Checking if '%s' plugin is loaded\n", self->name);

	if (plugin_find(self->name)) {
		Py_RETURN_TRUE;
	} else {
		Py_RETURN_FALSE;
	}
}

/**
 * ekg_plugin_unload()
 *
 * unload plugin
 *
 */

PyObject *ekg_plugin_unload(ekg_pluginObj * self, PyObject *args)
{
	debug("[python] Unloading plugin '%s'\n", self->name);

	if (plugin_unload(plugin_find(self->name)) == -1) {
		Py_RETURN_FALSE;
	} else {
		self->loaded = 0;
		Py_RETURN_TRUE;
	}
}

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 * vim: sts=8 sw=8
 */
