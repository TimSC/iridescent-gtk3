#include <gtk/gtk.h>
#include "iridescent-map.h"
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <fstream>

#include "iridescent-map/LabelEngine.h"
#include "iridescent-map/Regrouper.h"
#include "iridescent-map/ReadInput.h"
#include "iridescent-map/Transform.h"
#include "iridescent-map/drawlib/drawlibcairo.h"
#include "iridescent-map/MapRender.h"
#include "iridescent-map/Coast.h"

using namespace std;

typedef std::pair<int, int> IntPair;

G_DEFINE_TYPE( IridescentMap, iridescent_map, GTK_TYPE_DRAWING_AREA )

class Resource
{
public:
	LabelsByImportance labelsByImportance;
	cairo_surface_t *roughLabelsSurface, *shapesSurface, *labelsSurface;
	bool inputError;
	bool labelsSurfacePending, shapesSurfacePending;
	bool shapeTaskAssigned, labelTaskAssigned;

	Resource();
	Resource(const class Resource &a);
	virtual ~Resource();
};

Resource::Resource()
{
	shapesSurface = NULL;
	shapesSurfacePending = false;
	roughLabelsSurface= NULL;
	labelsSurface = NULL;
	labelsSurfacePending = false;
	inputError = false;
	shapeTaskAssigned = false;
	labelTaskAssigned = false;
}

Resource::Resource(const class Resource &a)
{
	shapesSurface = a.shapesSurface;
	roughLabelsSurface = a.roughLabelsSurface;
	labelsSurface = a.labelsSurface;

	labelsByImportance = a.labelsByImportance;
	labelsSurfacePending = a.labelsSurfacePending;
	shapesSurfacePending = a.shapesSurfacePending;
	inputError = a.inputError;
	shapeTaskAssigned = a.shapeTaskAssigned;
	labelTaskAssigned = a.labelTaskAssigned;
}

Resource::~Resource()
{
	labelsByImportance.clear();

	if(shapesSurface != NULL)
		cairo_surface_destroy(shapesSurface);
	shapesSurface = NULL;
	if(roughLabelsSurface != NULL)
		cairo_surface_destroy(roughLabelsSurface);
	roughLabelsSurface = NULL;
	if(labelsSurface != NULL)
		cairo_surface_destroy(labelsSurface);
	labelsSurface = NULL;

}

// ************************************************************
typedef map<int, map<int, map<int, Resource> > > Resources; //First index is zoom, then x, then y
gpointer WorkerThread (gpointer data);
static void iridescent_map_view_changed (GtkWidget *widget);
enum TaskType
{
	TASK_INVALID,
	TASK_SHAPES,
	TASK_LABELS
};

class _IridescentMapPrivate
{
public:
	//Local variables (not thread safe!)
	std::map<int, IntPair> pressPos;
	double preMoveX, preMoveY, preZoom;
	GtkWidget *parent;

	//Start of memory protected resources and controls
	GThread *workerThread;
	GMutex *mutex;
	GCond *stopWorkerCond;
	bool stopWorker;
	double currentX, currentY, currentZoom;
	std::vector<double> viewBbox; //left,bottom,right,top
	Resources resources;
	//End of memory protected resources

	_IridescentMapPrivate(GtkWidget *parent)
	{
		this->parent = parent;
		this->currentX = 2035.0;
		this->currentY = 1374.0;
		this->currentZoom = 12.0;
		this->preMoveX = 0.0;
		this->preMoveY = 0.0;
		this->preZoom = 0;
		this->stopWorker = false;
		this->mutex = new GMutex;
		g_mutex_init(this->mutex);
		this->stopWorkerCond = new GCond;
		g_cond_init (this->stopWorkerCond);
		this->workerThread = g_thread_new("IridescentMapWorker",
              WorkerThread,
              this);
	}

