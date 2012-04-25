#include "module.h"

MODULE = Ekg2::Session  PACKAGE = Ekg2
PROTOTYPES: ENABLE

void sessions()
PREINIT:
        session_t *s;
PPCODE:
        for (s = sessions; s; s = s->next) {
                XPUSHs(sv_2mortal(bless_session( s )));
        }

Ekg2::Session session_add(char *name)

Ekg2::Session session_find(const char *uid)

Ekg2::Session session_current()
CODE:
	RETVAL = session_current;
OUTPUT:
	RETVAL


#########   Ekg2::Session::Param #######################################################
MODULE = Ekg2::Session::Param	PACKAGE = Ekg2::Session::Param  PREFIX = session_param_

void session_help(Ekg2::Session session, const char *name)

void session_param_help(Ekg2::Session::Param param, Ekg2::Session session)
CODE:
	session_help(session, param->key);
	
int session_param_set(Ekg2::Session::Param param, Ekg2::Session session, const char *value)
CODE:
	RETVAL = session_set(session, param->key, value);
OUTPUT:
        RETVAL

###########  EKG2::Session ##################################################################
MODULE = Ekg2::Session	PACKAGE = Ekg2::Session  PREFIX = session_

Ekg2::Userlist session_userlist(Ekg2::Session session)
CODE:
        RETVAL = (session->userlist);
OUTPUT:
        RETVAL
	
# /session -w session ?
void session_set(Ekg2::Session session)
CODE:
	window_current->session = session;
	session_current = session;

void session_connected_set(Ekg2::Session session, int val)

# TODO think about that &perl_plugin...
int session_param_add(Ekg2::Session session, char *name)
CODE:
	RETVAL = plugin_var_add(&perl_plugin, name, VAR_STR, NULL, 0, NULL);
OUTPUT:
        RETVAL


void session_param_set(Ekg2::Session session, char *name, char *value)
CODE:
	session_set(session, name, value);

int session_disconnect(Ekg2::Session session)
CODE:
	RETVAL = command_exec(NULL, session, "/disconnect", 0);
OUTPUT:
	RETVAL

int session_connect(Ekg2::Session session)
CODE:
	RETVAL = command_exec(NULL, session, "/connect", 0);
OUTPUT:
	RETVAL


	
