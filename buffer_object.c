#include <wayland-client.h>

#include <eina_error.h>
#include <eina_log.h>
#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Ecore_Wayland.h>
#include <Evas.h>
#include <eina_safety_checks.h>

#include "buffer_object.h"

struct _Buffer_Object
{
   Evas_Object *object;
   Evas *e;
   Ecore_Wl_Window *win;
   Ecore_Wl_Subsurf *subsurf;
   struct wl_surface *surface;

   Buffer_Object_Release_Cb cb;
   void *cb_data;
   Eina_Bool buffer_busy;

   struct wl_buffer *wlb;
   void *pixels;

   int width, height; /* image width and height */

   int x, y; /* Subsurface position */
};

static int _buffer_object_log_dom = -1;
static int _init_count = 0;

#ifdef ERR
#undef ERR
#endif
#define ERR(...) EINA_LOG_DOM_ERR(_buffer_object_log_dom, __VA_ARGS__)

#ifdef DBG
#undef DBG
#endif
#define DBG(...) EINA_LOG_DOM_DBG(_buffer_object_log_dom, __VA_ARGS__)

static void
_buffer_pixels_cb(void *data, Evas *evas, void *event EINA_UNUSED)
{
   Buffer_Object *bo = data;

   if (!bo->buffer_busy)
     return;

   if (bo->cb)
     bo->cb(bo->cb_data, bo->wlb, bo->pixels);
}

static void
_buffer_object_wl_buffer_update(Buffer_Object *bo)
{
   struct wl_surface *surface = NULL;

   if (!bo->surface)
     return;

   DBG("%p: wl_buffer update", bo);

   wl_surface_attach(bo->surface, bo->wlb, 0, 0);
   wl_surface_damage(bo->surface, 0, 0, bo->width, bo->height);
   wl_surface_commit(bo->surface);
}

static void
_buffer_object_pixels_update(Buffer_Object *bo)
{
   void *unused_data = evas_object_image_data_get(bo->object, EINA_TRUE);

   DBG("%p: pixels update", bo);

   evas_object_image_data_set(bo->object, bo->pixels);
   evas_object_image_data_update_add(bo->object, 0, 0, bo->width, bo->height);
}

static void
_buffer_object_move(void *data, Evas_Object *obj EINA_UNUSED, const Evas_Video_Surface *video_surf EINA_UNUSED, Evas_Coord x, Evas_Coord y)
{
   Buffer_Object *bo = data;
   DBG("%p: video surface move: %p, %d, %d", bo, bo->subsurf, x, y);
   bo->x = x;
   bo->y = y;
   if (!bo->subsurf)
     return;
   ecore_wl_subsurf_position_set(bo->subsurf, bo->x, bo->y);
}

static void
_buffer_object_resize(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, const Evas_Video_Surface *video_surf EINA_UNUSED, Evas_Coord w, Evas_Coord h)
{
}

static void
_buffer_object_show(void *data, Evas_Object *obj EINA_UNUSED, const Evas_Video_Surface *video_surf EINA_UNUSED)
{
   Buffer_Object *bo = data;
   struct wl_surface *win_surf;
   DBG("%p: video surface show", bo);

   if (!bo->win)
     {
        Ecore_Evas *ee;

        ee = ecore_evas_ecore_evas_get(bo->e);
        if (!ee)
          {
             ERR("Couldn't get Ecore_Evas from Evas: %p", bo->e);
             return;
          }

        bo->win = ecore_evas_wayland_window_get(ee);
        if (!bo->win)
          {
             ERR("Couldn't get Ecore_Wl_Window from Ecore_Evas: %p", ee);
             return;
          }
     }

   if (!bo->subsurf)
     bo->subsurf = ecore_wl_subsurf_create(bo->win);

   if (!bo->subsurf)
     {
        ERR("Could not create subsurface on window: %p", bo->win);
        return;
     }

   win_surf = ecore_wl_window_surface_get(bo->win);

   ecore_wl_subsurf_position_set(bo->subsurf, bo->x, bo->y);

   bo->surface = ecore_wl_subsurf_surface_get(bo->subsurf);

   _buffer_object_wl_buffer_update(bo);
}