	virtual ~_IridescentMapPrivate()
	{
		g_mutex_lock (this->mutex);
		this->stopWorker = true;
		g_mutex_unlock (this->mutex);
		g_cond_signal (this->stopWorkerCond);
		
		g_thread_join (workerThread);
		g_thread_unref(workerThread);
		workerThread = NULL;

		g_mutex_lock (this->mutex);
		resources.clear();
		g_mutex_unlock (this->mutex);

		g_cond_clear (this->stopWorkerCond);
		delete this->stopWorkerCond;
		this->stopWorkerCond = NULL;
		g_mutex_clear (this->mutex);
		delete this->mutex;
		this->mutex = NULL;
	}
};

static void iridescent_map_init( IridescentMap* self )
{
    GdkRGBA c;
    GtkWidget *widget = GTK_WIDGET(self);
	
	self->privateData = (gpointer)new class _IridescentMapPrivate(GTK_WIDGET(widget));

    gdk_rgba_parse(&c, "blue");
    gtk_widget_override_background_color( widget, GTK_STATE_FLAG_NORMAL, &c );
	gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK);
	gtk_widget_add_events(widget, GDK_BUTTON_RELEASE_MASK);
	gtk_widget_add_events(widget, GDK_BUTTON_MOTION_MASK);
	gtk_widget_add_events(widget, GDK_SCROLL_MASK);
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
	
	g_mutex_lock (privateData->mutex);
	map<int, map<int, Resource> > &resourcesAtZoom = privateData->resources[(int)round(privateData->currentZoom)];
	
	for(map<int, map<int, Resource> >::iterator it = resourcesAtZoom.begin();
		it != resourcesAtZoom.end(); it++)
	{
		int x = it->first;
		for(map<int, Resource>::iterator it2 = it->second.begin();
			it2 != it->second.end(); it2++)
		{
			int y = it2->first;
			class Resource &r = it2->second;
			cairo_surface_t *shapesSurface = r.shapesSurface;
			cairo_pattern_t *shapesPattern = cairo_pattern_create_for_surface (shapesSurface);

			cairo_surface_t *roughLabelsSurface = r.roughLabelsSurface;
			cairo_pattern_t *roughLabelsPattern = cairo_pattern_create_for_surface (roughLabelsSurface);

			cairo_surface_t *labelsSurface = r.labelsSurface;
			cairo_pattern_t *labelsPattern = cairo_pattern_create_for_surface (labelsSurface);
			
			double dx = x - privateData->currentX;
			double dy = y - privateData->currentY;
			double px = round(dx * 640.0) + allocation.width/2;
			double py = round(dy * 640.0) + allocation.height/2;

			cairo_matrix_t mat;
			cairo_matrix_init_translate (&mat, -px, -py);

			cairo_move_to(cr, px, 
						py);
			cairo_line_to(cr, px + 640, 
						py);
			cairo_line_to(cr, px + 640, 
						py + 640);
			cairo_line_to(cr, px + 0, 
						py + 640);

			if(cairo_pattern_status(shapesPattern)==CAIRO_STATUS_SUCCESS)
			{
				cairo_pattern_set_matrix(shapesPattern, &mat);
				cairo_set_source (cr, shapesPattern);
				cairo_fill_preserve(cr);
			}
			cairo_pattern_destroy (shapesPattern);

			if(cairo_pattern_status(labelsPattern)==CAIRO_STATUS_SUCCESS)
			{
				cairo_pattern_set_matrix(labelsPattern, &mat);
				cairo_set_source (cr, labelsPattern);
				cairo_fill_preserve(cr);
			}
			else if (cairo_pattern_status(roughLabelsPattern)==CAIRO_STATUS_SUCCESS)
			{
				//If final labels are not ready, use rough labels
				cairo_pattern_set_matrix(roughLabelsPattern, &mat);
				cairo_set_source (cr, roughLabelsPattern);
				cairo_fill_preserve(cr);
			}

			cairo_new_path (cr); //Clear current path
			cairo_pattern_destroy (labelsPattern);
			cairo_pattern_destroy (roughLabelsPattern);

		}
	}
	g_mutex_unlock (privateData->mutex);

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
		g_mutex_lock (privateData->mutex);
		privateData->preMoveX = privateData->currentX;
		privateData->preMoveY = privateData->currentY;
		privateData->preZoom = privateData->currentZoom;
		g_mutex_unlock (privateData->mutex);
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
		g_mutex_lock (privateData->mutex);
		privateData->currentX = privateData->preMoveX - dx / 640.0;
		privateData->currentY = privateData->preMoveY - dy / 640.0;
		g_mutex_unlock (privateData->mutex);
		iridescent_map_view_changed(widget);
		gtk_widget_queue_draw (widget);
	}
	return true;
}

