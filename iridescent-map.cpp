#include <gtk/gtk.h>
#include "iridescent-map.h"
#include <iostream>
#include <cmath>

#include "iridescent-map/LabelEngine.h"
#include "iridescent-map/Regrouper.h"
#include "iridescent-map/ReadInput.h"
#include "iridescent-map/Transform.h"
#include "iridescent-map/drawlib/drawlibcairo.h"
#include "iridescent-map/MapRender.h"

using namespace std;

typedef map<int, map<int, LabelsByImportance> > OrganisedLabelsMap;
typedef map<int, map<int, cairo_surface_t *> > OrganisedSurfaceMap;
typedef std::pair<int, int> IntPair;

G_DEFINE_TYPE( IridescentMap, iridescent_map, GTK_TYPE_DRAWING_AREA )

class _IridescentMapPrivate
{
public:
	OrganisedLabelsMap labelInfoMap;
	OrganisedSurfaceMap organisedShapeMap;
	OrganisedSurfaceMap organisedLabelMap;
	std::map<int, IntPair> pressPos;
	double currentX, currentY;
	double preMoveX, preMoveY;

	_IridescentMapPrivate()
	{
		this->currentX = 2035.0;
		this->currentY = 1374.0;
		this->preMoveX = 0.0;
		this->preMoveY = 0.0;

		for(int x=2034; x <= 2036; x++)
		{
			for(int y=1373; y<= 1375; y++)
			{

				// ** Render without labels and collect label info **

				cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
				FeatureStore featureStore;
				ReadInput(12, x, y, featureStore);

				class SlippyTilesTransform slippyTilesTransform(12, x, y);

				class DrawLibCairoPango drawlib(surface);	
				class MapRender mapRender(&drawlib);
				LabelsByImportance organisedLabels;
	
				mapRender.Render(12, featureStore, true, true, slippyTilesTransform, organisedLabels);
				labelInfoMap[x][y] = organisedLabels;

				organisedShapeMap[x][y] = surface;

			}
		}

		// ** Render labels **

		for(int x=2035; x <= 2035; x++)
		{
			for(int y=1374; y<= 1374; y++)
			{

				RenderLabelList labelList;
				RenderLabelListOffsets labelOffsets;

				for(int y2=1373; y2<= 1375; y2++)
				{
					for(int x2=2034; x2 <= 2036; x2++)
					{
						map<int, LabelsByImportance> &col = labelInfoMap[x2];
						labelList.push_back(col[y2]);
						labelOffsets.push_back(std::pair<double, double>(640.0*(x2-2035), 640.0*(y2-1374)));
					}
				}

				cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
				class DrawLibCairoPango drawlib(surface);	
				class MapRender mapRender(&drawlib);
				mapRender.RenderLabels(labelList, labelOffsets);

				organisedLabelMap[x][y] = surface;
			}
		}


	}

	virtual ~_IridescentMapPrivate()
	{
		for(OrganisedSurfaceMap::iterator it = organisedShapeMap.begin();
			it != organisedShapeMap.end(); it++)
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
	gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK);
	gtk_widget_add_events(widget, GDK_BUTTON_RELEASE_MASK);
	gtk_widget_add_events(widget, GDK_BUTTON_MOTION_MASK);
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
	
	for(OrganisedSurfaceMap::iterator it = privateData->organisedShapeMap.begin();
		it != privateData->organisedShapeMap.end(); it++)
	{
		int x = it->first;
		for(map<int, cairo_surface_t *>::iterator it2 = it->second.begin();
			it2 != it->second.end(); it2++)
		{
			int y = it2->first;
			cairo_surface_t **surface = &(it2->second);
			cairo_pattern_t *pattern = cairo_pattern_create_for_surface (*surface);
			
			double dx = x - privateData->currentX;
			double dy = y - privateData->currentY;
			double px = round(dx * 640.0);
			double py = round(dy * 640.0);

			cairo_matrix_t mat;
			cairo_matrix_init_translate (&mat, -px, -py);
			cairo_pattern_set_matrix(pattern, &mat);

			cairo_set_source (cr,
                      pattern);
			cairo_pattern_destroy (pattern);

			cairo_move_to(cr, px, 
						py);
			cairo_line_to(cr, px + 640, 
						py);
			cairo_line_to(cr, px + 640, 
						py + 640);
			cairo_line_to(cr, px + 0, 
						py + 640);
			cairo_fill (cr);

		}
	}

