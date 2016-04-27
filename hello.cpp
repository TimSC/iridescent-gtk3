#include <gtk/gtk.h>
#include "iridescent-map.h"

static void
activate (GtkApplication* app,
			gpointer user_data)
{
	GtkWidget *window;

	window = gtk_application_window_new (app);
	gtk_window_set_title (GTK_WINDOW (window), "Window");
	gtk_window_set_default_size (GTK_WINDOW (window), 200, 200);

	GtkWidget *button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add (GTK_CONTAINER (window), button_box);

	GtkWidget *test = GTK_WIDGET(g_object_new(IRIDESCENT_MAP_TYPE,NULL));
	gtk_box_pack_start (GTK_BOX(button_box), test, TRUE, TRUE, 0);

	gtk_widget_show_all (window);
}

int
main (int argc,
	char **argv)
{
	GtkApplication *app;
	int status;

	app = gtk_application_new ("org.gtk.example", G_APPLICATION_FLAGS_NONE);
	g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
	status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);

	return status;
}