void iridescent_map_realize(GtkWidget *widget)
{
	GTK_WIDGET_CLASS (iridescent_map_parent_class)->realize (widget);

	iridescent_map_view_changed(widget);
}

gboolean iridescent_map_scroll_event (GtkWidget *widget,
	GdkEventScroll *event)
{
	cout << "iridescent_map_scroll_event" << endl;
	IridescentMap *self = IRIDESCENT_MAP(widget);
	_IridescentMapPrivate *privateData = (_IridescentMapPrivate *)self->privateData;

	GdkScrollDirection &direction = event->direction;
	g_mutex_lock (privateData->mutex);
	if(direction == GDK_SCROLL_UP)
	{
		privateData->currentZoom = round(privateData->currentZoom) + 1;
		privateData->currentX *= 2;
		privateData->currentY *= 2;
	}
	if(direction == GDK_SCROLL_DOWN)
	{
		if(privateData->currentZoom > 12.0)
		{
			privateData->currentZoom = round(privateData->currentZoom) - 1;
			privateData->currentX /= 2;
			privateData->currentY /= 2;
		}
	}
	g_mutex_unlock (privateData->mutex);

	//Plan what work needs doing at the new zoom level
	iridescent_map_view_changed(widget);

	//Trigger redraw
	gtk_widget_queue_draw (widget);
}


static void iridescent_map_class_init( IridescentMapClass* klass )
{
	GtkWidgetClass *widget_class = (GtkWidgetClass*) klass;
	widget_class->get_preferred_height = iridescent_map_get_preferred_height;
	widget_class->get_preferred_width = iridescent_map_get_preferred_width;
	widget_class->realize = iridescent_map_realize;
	widget_class->draw = iridescent_map_draw;
	widget_class->destroy = iridescent_map_destroy;
	widget_class->button_press_event = iridescent_map_button_press_event;
	widget_class->button_release_event = iridescent_map_button_release_event;
	widget_class->motion_notify_event = iridescent_map_motion_notify_event;
	widget_class->scroll_event = iridescent_map_scroll_event;
}

static gboolean iridescent_map_resources_changed (gpointer data)
{
	class _IridescentMapPrivate *priv = (class _IridescentMapPrivate *)data;

	if(priv != NULL && priv->parent != NULL)
		gtk_widget_queue_draw (priv->parent);
	return G_SOURCE_REMOVE;
}

static void iridescent_map_view_changed (GtkWidget *widget)
{
	IridescentMap *self = IRIDESCENT_MAP(widget);
	_IridescentMapPrivate *priv = (_IridescentMapPrivate *)self->privateData;

	GtkAllocation allocation;
	gtk_widget_get_allocation (widget,
                               &allocation);

	double halfWidthNumTiles = allocation.width / (640.0 * 2.0);
	double halfHeightNumTiles = allocation.height / (640.0 * 2.0);

	g_mutex_lock (priv->mutex);
	int minx = priv->currentX - halfWidthNumTiles;
	int maxx = priv->currentX + halfWidthNumTiles;
	int miny = priv->currentY - halfHeightNumTiles;
	int maxy = priv->currentY + halfHeightNumTiles;

	priv->viewBbox.clear();
	priv->viewBbox.push_back(minx);//left,bottom,right,top
	priv->viewBbox.push_back(maxy);
	priv->viewBbox.push_back(maxx);
	priv->viewBbox.push_back(miny);
	g_mutex_unlock (priv->mutex);
	
}

