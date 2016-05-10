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
	cairo_surface_t *labelsSurface, *shapesSurface;
	bool inputError;
	bool labelsSurfacePending, shapesSurfacePending;

	Resource();
	Resource(const class Resource &a);
	virtual ~Resource();
};

Resource::Resource()
{
	labelsSurface = NULL;
	labelsSurfacePending = false;
	shapesSurface = NULL;
	shapesSurfacePending = false;
	inputError = false;
}

Resource::Resource(const class Resource &a)
{
	labelsSurface = a.labelsSurface;
	shapesSurface = a.shapesSurface;
	labelsByImportance = a.labelsByImportance;
	labelsSurfacePending = a.labelsSurfacePending;
	shapesSurfacePending = a.shapesSurfacePending;
	inputError = a.inputError;
}

Resource::~Resource()
{
	labelsByImportance.clear();
	if(labelsSurface != NULL)
		cairo_surface_destroy(labelsSurface);
	labelsSurface = NULL;
	if(shapesSurface != NULL)
		cairo_surface_destroy(shapesSurface);
	shapesSurface = NULL;
}
// ************************************************************

class WorkerThreadTask
{
public:
	enum TaskType
	{
		TASK_INVALID,
		TASK_SHAPES,
		TASK_LABELS
	};

	TaskType type;
	int x, y, zoom;
	bool complete, assigned;
	int priority;

	WorkerThreadTask();
	WorkerThreadTask(const class WorkerThreadTask &a);
	WorkerThreadTask& operator=(const WorkerThreadTask &arg);
	virtual ~WorkerThreadTask();
};

WorkerThreadTask::WorkerThreadTask()
{
	type = TASK_INVALID;
	x = 0;	
	y = 0; 
	zoom = 0;
	priority = 0;
	complete = false;
	assigned = false;
}

WorkerThreadTask::WorkerThreadTask(const class WorkerThreadTask &a)
{
	*this = a;
}

WorkerThreadTask& WorkerThreadTask::operator=(const WorkerThreadTask &arg)
{
	type = arg.type;
	x = arg.x;	
	y = arg.y; 
	zoom = arg.zoom;
	priority = arg.priority;
	complete = arg.complete;
	assigned = arg.assigned;
	return *this;
}

WorkerThreadTask::~WorkerThreadTask()
{

}

// ************************************************************
typedef map<int, map<int, Resource> > Resources;
gpointer WorkerThread (gpointer data);
static void iridescent_map_view_changed (GtkWidget *widget);

class _IridescentMapPrivate
{
public:
	//Local variables (not thread safe!)
	std::map<int, IntPair> pressPos;
	double currentX, currentY;
	double preMoveX, preMoveY;
	GtkWidget *parent;

	//Start of memory protected resources and controls
	GThread *workerThread;
	GMutex *mutex;
	GCond *stopWorkerCond;
	bool stopWorker;
	std::list<class WorkerThreadTask> taskList;
	Resources resources;
	//End of memory protected resources