	for(OrganisedSurfaceMap::iterator it = privateData->organisedLabelMap.begin();
		it != privateData->organisedLabelMap.end(); it++)
	{
		int x = it->first;
		for(map<int, cairo_surface_t *>::iterator it2 = it->second.begin();
			it2 != it->second.end(); it2++)
		{
			int y = it2->first;
			cairo_surface_t **surface = &(it2->second);
			cairo_pattern_t *pattern = cairo_pattern_create_for_surface (*surface);
			
			double dx = x - privateData->currentX;
			double dy = y - privateData->currentY;
			double px = round(dx * 640.0);
			double py = round(dy * 640.0);

			cairo_matrix_t mat;
			cairo_matrix_init_translate (&mat, -px, -py);
			cairo_pattern_set_matrix(pattern, &mat);

			cairo_set_source (cr,
                      pattern);
			cairo_pattern_destroy (pattern);

			cairo_move_to(cr, px, 
						py);
			cairo_line_to(cr, px + 640, 
						py);
			cairo_line_to(cr, px + 640, 
						py + 640);
			cairo_line_to(cr, px + 0, 
						py + 640);
			cairo_fill (cr);

		}
	}

	cairo_restore(cr);

	return true; //stop other handlers from being invoked for the event
}

gboolean iridescent_map_button_press_event (GtkWidget *widget,
				 GdkEventButton *event)
{
	IridescentMap *self = IRIDESCENT_MAP(widget);
	_IridescentMapPrivate *privateData = (_IridescentMapPrivate *)self->privateData;
	privateData->pressPos[event->button] = IntPair(event->x, event->y);

	std::map<int, IntPair>::iterator it = privateData->pressPos.find(1);
	if(it != privateData->pressPos.end())
	{
		privateData->preMoveX = privateData->currentX;
		privateData->preMoveY = privateData->currentY;
	}

	return true;
}

gboolean iridescent_map_button_release_event (GtkWidget *widget,
				 GdkEventButton *event)
{
	IridescentMap *self = IRIDESCENT_MAP(widget);
	_IridescentMapPrivate *privateData = (_IridescentMapPrivate *)self->privateData;

	std::map<int, IntPair>::iterator it = privateData->pressPos.find(event->button);
	if(it != privateData->pressPos.end())
		privateData->pressPos.erase(it);
	return true;
}

gboolean iridescent_map_motion_notify_event (GtkWidget *widget,
					 GdkEventMotion *event)
{
	IridescentMap *self = IRIDESCENT_MAP(widget);
	_IridescentMapPrivate *privateData = (_IridescentMapPrivate *)self->privateData;

	std::map<int, IntPair>::iterator it = privateData->pressPos.find(1);
	if(it != privateData->pressPos.end())
	{
		IntPair &startPos = it->second;
		double dx = event->x - startPos.first;
		double dy = event->y - startPos.second;
		privateData->currentX = privateData->preMoveX - dx / 640.0;
		privateData->currentY = privateData->preMoveY - dy / 640.0;
		gtk_widget_queue_draw (widget);
	}
	return true;
}

static void iridescent_map_class_init( IridescentMapClass* klass )
{
	GtkWidgetClass *widget_class = (GtkWidgetClass*) klass;
	widget_class->get_preferred_height = iridescent_map_get_preferred_height;
	widget_class->get_preferred_width = iridescent_map_get_preferred_width;
	widget_class->draw = iridescent_map_draw;
	widget_class->destroy = iridescent_map_destroy;
	widget_class->button_press_event = iridescent_map_button_press_event;
	widget_class->button_release_event = iridescent_map_button_release_event;
	widget_class->motion_notify_event = iridescent_map_motion_notify_event;
}

