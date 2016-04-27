#include <gtk/gtk.h>
#include "iridescent-map.h"

G_DEFINE_TYPE( IridescentMap, iridescent_map, GTK_TYPE_DRAWING_AREA )

static void iridescent_map_class_init( IridescentMapClass* klass )
    {}

static void iridescent_map_init( IridescentMap* self )
{
    GdkRGBA c;
    GtkWidget *widget;

    gdk_rgba_parse(&c, "blue");
    widget = GTK_WIDGET(self);

    gtk_widget_override_background_color( widget, GTK_STATE_FLAG_NORMAL, &c );
}

