#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <wayland-client.h>

#include <Elementary.h>
#include <Evas.h>

#include "buffer_object.h"

#define BUFWIDTH (250)
#define BUFHEIGHT (250)

struct buffer
{
   struct wl_buffer *buffer;
   void *shm_data;
   int width, height;
   int busy;
};

struct window
{
   struct wl_shm *shm;
   struct buffer buffers[2];
};

struct test_data
{
   Evas_Object *win;
   struct window window;
   Buffer_Object *bo;
   Ecore_Animator *anim;
};

static int
create_shm_buffer(struct wl_shm *shm, struct buffer *buffer,
                  int width, int height, uint32_t format)
{
   struct wl_shm_pool *pool;
   int fd, size, stride;
   void *data;

   stride = width * 4;
   size = stride * height;

   fd = os_create_anonymous_file(size);
   if (fd < 0)
     {
        fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
                size);
        return -1;
     }

   data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (data == MAP_FAILED)
     {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        return -1;
     }

   pool = wl_shm_create_pool(shm, fd, size);
   buffer->buffer = wl_shm_pool_create_buffer(pool, 0,
                                              width, height,
                                              stride, format);
   // wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
   wl_shm_pool_destroy(pool);
   close(fd);

   buffer->shm_data = data;

   return 0;
}

static struct buffer *
window_next_buffer(struct window *window)
{
   struct buffer *buffer;
   int ret = 0;

   if (!window->buffers[0].busy)
     {
        buffer = &window->buffers[0];
     }
   else if (!window->buffers[1].busy)
     {
        buffer = &window->buffers[1];
     }
   else
     return NULL;

   if (!buffer->buffer)
     {
        buffer->width = BUFWIDTH;
        buffer->height = BUFHEIGHT;
        ret = create_shm_buffer(window->shm, buffer,
                                buffer->width, buffer->height,
                                WL_SHM_FORMAT_XRGB8888);

        if (ret < 0)
          return NULL;

        /* paint the padding */
        memset(buffer->shm_data, 0xff, buffer->width * buffer->height * 4);
     }

   return buffer;
}

static void
paint_pixels(void *image, int padding, int width, int height, uint32_t time)
{
   const int halfh = padding + (height - padding * 2) / 2;
   const int halfw = padding + (width  - padding * 2) / 2;
   int ir, or;
   uint32_t *pixel = image;
   int y;

   /* squared radii thresholds */
   or = (halfw < halfh ? halfw : halfh) - 8;
   ir = or - 32;
   or *= or;
   ir *= ir;

   pixel += padding * width;
   for (y = padding; y < height - padding; y++)
     {
        int x;
        int y2 = (y - halfh) * (y - halfh);

        pixel += padding;
        for (x = padding; x < width - padding; x++)
          {
             uint32_t v;

             /* squared distance from center */
             int r2 = (x - halfw) * (x - halfw) + y2;

             if (r2 < ir)
               v = (r2 / 32 + time / 64) * 0x0080401;
             else if (r2 < or)
               v = (y + time / 32) * 0x0080401;
             else
               v = (x + time / 16) * 0x0080401;
             v &= 0x00ffffff;

             /* cross if compositor uses X from XRGB as alpha */
             // if (abs(x - y) > 6 && abs(x + y - height) > 6)
             v |= 0xff000000;

             *pixel++ = v;
          }

        pixel += padding;
     }
}

static struct buffer *
redraw(struct window *window, uint32_t time)
{
   struct buffer *buffer;

   buffer = window_next_buffer(window);
   if (!buffer)
     {
        fprintf(stderr, "Both buffers busy at redraw(). Server bug?\n");
        abort();
     }

   paint_pixels(buffer->shm_data, 20, buffer->width, buffer->height, time);

   return buffer;
}

static struct buffer *
_video_content_draw(struct test_data *d)
{
   uint32_t t = ecore_loop_time_get() * 1000;
   return redraw(&d->window, t);
}

static void
_video_image_release_cb(void *data, struct wl_buffer *wlb, void *pixels)
{
}

static Evas_Object *
_video_image_add(struct test_data *d)
{
   Evas_Object *video;
   Buffer_Object *bo;
   Evas *e = evas_object_evas_get(d->win);
   struct buffer *buffer;

   video = evas_object_image_filled_add(e);
   bo = buffer_object_setup(video);
   buffer_object_release_cb_set(bo, _video_image_release_cb, NULL);
   evas_object_data_set(video, "buffer_object", bo);

   buffer = _video_content_draw(d);
   buffer_object_buffer_set(bo, buffer->buffer, buffer->shm_data, buffer->width, buffer->height);

   evas_object_size_hint_min_set(video, buffer->width, buffer->height);
   evas_object_size_hint_max_set(video, buffer->width, buffer->height);

   d->bo = bo;

   return video;
}

