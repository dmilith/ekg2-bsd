#if 0
void userlist_select (session *sess, char *name);
char **userlist_selection_list (GtkWidget *widget, int *num_ret);
GdkPixbuf *get_user_icon (server *serv, struct User *user);
#endif

GtkWidget *userlist_create (GtkWidget *box);
void *userlist_create_model(void);
void userlist_show(window_t *sess);
void userlist_set_value (GtkWidget *treeview, gfloat val);
gfloat userlist_get_value (GtkWidget *treeview);

void fe_userlist_numbers(window_t *sess);
void fe_userlist_clear(window_t *sess);
void fe_userlist_insert(window_t *sess, userlist_t *u, GdkPixbuf **pixmaps);

