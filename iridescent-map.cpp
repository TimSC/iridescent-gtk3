#include <gtk/gtk.h>
#include "iridescent-map.h"
#include <iostream>
using namespace std;

G_DEFINE_TYPE( IridescentMap, iridescent_map, GTK_TYPE_DRAWING_AREA )

static void iridescent_map_init( IridescentMap* self )
{
    GdkRGBA c;
    GtkWidget *widget;

    gdk_rgba_parse(&c, "blue");
    widget = GTK_WIDGET(self);

    gtk_widget_override_background_color( widget, GTK_STATE_FLAG_NORMAL, &c );
}

void iridescent_map_get_preferred_height(GtkWidget *widget,
                                         gint *minimum_height,
                                         gint *natural_height)
{
	*minimum_height = 10;
	*natural_height = 20;
}

void iridescent_map_get_preferred_width(GtkWidget *widget,
                                         gint *minimum_width,
                                         gint *natural_width)
{
	*minimum_width = 50;
	*natural_width = 100;
}

gboolean iridescent_map_draw(GtkWidget *widget,
                                cairo_t *cr)
{
	GtkAllocation allocation;
	gtk_widget_get_allocation (widget, &allocation);

	cairo_save(cr);

	cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0);
	cairo_set_line_width(cr, 5.0);
	cairo_move_to(cr, 0.0, 
				0.0);
	cairo_line_to(cr, allocation.width, 
				allocation.height);

	cairo_stroke(cr);

	cairo_restore(cr);

	return true; //stop other handlers from being invoked for the event
}

static void iridescent_map_class_init( IridescentMapClass* klass )
{
	GtkWidgetClass *widget_class = (GtkWidgetClass*) klass;
	widget_class->get_preferred_height = iridescent_map_get_preferred_height;
	widget_class->get_preferred_width = iridescent_map_get_preferred_width;
	widget_class->draw = iridescent_map_draw;
}