static Evas_Object *
_video_layout_add(struct test_data *d)
{
   Evas_Object *layout, *video;

   video = _video_image_add(d);
   layout = elm_layout_add(d->win);
   elm_layout_file_set(layout, "./video_layout.edj", "video");
   elm_object_part_content_set(layout, "swallow.video", video);

   return layout;
}

static Eina_Bool
_video_play_anim(void *data)
{
   struct test_data *d = data;
   struct buffer *buffer;

   buffer = _video_content_draw(d);
   buffer_object_buffer_set(d->bo, buffer->buffer, buffer->shm_data, buffer->width, buffer->height);

   return ECORE_CALLBACK_RENEW;
}

static void
_video_play_cb(void *data, Evas_Object *obj, void *event_info)
{
   struct test_data *d = data;

   if (elm_check_state_get(obj))
     {
        if (d->anim)
          return;
        d->anim = ecore_animator_add(_video_play_anim, d);
     }
   else
     {
        if (!d->anim)
          return;
        ecore_animator_del(d->anim);
        d->anim = NULL;
     }
}

static void
_frame_new_cb(void *data, Evas_Object *obj, void *event_info)
{
   struct test_data *d = data;
   struct buffer *buffer;

   buffer = _video_content_draw(d);
   buffer_object_buffer_set(d->bo, buffer->buffer, buffer->shm_data, buffer->width, buffer->height);
}

EAPI_MAIN int
elm_main(int argc, char **argv)
{
   Evas_Object *win, *scroller, *box, *vbox, *plant, *plant2, *video_layout;
   Evas_Object *btn, *check;
   struct test_data d = {0};
   Evas_Coord w, h;
   char buf[PATH_MAX];

   buffer_object_init();

   elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);
   elm_app_info_set(elm_main, "elementary", "subsurfaces example");

   win = elm_win_util_standard_add(NULL, "bg-image");
   elm_win_title_set(win, "Bg Image");
   elm_win_autodel_set(win, EINA_TRUE);

   d.win = win;
   d.window.shm = ecore_wl_shm_get();

   /* Create vertical box */
   vbox = elm_box_add(win);
   elm_box_horizontal_set(vbox, EINA_FALSE);
   evas_object_size_hint_weight_set(vbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(vbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(vbox);
   elm_win_resize_object_add(win, vbox);

   /* scroller where we are adding images */
   scroller = elm_scroller_add(win);
   evas_object_size_hint_weight_set(scroller, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(scroller, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_scroller_bounce_set(scroller, EINA_TRUE, EINA_FALSE);
   elm_scroller_policy_set(scroller, ELM_SCROLLER_POLICY_ON, ELM_SCROLLER_POLICY_OFF);
   evas_object_show(scroller);
   elm_box_pack_end(vbox, scroller);

   /* put the images inside a box (inside the scroller) */
   box = elm_box_add(win);
   elm_box_horizontal_set(box, EINA_TRUE);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(box);

   elm_object_content_set(scroller, box);

   /* the actual images */
   plant = elm_image_add(win);
   elm_image_file_set(plant, "./plant_01.jpg", NULL);
   evas_object_size_hint_weight_set(plant, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(plant, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_min_set(plant, 420, BUFHEIGHT);
   evas_object_show(plant);
   elm_box_pack_end(box, plant);

   video_layout = _video_layout_add(&d);
   evas_object_size_hint_weight_set(video_layout, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(video_layout, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_min_set(video_layout, 250, 250);
   evas_object_show(video_layout);
   elm_box_pack_end(box, video_layout);

   plant2 = elm_image_add(win);
   elm_image_prescale_set(plant2, 40);
   elm_image_file_set(plant2, "./plant_01.jpg", NULL);
   evas_object_size_hint_weight_set(plant2, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(plant2, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_min_set(plant2, 420, BUFHEIGHT);
   evas_object_show(plant2);
   elm_box_pack_end(box, plant2);

   /* horizontal box layout to setup the video image */
   box = elm_box_add(win);
   elm_box_horizontal_set(box, EINA_TRUE);
   evas_object_size_hint_weight_set(box, 0.1, 0.1);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(box);
   elm_box_pack_end(vbox, box);

   /* new frame button */
   btn = elm_button_add(win);
   elm_object_text_set(btn, "New Frame");
   evas_object_size_hint_weight_set(btn, 0, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(btn, 0.0, 0.5);
   evas_object_show(btn);
   elm_box_pack_end(box, btn);

   evas_object_smart_callback_add(btn, "clicked", _frame_new_cb, &d);

   /* animation playing checkbox */
   check = elm_check_add(win);
   elm_object_text_set(check, "Play video");
   evas_object_size_hint_weight_set(check, 0, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(check, 0.0, 0.5);
   evas_object_show(check);
   elm_box_pack_end(box, check);

   evas_object_smart_callback_add(check, "changed", _video_play_cb, &d);

   evas_object_resize(win, 640, 480);
   evas_object_show(win);

   elm_run();
   elm_shutdown();

   buffer_object_shutdown();

   return 0;
}
ELM_MAIN()
