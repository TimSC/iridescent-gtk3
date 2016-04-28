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
typedef map<int, map<int, cairo_surface_t *> > OrganisedSurfaceMap;

G_DEFINE_TYPE( IridescentMap, iridescent_map, GTK_TYPE_DRAWING_AREA )

class _IridescentMapPrivate
{
public:
	OrganisedLabelsMap organisedLabelsMap;
	OrganisedSurfaceMap organisedSurfaceMap;

	_IridescentMapPrivate()
	{
		// ** Collect labels from off screen tiles	
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

		organisedSurfaceMap[2035] = map<int, cairo_surface_t *>();
		organisedSurfaceMap[2035][1374] = surface;
	}

	virtual ~_IridescentMapPrivate()
	{
		for(OrganisedSurfaceMap::iterator it = organisedSurfaceMap.begin();
			it != organisedSurfaceMap.end(); it++)
		{
			for(map<int, cairo_surface_t *>::iterator it2 = it->second.begin();
				it2 != it->second.end(); it2++)
			{
				cairo_surface_t **surface = &(it2->second);
				if(*surface != NULL)
					cairo_surface_destroy(*surface);
				*surface = NULL;
			}
		}
	}
};

static void iridescent_map_init( IridescentMap* self )
{
    GdkRGBA c;
    GtkWidget *widget;
	
	self->privateData = (gpointer)new class _IridescentMapPrivate();

    gdk_rgba_parse(&c, "blue");
    widget = GTK_WIDGET(self);

    gtk_widget_override_background_color( widget, GTK_STATE_FLAG_NORMAL, &c );

}

void iridescent_map_destroy(GtkWidget *widget)
{
	IridescentMap *self = IRIDESCENT_MAP(widget);
	if(self->privateData != NULL)
		delete (_IridescentMapPrivate *) self->privateData;
	self->privateData = NULL;

	GTK_WIDGET_CLASS (iridescent_map_parent_class)->destroy (widget);
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
	_IridescentMapPrivate *privateData = (_IridescentMapPrivate *)self->privateData;

	GtkAllocation allocation;
	gtk_widget_get_allocation (widget, &allocation);

	cairo_save(cr);
	
	for(OrganisedSurfaceMap::iterator it = privateData->organisedSurfaceMap.begin();
		it != privateData->organisedSurfaceMap.end(); it++)
	{
		int x = it->first;
		for(map<int, cairo_surface_t *>::iterator it2 = it->second.begin();
			it2 != it->second.end(); it2++)
		{
			int y = it2->first;
			cairo_surface_t **surface = &(it2->second);
			cairo_pattern_t *pattern = cairo_pattern_create_for_surface (*surface);
			
			/*if(properties.texx != 0.0 || properties.texy != 0.0)
			{
				cairo_matrix_t mat;
				cairo_matrix_init_translate (&mat, properties.texx, properties.texy);
				cairo_pattern_set_matrix(pattern, &mat);
			}*/

			cairo_set_source (cr,
                      pattern);
			cairo_pattern_destroy (pattern);

			cairo_move_to(cr, 0.0, 
						0.0);
			cairo_line_to(cr, 640, 
						0);
			cairo_line_to(cr, 640, 
						640);
			cairo_line_to(cr, 0, 
						640);
			cairo_fill (cr);

		}
	}

	cairo_restore(cr);

	return true; //stop other handlers from being invoked for the event
}

static void iridescent_map_class_init( IridescentMapClass* klass )
{
	GtkWidgetClass *widget_class = (GtkWidgetClass*) klass;
	widget_class->get_preferred_height = iridescent_map_get_preferred_height;
	widget_class->get_preferred_width = iridescent_map_get_preferred_width;
	widget_class->draw = iridescent_map_draw;
	widget_class->destroy = iridescent_map_destroy;
}

