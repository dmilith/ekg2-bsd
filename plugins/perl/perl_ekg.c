/*
 *  (C) Copyright 2005 Jakub Zawadzki <darkjames@darkjames.ath.cx>
 *                2010 Wies�aw Ochmi�ski <wiechu@wiechu.com>
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

#ifndef __FreeBSD__
#define _XOPEN_SOURCE 600
#define __EXTENSIONS__
#endif

#include "ekg2-config.h"

#include <sys/types.h>
#include <stdlib.h>

#include <ekg/plugins.h>
#include <ekg/scripts.h>

#include <ekg/commands.h>
#include <ekg/themes.h>
#include <ekg/vars.h>
#include <ekg/xmalloc.h>
#undef _

// #include "perl_ekg.h"
#include "perl_core.h"

int perl_theme_init();
int auto_load;

PLUGIN_DEFINE(perl, PLUGIN_SCRIPTING, NULL);
SCRIPT_DEFINE(perl, ".pl");

COMMAND(perl_command_list)
{
	return script_list(&perl_lang);
}

COMMAND(perl_command_eval)
{
	char *code = saprintf("use Ekg2; %s", params[0]);
	
	eval_pv(code, TRUE);
	xfree(code);
	
	return 0;
}

COMMAND(perl_command_test)
{
	char *code = saprintf("use Ekg2;\nuse Ekg2::%s Ekg2::debug(\"%s\n\");", params[0], params[1]);
	
	eval_pv(code, TRUE);
	xfree(code);
	
	return 0;
}

COMMAND(perl_command_unload)
{
	return script_unload_name(&perl_lang, (char *) params[0]);
}

COMMAND(perl_command_load)
{
	return script_load(&perl_lang, (char *) params[0]);
}

int perl_plugin_destroy()
{
	scriptlang_unregister(&perl_lang);
	plugin_unregister(&perl_plugin);
	return 0;
}

int perl_plugin_init(int prio)
{
	auto_load = 1;

	PLUGIN_CHECK_VER("perl");

	plugin_register(&perl_plugin, prio);
	scriptlang_register(&perl_lang);
/* TODO
 *	command_add(&perl_plugin, "perl:eval",	 "!",  perl_command_eval,   COMMAND_ENABLEREQPARAMS, NULL);
 *	command_add(&perl_plugin, "perl:test",	 "!",  perl_command_test,   COMMAND_ENABLEREQPARAMS, NULL);
 *	command_add(&perl_plugin, "perl:run",	 "?",  perl_command_run,    0, NULL);
 */
	command_add(&perl_plugin, ("perl:load"),   ("!"),  perl_command_load,	COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&perl_plugin, ("perl:unload"), ("!"),  perl_command_unload, COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&perl_plugin, ("perl:list"),  NULL,  perl_command_list,   0, NULL);

	variable_add(&perl_plugin, ("autoload"), VAR_BOOL, 1, &auto_load, NULL, NULL, NULL);

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