void FindAvailableTask(class _IridescentMapPrivate *priv, enum TaskType &taskTypeOut, 
	int &taskxOut, int &taskyOut, int &taskZoomOut)
{
	g_mutex_lock (priv->mutex);
	if(priv->viewBbox.size() != 4)
	{
		g_mutex_unlock (priv->mutex);
		return;
	}
	int minx = (int)floor(priv->viewBbox[0]);
	int maxx = (int)ceil(priv->viewBbox[2]);
	int miny = (int)floor(priv->viewBbox[3]);
	int maxy = (int)ceil(priv->viewBbox[1]);
	int roundedZoom = (int)round(priv->currentZoom);

	map<int, map<int, Resource> > &resourcesAtZoom = priv->resources[roundedZoom];

	//Tiles currently in view
	for(int x = minx; x <= maxx; x++)
	{
		//Ensure column exists
		map<int, Resource> &col = resourcesAtZoom[x];

		for(int y = miny; y <= maxy; y++)
		{
			Resource &r = col[y];
			if(!r.shapesSurfacePending && r.shapesSurface == NULL && !r.inputError)
			{
				r.shapesSurfacePending = true;
				
				taskxOut = x;
				taskyOut = y;
				taskZoomOut = roundedZoom;
				taskTypeOut = TASK_SHAPES;
				g_mutex_unlock (priv->mutex);
				return;
			}
		}
	}

	//Tiles in view + a further tile in all directions
	for(int x = minx-1; x <= maxx+1; x++)
	{
		//Ensure column exists
		map<int, Resource> &col = resourcesAtZoom[x];

		for(int y = miny-1; y <= maxy+1; y++)
		{
			Resource &r = col[y];
			if(!r.shapesSurfacePending && r.shapesSurface == NULL && !r.inputError)
			{
				r.shapesSurfacePending = true;
				
				taskxOut = x;
				taskyOut = y;
				taskZoomOut = roundedZoom;
				taskTypeOut = TASK_SHAPES;
				g_mutex_unlock (priv->mutex);
				return;
			}
		}
	}

	//Tiles currently in view
	for(int x = minx; x <= maxx; x++)
	{
		//Ensure column exists
		map<int, Resource> &col = resourcesAtZoom[x];

		for(int y = miny; y <= maxy; y++)
		{
			Resource &r = col[y];
			if(!r.labelsSurfacePending && r.labelsSurface == NULL && !r.inputError)
			{
				r.labelsSurfacePending = true;
				
				taskxOut = x;
				taskyOut = y;
				taskZoomOut = roundedZoom;
				taskTypeOut = TASK_LABELS;
				g_mutex_unlock (priv->mutex);
				return;
			}
		}
	}

	//Limit the number of tiles in memory
	//TODO

	

	g_mutex_unlock (priv->mutex);
}