static void
_buffer_object_hide(void *data, Evas_Object *obj EINA_UNUSED, const Evas_Video_Surface *video_surf EINA_UNUSED)
{
   Buffer_Object *bo = data;
   DBG("%p: video surface hide", bo);
   if (!bo->subsurf)
     return;

   ecore_wl_subsurf_del(bo->subsurf);
   bo->subsurf = NULL;
}

static void
_buffer_object_update_pixels(void *data, Evas_Object *obj EINA_UNUSED, const Evas_Video_Surface *video_surf EINA_UNUSED)
{
   Buffer_Object *bo = data;
   DBG("%p: video surface update pixels.\n", bo);
   _buffer_object_pixels_update(bo);
}

Eina_Bool
buffer_object_init(void)
{
   if (_init_count++ > 0)
     return EINA_TRUE;

   if (!eina_init())
     {
        _init_count--;
        return EINA_FALSE;
     }

   _buffer_object_log_dom = eina_log_domain_register("buffer_object",
                                                     EINA_COLOR_BLUE);
   if (_buffer_object_log_dom < 0)
     {
        EINA_LOG_ERR("Could not register log domain: buffer_object");
        _init_count--;
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

Eina_Bool
buffer_object_shutdown(void)
{
   if (_init_count == 0)
     {
        EINA_LOG_ERR("buffer_object already shutdown.");
        return EINA_FALSE;
     }

   _init_count--;

   eina_log_domain_unregister(_buffer_object_log_dom);
   _buffer_object_log_dom = -1;

   eina_shutdown();

   return EINA_TRUE;
}

Buffer_Object *
buffer_object_setup(Evas_Object *o)
{
   Evas_Video_Surface video;
   unsigned int video_caps;
   Buffer_Object *bo = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(o, NULL);

   bo = calloc(1, sizeof(*bo));
   bo->object = o;

   video.version = EVAS_VIDEO_SURFACE_VERSION;
   video.data = bo;
   video.parent = NULL;
   video.move = _buffer_object_move;
   video.resize = _buffer_object_resize;
   video.show = _buffer_object_show;
   video.hide = _buffer_object_hide;
   video.update_pixels = _buffer_object_update_pixels;

   evas_object_image_video_surface_set(o, &video);
   video_caps = evas_object_image_video_surface_caps_get(o);
   video_caps = video_caps & ~EVAS_VIDEO_SURFACE_RESIZE;
   video_caps = video_caps & ~EVAS_VIDEO_SURFACE_STACKING_CHECK;
   video_caps = video_caps & ~EVAS_VIDEO_SURFACE_IGNORE_WINDOW;
   evas_object_image_video_surface_caps_set(o, video_caps);

   bo->e = evas_object_evas_get(bo->object);
   if (!bo->e)
     {
        ERR("Couldn't get Evas from object %p", bo->object);
        goto error;
     }
   evas_event_callback_add(bo->e, EVAS_CALLBACK_RENDER_POST,
                           _buffer_pixels_cb, bo);
   return bo;

error:
   free(bo);
   return NULL;
}

void
buffer_object_destroy(Buffer_Object *bo)
{
   EINA_SAFETY_ON_NULL_RETURN(bo);
   evas_event_callback_del_full(bo->e, EVAS_CALLBACK_RENDER_POST,
                                _buffer_pixels_cb, bo);

   evas_object_image_video_surface_set(bo->object, NULL);
   free(bo);
}

void
buffer_object_buffer_set(Buffer_Object *bo, struct wl_buffer *wlb, void *pixels, int width, int height)
{
   EINA_SAFETY_ON_NULL_RETURN(bo);
   EINA_SAFETY_ON_NULL_RETURN(wlb);
   EINA_SAFETY_ON_NULL_RETURN(pixels);
   bo->wlb = wlb;
   bo->pixels = pixels;
   bo->width = width;
   bo->height = height;
   bo->buffer_busy = EINA_TRUE;

   evas_object_image_size_set(bo->object, width, height);
   evas_object_image_pixels_dirty_set(bo->object, EINA_TRUE);

   if (bo->subsurf)
     _buffer_object_wl_buffer_update(bo);
}

void
buffer_object_release_cb_set(Buffer_Object *bo, Buffer_Object_Release_Cb cb, void *data)
{
   EINA_SAFETY_ON_NULL_RETURN(bo);
   bo->cb = cb;
   bo->cb_data = data;
}
