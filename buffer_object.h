#include <wayland-client.h>

#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Ecore_Wayland.h>
#include <Evas.h>

typedef struct _Buffer_Object Buffer_Object;

typedef void (*Buffer_Object_Release_Cb)(void *data, struct wl_buffer *buffer, void *pixels);

Eina_Bool       buffer_object_init(void);
Eina_Bool       buffer_object_shutdown(void);
Buffer_Object  *buffer_object_setup(Evas_Object *o);
void            buffer_object_destroy(Buffer_Object *bo);
void            buffer_object_buffer_set(Buffer_Object *bo, struct wl_buffer *buffer, void *pixels, int width, int height);
void            buffer_object_release_cb_set(Buffer_Object *bo, Buffer_Object_Release_Cb cb, void *data);