	_IridescentMapPrivate(GtkWidget *parent)
	{
		this->parent = parent;
		this->currentX = 2035.0;
		this->currentY = 1374.0;
		this->preMoveX = 0.0;
		this->preMoveY = 0.0;
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
	for(Resources::iterator it = privateData->resources.begin();
		it != privateData->resources.end(); it++)
	{
		int x = it->first;
		for(map<int, Resource>::iterator it2 = it->second.begin();
			it2 != it->second.end(); it2++)
		{
			int y = it2->first;
			class Resource &r = it2->second;
			cairo_surface_t *shapesSurface = r.shapesSurface;
			cairo_pattern_t *shapesPattern = cairo_pattern_create_for_surface (shapesSurface);

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
			cairo_new_path (cr); //Clear current path
			cairo_pattern_destroy (labelsPattern);

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
	_IridescentMapPrivate *privateData = (_IridescentMapPrivate *)self->privateData;

	GtkAllocation allocation;
	gtk_widget_get_allocation (widget,
                               &allocation);

	double halfWidthNumTiles = allocation.width / (640.0 * 2.0);
	double halfHeightNumTiles = allocation.height / (640.0 * 2.0);

	int minx = (int)floor(privateData->currentX - halfWidthNumTiles);
	int maxx = (int)ceil(privateData->currentX + halfWidthNumTiles);
	int miny = (int)floor(privateData->currentY - halfHeightNumTiles);
	int maxy = (int)ceil(privateData->currentY + halfHeightNumTiles);

	g_mutex_lock (privateData->mutex);
	
	//Tiles currently in view
	for(int x = minx; x < maxx; x++)
	{
		//Ensure column exists
		map<int, Resource> &col = privateData->resources[x];

		for(int y = miny; y < maxy; y++)
		{
			Resource &r = col[y];
			if(!r.shapesSurfacePending && r.shapesSurface == NULL)
			{
				r.shapesSurfacePending = true;
				
				WorkerThreadTask task;
				task.x = x;
				task.y = y;
				task.zoom = 12;
				task.type = WorkerThreadTask::TASK_SHAPES;
				task.priority = 1;
				privateData->taskList.push_back(task);
			}

			if(!r.labelsSurfacePending && r.labelsSurface == NULL)
			{
				r.labelsSurfacePending = true;
				
				WorkerThreadTask task;
				task.x = x;
				task.y = y;
				task.zoom = 12;
				task.type = WorkerThreadTask::TASK_LABELS;
				task.priority = 2;
				privateData->taskList.push_back(task);
			}
		}
	}

	//Check label tasks have appropriate prerequisite data to complete properly
	

	g_mutex_unlock (privateData->mutex);

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
		g_mutex_lock (priv->mutex);
		class WorkerThreadTask taskCpy; //Local copy of task
		bool taskReady = false;
		std::list<class WorkerThreadTask> &taskList = priv->taskList;
		int highestPriority = 0;
		bool highestPrioritySet = false;

		//Find highest priority unassigned task
		std::list<class WorkerThreadTask>::iterator bestIt = taskList.end();
		for(std::list<class WorkerThreadTask>::iterator it = taskList.begin(); it!=taskList.end(); it++)
		{
			if (it->complete || it->assigned) continue;
			if(highestPrioritySet && it->priority >= highestPriority) continue; //Task not urgent compared to others

			//Candidate task found
			bestIt = it;
			highestPrioritySet = true;
			highestPriority = it->priority;
		}

		if(bestIt != taskList.end())
		{		
			//Task found, mark it as assigned
			bestIt->assigned = true;
			taskCpy = *bestIt;
			taskReady = true;
		}
		g_mutex_unlock (priv->mutex);

		//Perform task if one is available
		if(taskReady && taskCpy.type == WorkerThreadTask::TASK_SHAPES && !taskCpy.complete)
		{
			// ** Draw shape layer **
			cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
			FeatureStore featureStore;
			bool inputError = false;
			try
			{
				ReadInput(taskCpy.zoom, taskCpy.x, taskCpy.y, featureStore);
			}
			catch(runtime_error &err)
			{
				inputError = true;
			}

			if(!inputError)
			{
				class SlippyTilesTransform slippyTilesTransform(taskCpy.zoom, taskCpy.x, taskCpy.y);

				class DrawLibCairoPango drawlib(surface);	
				class MapRender mapRender(&drawlib, taskCpy.x, taskCpy.y, taskCpy.zoom);
				mapRender.SetCoastMap(coastMap);
				LabelsByImportance organisedLabels;

				mapRender.Render(taskCpy.zoom, featureStore, true, true, slippyTilesTransform, organisedLabels);

				g_mutex_lock (priv->mutex);
				class Resource rTmp;
				priv->resources[taskCpy.x][taskCpy.y] = rTmp;
				Resource &r = priv->resources[taskCpy.x][taskCpy.y];
				r.labelsByImportance = organisedLabels;
				r.shapesSurface = surface;
				r.shapesSurfacePending = false;
				g_mutex_unlock (priv->mutex);

				gdk_threads_add_idle (iridescent_map_resources_changed, data);		
			}
			else
			{
				g_mutex_lock (priv->mutex);
				class Resource rTmp;
				priv->resources[taskCpy.x][taskCpy.y] = rTmp;
				Resource &r = priv->resources[taskCpy.x][taskCpy.y];
				r.inputError = true;
				g_mutex_unlock (priv->mutex);
			}
		}

		if(taskReady && taskCpy.type == WorkerThreadTask::TASK_LABELS && !taskCpy.complete)
		{
			// ** Draw labels layer **

			RenderLabelList labelList;
			RenderLabelListOffsets labelOffsets;

			for(int y2=taskCpy.y-1; y2<= taskCpy.y+1; y2++)
			{
				for(int x2=taskCpy.x-1; x2 <= taskCpy.x+1; x2++)
				{
					g_mutex_lock (priv->mutex);
					map<int, Resource> &col = priv->resources[x2];
					labelList.push_back(col[y2].labelsByImportance);
					labelOffsets.push_back(std::pair<double, double>(640.0*(x2-2035), 640.0*(y2-1374)));
					g_mutex_unlock (priv->mutex);
				}
			}

			cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 640);
			class DrawLibCairoPango drawlib(surface);	
			class MapRender mapRender(&drawlib, taskCpy.x, taskCpy.y, taskCpy.zoom);
			mapRender.SetCoastMap(coastMap);
			mapRender.RenderLabels(labelList, labelOffsets);

			g_mutex_lock (priv->mutex);
			Resource &r = priv->resources[taskCpy.x][taskCpy.y];
			r.labelsSurface = surface;
			r.labelsSurfacePending = false;
			g_mutex_unlock (priv->mutex);

			gdk_threads_add_idle (iridescent_map_resources_changed, data);

		}

		//Wait for a while, unless signalled by a condition
		gint64 end_time;
		end_time = g_get_monotonic_time () + 1 * G_TIME_SPAN_SECOND;

		g_mutex_lock (priv->mutex);
		g_cond_wait_until (priv->stopWorkerCond,
                   priv->mutex,
                   end_time);
		stop = priv->stopWorker;
		g_mutex_unlock (priv->mutex);
	}

	return 0;
}