gpointer WorkerThread (gpointer data)
{
	class _IridescentMapPrivate *priv = (class _IridescentMapPrivate *)data;
	g_mutex_lock (priv->mutex);
	bool stop = priv->stopWorker;
	g_mutex_unlock (priv->mutex);

	CoastMap coastMap("fosm-coast-earth201507161012.bin", 12);

	while (!stop)
	{
		int taskx = 0, tasky = 0, taskZoom = 0;
		enum TaskType taskType = TASK_INVALID; 
		FindAvailableTask(priv, taskType, taskx, tasky, taskZoom);

		//Perform task if one is available
		if(taskType == TASK_SHAPES)
		{
			// ** Draw shape layer **
			cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
			cairo_surface_t *roughLabelsSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
			FeatureStore featureStore;
			bool inputError = false;
			int dataZoom = taskZoom;
			int datax = taskx;
			int datay = tasky;
			try
			{
				//Convert request to zoom level 12
				int reqZoom = taskZoom;
				while(reqZoom > 12)
				{
					reqZoom --;
					datax /= 2;
					datay /= 2;
				}

				ReadInput(reqZoom, datax, datay, featureStore);
				dataZoom = reqZoom;
			}
			catch(runtime_error &err)
			{
				inputError = true;
			}

			if(!inputError)
			{
				class DrawLibCairoPango drawlib(surface);	
				class MapRender mapRender(&drawlib, taskx, tasky, taskZoom, datax, datay, dataZoom);
				mapRender.SetCoastMap(coastMap);
				LabelsByImportance organisedLabels;

				//Render shapes
				mapRender.Render(taskZoom, featureStore, true, true, organisedLabels);

				//Do a rough render of labels
				class DrawLibCairoPango drawlib2(roughLabelsSurface);
				class MapRender roughLabelsRender(&drawlib2, taskx, tasky, taskZoom, datax, datay, dataZoom);
				RenderLabelList labelList;
				RenderLabelListOffsets labelOffsets;
				labelList.push_back(organisedLabels);
				labelOffsets.push_back(std::pair<double, double>(0.0, 0.0));
				roughLabelsRender.RenderLabels(labelList, labelOffsets);

				g_mutex_lock (priv->mutex);
				class Resource rTmp;
				priv->resources[taskZoom][taskx][tasky] = rTmp;
				Resource &r = priv->resources[taskZoom][taskx][tasky];
				r.labelsByImportance = organisedLabels;
				r.shapesSurface = surface;
				r.roughLabelsSurface = roughLabelsSurface;
				r.shapesSurfacePending = false;
				g_mutex_unlock (priv->mutex);

				gdk_threads_add_idle (iridescent_map_resources_changed, data);		
			}
			else
			{
				g_mutex_lock (priv->mutex);
				class Resource rTmp;
				priv->resources[taskZoom][taskx][tasky] = rTmp;
				Resource &r = priv->resources[taskZoom][taskx][tasky];
				r.inputError = true;
				r.shapesSurfacePending = false;
				g_mutex_unlock (priv->mutex);
			}
		}

		if(taskType == TASK_LABELS)
		{
			// ** Draw labels layer **

			RenderLabelList labelList;
			RenderLabelListOffsets labelOffsets;

			for(int y2=tasky-1; y2<= tasky+1; y2++)
			{
				for(int x2=taskx-1; x2 <= taskx+1; x2++)
				{
					g_mutex_lock (priv->mutex);
					map<int, Resource> &col = priv->resources[taskZoom][x2];
					labelList.push_back(col[y2].labelsByImportance);
					labelOffsets.push_back(std::pair<double, double>(640.0*(x2-taskx), 640.0*(y2-tasky)));
					g_mutex_unlock (priv->mutex);
				}
			}

			cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
			class DrawLibCairoPango drawlib(surface);	
			class MapRender mapRender(&drawlib, taskx, tasky, taskZoom, taskx, tasky, taskZoom);
			mapRender.SetCoastMap(coastMap);
			mapRender.RenderLabels(labelList, labelOffsets);

			g_mutex_lock (priv->mutex);
			Resource &r = priv->resources[taskZoom][taskx][tasky];
			r.labelsSurface = surface;
			r.labelsSurfacePending = false;
			if(r.roughLabelsSurface != NULL)
				cairo_surface_destroy(r.roughLabelsSurface);
			r.roughLabelsSurface = NULL;
			g_mutex_unlock (priv->mutex);

			gdk_threads_add_idle (iridescent_map_resources_changed, data);

		}

		//Wait for a while, unless signalled by a condition
		gint64 end_time;
		if(taskType == TASK_INVALID)
			end_time = g_get_monotonic_time () + 1 * G_TIME_SPAN_SECOND; //Slow wait
		else
			end_time = g_get_monotonic_time () + 10 * G_TIME_SPAN_MILLISECOND; //Fast wait

		g_mutex_lock (priv->mutex);
		g_cond_wait_until (priv->stopWorkerCond,
                   priv->mutex,
                   end_time);
		stop = priv->stopWorker;
		g_mutex_unlock (priv->mutex);
	}

	return 0;
}

