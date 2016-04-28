#ifndef __IRIDESCENT_MAP_WIDGET_H__
#define __IRIDESCENT_MAP_WIDGET_H__

#include <gtk/gtk.h>

#define IRIDESCENT_MAP_TYPE                  (iridescent_map_get_type ())
#define IRIDESCENT_MAP(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIDESCENT_MAP_TYPE, IridescentMap))
#define IRIDESCENT_MAP_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST  ((klass), IRIDESCENT_MAP_TYPE, IridescentMapClass))
#define IS_IRIDESCENT_MAP(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIDESCENT_MAP_TYPE))
#define IS_IRIDESCENT_MAP_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE  ((klass), IRIDESCENT_MAP_TYPE))
#define IRIDESCENT_MAP_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS  ((obj), IRIDESCENT_MAP_TYPE, IridescentMapClass))

typedef struct _IridescentMap      IridescentMap;
typedef struct _IridescentMapClass IridescentMapClass;

struct _IridescentMap
{
    GtkDrawingArea parent_instance;

	gpointer privateData;
};

struct _IridescentMapClass
{
    GtkDrawingAreaClass parent_class;
};

GType iridescent_map_get_type(void);

//GtkWidget* iridescent_map_new(void);

#endif //IRIDESCENT_MAP_WIDGET

