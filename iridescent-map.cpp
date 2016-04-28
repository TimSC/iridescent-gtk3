#include <gtk/gtk.h>
#include "iridescent-map.h"
#include <iostream>

#include "iridescent-map/LabelEngine.h"
#include "iridescent-map/Regrouper.h"
#include "iridescent-map/ReadInput.h"
#include "iridescent-map/Transform.h"
#include "iridescent-map/drawlib/drawlibcairo.h"
#include "iridescent-map/MapRender.h"

using namespace std;

typedef map<int, map<int, LabelsByImportance> > OrganisedLabelsMap;

G_DEFINE_TYPE( IridescentMap, iridescent_map, GTK_TYPE_DRAWING_AREA )

static void iridescent_map_init( IridescentMap* self )
{
    GdkRGBA c;
    GtkWidget *widget;
	
	self->userData = (gpointer)1;

    gdk_rgba_parse(&c, "blue");
    widget = GTK_WIDGET(self);

    gtk_widget_override_background_color( widget, GTK_STATE_FLAG_NORMAL, &c );

	// ** Collect labels from off screen tiles
	OrganisedLabelsMap organisedLabelsMap;
	cairo_surface_t *offScreenSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);

	for(int x=2034; x <= 2036; x++)
		for(int y=1373; y<= 1375; y++)
		{
			if(x == 2035 && y == 1374) continue;
			FeatureStore featureStore;
			ReadInput(12, x, y, featureStore);
			class SlippyTilesTransform slippyTilesTransform(12, x, y);

			class DrawLibCairoPango drawlib(offScreenSurface);	
			class MapRender mapRender(&drawlib);
			LabelsByImportance organisedLabels;
			
			mapRender.Render(12, featureStore, false, true, slippyTilesTransform, organisedLabels);

			OrganisedLabelsMap::iterator it = organisedLabelsMap.find(x);
			if(it == organisedLabelsMap.end())
				organisedLabelsMap[x] = map<int, LabelsByImportance>();
			map<int, LabelsByImportance> &col = organisedLabelsMap[x];
			col[y] = organisedLabels;
		}

	cairo_surface_destroy(offScreenSurface);

	// ** Render without labels and collect label info **

	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
	FeatureStore featureStore;
	ReadInput(12, 2035, 1374, featureStore);

	class SlippyTilesTransform slippyTilesTransform(12, 2035, 1374);
	//class SlippyTilesTransform slippyTilesTransform(14, 8143, 5498);

	class DrawLibCairoPango drawlib(surface);	
	class MapRender mapRender(&drawlib);
	LabelsByImportance organisedLabels;
	
	mapRender.Render(12, featureStore, true, true, slippyTilesTransform, organisedLabels);
	organisedLabelsMap[2035][1374] = organisedLabels;

	// ** Render labels **
	RenderLabelList labelList;
	RenderLabelListOffsets labelOffsets;
	for(int y=1373; y<= 1375; y++)
	{
		for(int x=2034; x <= 2036; x++)
		{
			map<int, LabelsByImportance> &col = organisedLabelsMap[x];
			labelList.push_back(col[y]);
			labelOffsets.push_back(std::pair<double, double>(640.0*(x-2035), 640.0*(y-1374)));
		}
	}

	mapRender.RenderLabels(labelList, labelOffsets);

	//cairo_surface_write_to_png(surface, "image.png");	
	cairo_surface_destroy(surface);

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
	IridescentMap *self = IRIDESCENT_MAP(widget);

	GtkAllocation allocation;
	gtk_widget_get_allocation (widget, &allocation);
	cout << (unsigned long)self->userData << endl;

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

