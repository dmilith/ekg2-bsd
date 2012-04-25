/*
 * tlen_handle_notification()
 *
 *
 */

JABBER_HANDLER(tlen_handle_notification) {	/* n->name: "m" TLEN only: typing, nottyping, and alert notifications */
	char *type = jabber_attr(n->atts, "tp");
	char *from = jabber_attr(n->atts, "f");
	char *typeadd = jabber_attr(n->atts, "type");


	if (!type || !from || (typeadd && !xstrcmp(typeadd, "error"))) {
		debug_error("tlen_handle() %d %s/%s/%s", __LINE__, type, from, typeadd);
		return;
	}

	if (!xstrcmp(type, "t") || !xstrcmp(type, "u")) {
		char *uid = tlen_uid(from);

		/* typing notification */
		if (!xstrcmp(type, "u"))
			protocol_xstate_emit(s, uid, 0, EKG_XSTATE_TYPING);
		else
			protocol_xstate_emit(s, uid, EKG_XSTATE_TYPING, 0);
		xfree(uid);
		return;
	}

	if (!xstrcmp(type, "a")) {	/* funny thing called alert */
		char *uid = tlen_uid(from);
		print_info(uid, s, "tlen_alert", session_name(s), format_user(s, uid));

		if (config_sound_notify_file)
			play_sound(config_sound_notify_file);
		else if (config_beep && config_beep_notify)
			query_emit_id(NULL, UI_BEEP, NULL);
		xfree(uid);
		return;
	}

}

/*
 * tlen_handle_newmail()
 *
 */

JABBER_HANDLER(tlen_handle_newmail) {
	char *from = tlen_decode(jabber_attr(n->atts, "f"));
	char *subj = tlen_decode(jabber_attr(n->atts, "s"));

	print("tlen_mail", session_name(s), from, subj);
	newmail_common(s);

	xfree(from);
	xfree(subj);
}

/*
 * tlen_handle_webmessage()
 * 
 *
 */

JABBER_HANDLER(tlen_handle_webmessage) {
	char *from = jabber_attr(n->atts, "f");
	char *mail = jabber_attr(n->atts, "e");
	char *content = n->data;
	string_t body = string_init("");

	char *text;

	if (from || mail) {
		string_append(body, "From:");
		if (from) {
			string_append_c(body, ' ');
			string_append(body, from);
		}
		if (mail) {
			string_append(body, " <");
			string_append(body, mail);
			string_append_c(body, '>');
		}
		string_append_c(body, '\n');
	}

	if (body->len) string_append_c(body, '\n');

	string_append(body, content);
	text = tlen_decode(body->str);
	string_free(body, 1);

	protocol_message_emit(s, "ludzie.tlen.pl", NULL, text, NULL, time(NULL), EKG_MSGCLASS_MESSAGE, NULL, EKG_TRY_BEEP, 0);

	xfree(text);
}

static const struct jabber_generic_handler tlen_handlers[] = {
	{ "m",	tlen_handle_notification },
	{ "n",	tlen_handle_newmail },
	{ "w",	tlen_handle_webmessage },
	{ NULL,	NULL }
};

// vim:syn=c
