/* qidisrc for gstreamer
 * integration with proprietary QIDI Tech blob for getting raw h.264 video
 *
 * Copyright (C) 2023 Joshua Wise <joshua@accelerated.tech>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstQIDIsrc.h"

#include <stdio.h>
#include <unistd.h>
#ifndef EXTERNAL_GST_PLUGIN
#define QIDI_DYNAMIC
#endif
#include "QIDITunnel.h"

#ifdef QIDI_DYNAMIC
// From PrinterFileSystem.
#ifdef __cplusplus
extern "C"
#else
extern
#endif
QIDILib *qidilib_get();
QIDILib *_lib = NULL;
#define QIDILIB(x) (_lib->x)

#else
#define QIDILIB(x) (x)
#endif

GST_DEBUG_CATEGORY_STATIC (gst_qidisrc_debug);
#define GST_CAT_DEFAULT gst_qidisrc_debug

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
    //GST_STATIC_CAPS("video/x-h264,framerate=0/1,parsed=(boolean)false,stream-format=(string)byte-stream"));

enum
{
  PROP_0,
  PROP_LOCATION,
};

static void gst_qidisrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_qidisrc_finalize (GObject * gobject);
static void gst_qidisrc_dispose (GObject * gobject);

static void gst_qidisrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qidisrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_qidisrc_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_qidisrc_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_qidisrc_start (GstBaseSrc * bsrc);
static gboolean gst_qidisrc_stop (GstBaseSrc * bsrc);
static gboolean gst_qidisrc_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_qidisrc_query (GstBaseSrc * bsrc, GstQuery * query);
static gboolean gst_qidisrc_unlock (GstBaseSrc * bsrc);
static gboolean gst_qidisrc_unlock_stop (GstBaseSrc * bsrc);
static gboolean gst_qidisrc_set_location (GstQIDISrc * src,
    const gchar * uri, GError ** error);

#define gst_qidisrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQIDISrc, gst_qidisrc, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_qidisrc_uri_handler_init));

static void
gst_qidisrc_class_init (GstQIDISrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_qidisrc_set_property;
  gobject_class->get_property = gst_qidisrc_get_property;
  gobject_class->finalize = gst_qidisrc_finalize;
  gobject_class->dispose = gst_qidisrc_dispose;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "URI to pass to QIDI Tech blobs", "",
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);

  gst_element_class_set_static_metadata (gstelement_class, "QIDI Tech source",
      "Source/Network",
      "Receive data as a client over the network using the proprietary QIDI Tech blobs",
      "Joshua Wise <joshua@accelerated.tech>");
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_qidisrc_change_state);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_qidisrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_qidisrc_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_qidisrc_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_qidisrc_unlock_stop);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_qidisrc_is_seekable);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_qidisrc_query);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_qidisrc_create);

  GST_DEBUG_CATEGORY_INIT (gst_qidisrc_debug, "qidisrc", 0,
      "QIDI Tech src");
}

static void
gst_qidisrc_reset (GstQIDISrc * src)
{
  gst_caps_replace (&src->src_caps, NULL);
  if (src->tnl) {
    QIDILIB(QIDI_Close)(src->tnl);
    QIDILIB(QIDI_Destroy)(src->tnl);
    src->tnl = NULL;
  }
}

static void
gst_qidisrc_init (GstQIDISrc * src)
{
  src->location = NULL;
  src->tnl = NULL;

  gst_base_src_set_automatic_eos (GST_BASE_SRC (src), FALSE);
  gst_base_src_set_live(GST_BASE_SRC(src), TRUE);

  gst_qidisrc_reset (src);
}

static void
gst_qidisrc_dispose (GObject * gobject)
{
  GstQIDISrc *src = GST_QIDISRC (gobject);

  GST_DEBUG_OBJECT (src, "dispose");

  G_OBJECT_CLASS (parent_class)->dispose (gobject);
}

static void
gst_qidisrc_finalize (GObject * gobject)
{
  GstQIDISrc *src = GST_QIDISRC (gobject);

  GST_DEBUG_OBJECT (src, "finalize");

  g_free (src->location);
  if (src->tnl) {
    QIDILIB(QIDI_Close)(src->tnl);
    QIDILIB(QIDI_Destroy)(src->tnl);
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_qidisrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQIDISrc *src = GST_QIDISRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
    {
      const gchar *location;

      location = g_value_get_string (value);

      if (location == NULL) {
        GST_WARNING ("location property cannot be NULL");
        goto done;
      }
      if (!gst_qidisrc_set_location (src, location, NULL)) {
        GST_WARNING ("badly formatted location");
        goto done;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
done:
  return;
}

static void
gst_qidisrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQIDISrc *src = GST_QIDISRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

int gst_qidi_last_error = 0;

static GstFlowReturn
gst_qidisrc_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstQIDISrc *src;

  src = GST_QIDISRC (psrc);

  (void) src;
  GST_DEBUG_OBJECT (src, "create()");

  int rv;
  QIDI_Sample sample;

  if (!src->tnl) {
    return GST_FLOW_ERROR;
  }

  while ((rv = QIDILIB(QIDI_ReadSample)(src->tnl, &sample)) == QIDI_would_block) {
    GST_DEBUG_OBJECT(src, "create would block");
    usleep(33333); /* 30Hz */
  }

  if (rv == QIDI_stream_end) {
    return GST_FLOW_EOS;
  }

  if (rv != QIDI_success) {
      gst_qidi_last_error = rv;
      return GST_FLOW_ERROR;
  }

#if GLIB_CHECK_VERSION(2,68,0)
  gpointer sbuf = g_memdup2(sample.buffer, sample.size);
#else
  gpointer sbuf = g_memdup(sample.buffer, sample.size);
#endif
  *outbuf = gst_buffer_new_wrapped_full(0, sbuf, sample.size, 0, sample.size, sbuf, g_free);

  /* The NAL data already contains a timestamp (I think?), but we seem to
   * need to feed this in too -- otherwise the GStreamer pipeline gets upset
   * and starts triggering QoS events.
   */
  if (src->video_type == AVC1) {
    if (!src->sttime) {
      src->sttime = sample.decode_time * 100ULL;
    }
    GST_BUFFER_DTS(*outbuf) = sample.decode_time * 100ULL - src->sttime;
    GST_BUFFER_PTS(*outbuf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(*outbuf) = GST_CLOCK_TIME_NONE;
  }
  else {
    if (!src->sttime) {
      //only available from 1.18
      //src->sttime = gst_element_get_current_clock_time((GstElement *)psrc);
      src->sttime = gst_clock_get_time(((GstElement *)psrc)->clock);
      //if (GST_CLOCK_TIME_NONE == src->sttime)
      //  src->sttime
      GST_DEBUG_OBJECT(src,
        "sttime init to %lu.",
        src->sttime);
    }
    //GST_BUFFER_DTS(*outbuf) = gst_element_get_current_clock_time((GstElement *)psrc) - src->sttime;
    GST_BUFFER_DTS(*outbuf) = gst_clock_get_time(((GstElement *)psrc)->clock) - src->sttime;
    GST_BUFFER_PTS(*outbuf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(*outbuf) = GST_CLOCK_TIME_NONE;
  }
  GST_DEBUG_OBJECT(src,
    "sttime:%lu, DTS:%lu, PTS: %lu~",
    src->sttime, GST_BUFFER_DTS(*outbuf), GST_BUFFER_PTS(*outbuf));

  return GST_FLOW_OK;
}

static void _log(void *ctx, int lvl, const char *msg) {
  GstQIDISrc *src = (GstQIDISrc *) ctx;
  GST_DEBUG_OBJECT(src, "qidi: %s", msg);
  QIDILIB(QIDI_FreeLogMsg)(msg);
}

static gboolean
gst_qidisrc_start (GstBaseSrc * bsrc)
{
  GstQIDISrc *src = GST_QIDISRC (bsrc);

  GST_DEBUG_OBJECT (src, "start(\"%s\")", src->location);

  if (src->tnl) {
    QIDILIB(QIDI_Close)(src->tnl);
    QIDILIB(QIDI_Destroy)(src->tnl);
    src->tnl = NULL;
  }

#ifdef QIDI_DYNAMIC
  if (!_lib) {
    _lib = qidilib_get();
    if (!_lib->QIDI_Open) {
      return FALSE;
    }
  }
#endif
  if (QIDILIB(QIDI_Create)(&src->tnl, src->location) != QIDI_success) {
    return FALSE;
  }

  int rv = 0;
  QIDILIB(QIDI_SetLogger)(src->tnl, _log, (void *)src);
  if ((rv = QIDILIB(QIDI_Open)(src->tnl)) != QIDI_success) {
    QIDILIB(QIDI_Destroy)(src->tnl);
    src->tnl = NULL;
    gst_qidi_last_error = rv;
    return FALSE;
  }

  int n = 0;
  while ((rv = QIDILIB(QIDI_StartStream)(src->tnl, 1 /* video */)) == QIDI_would_block) {
    usleep(100000);
  }
  if (rv != QIDI_success) {
    QIDILIB(QIDI_Close)(src->tnl);
    QIDILIB(QIDI_Destroy)(src->tnl);
    src->tnl = NULL;
    gst_qidi_last_error = rv;
    return FALSE;
  }

  src->video_type = AVC1;
  n = QIDILIB(QIDI_GetStreamCount)(src->tnl);
  GST_INFO_OBJECT (src, "QIDI_GetStreamCount returned stream count=%d",n);
  for (int i = 0; i < n; ++i) {
    QIDI_StreamInfo info;
    QIDILIB(QIDI_GetStreamInfo)(src->tnl, i, &info);

    GST_INFO_OBJECT (src, "stream %d type=%d, sub_type=%d", i, info.type, info.sub_type);
    if (info.type == VIDE) {
      src->video_type = info.sub_type;
      GST_INFO_OBJECT (src, " width %d height=%d, frame_rate=%d",
          info.format.video.width, info.format.video.height, info.format.video.frame_rate);
    }
  }

  src->sttime = 0;
  return TRUE;
}

static gboolean
gst_qidisrc_stop (GstBaseSrc * bsrc)
{
  GstQIDISrc *src;

  src = GST_QIDISRC (bsrc);
  GST_DEBUG_OBJECT (src, "stop()");
  if (src->tnl) {
    QIDILIB(QIDI_Close)(src->tnl);
    QIDILIB(QIDI_Destroy)(src->tnl);
    src->tnl = NULL;
  }

  return TRUE;
}

static GstStateChangeReturn
gst_qidisrc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstQIDISrc *src;

  src = GST_QIDISRC (element);

  (void) src;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      //gst_qidisrc_session_close (src);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

/* Interrupt a blocking request. */
static gboolean
gst_qidisrc_unlock (GstBaseSrc * bsrc)
{
  GstQIDISrc *src;

  src = GST_QIDISRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock()");

  return TRUE;
}

/* Interrupt interrupt. */
static gboolean
gst_qidisrc_unlock_stop (GstBaseSrc * bsrc)
{
  GstQIDISrc *src;

  src = GST_QIDISRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock_stop()");

  return TRUE;
}

static gboolean
gst_qidisrc_is_seekable (GstBaseSrc * bsrc)
{
  GstQIDISrc *src = GST_QIDISRC (bsrc);

  (void) src;

  return FALSE;
}

static gboolean
gst_qidisrc_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstQIDISrc *src = GST_QIDISRC (bsrc);
  gboolean ret;
  GstSchedulingFlags flags;
  gint minsize, maxsize, align;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_URI:
      gst_query_set_uri (query, src->location);
      ret = TRUE;
      break;
    default:
      ret = FALSE;
      break;
  }

  if (!ret)
    ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SCHEDULING:
      gst_query_parse_scheduling (query, &flags, &minsize, &maxsize, &align);
      flags = (GstSchedulingFlags)((int)flags | (int)GST_SCHEDULING_FLAG_SEQUENTIAL);
      gst_query_set_scheduling (query, flags, minsize, maxsize, align);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_qidisrc_set_location (GstQIDISrc * src, const gchar * uri,
    GError ** error)
{
  if (src->location) {
    g_free (src->location);
    src->location = NULL;
  }

  if (uri == NULL)
    return FALSE;

  src->location = g_strdup (uri);

  return TRUE;
}

static GstURIType
gst_qidisrc_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_qidisrc_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "qidi", NULL };

  return protocols;
}

static gchar *
gst_qidisrc_uri_get_uri (GstURIHandler * handler)
{
  GstQIDISrc *src = GST_QIDISRC (handler);

  /* FIXME: make thread-safe */
  return g_strdup (src->location);
}

static gboolean
gst_qidisrc_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstQIDISrc *src = GST_QIDISRC (handler);

  return gst_qidisrc_set_location (src, uri, error);
}

static void
gst_qidisrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_qidisrc_uri_get_type;
  iface->get_protocols = gst_qidisrc_uri_get_protocols;
  iface->get_uri = gst_qidisrc_uri_get_uri;
  iface->set_uri = gst_qidisrc_uri_set_uri;
}

static gboolean gstqidisrc_init(GstPlugin *plugin)
{
  return gst_element_register(plugin, "qidisrc", GST_RANK_PRIMARY, GST_TYPE_QIDISRC);
}

#ifndef EXTERNAL_GST_PLUGIN

// for use inside of QIDI Slicer
void gstqidisrc_register()
{
  static int did_register = 0;
  if (did_register)
    return;
  did_register = 1;

  gst_plugin_register_static(GST_VERSION_MAJOR, GST_VERSION_MINOR, "qidisrc", "QIDI Tech source", gstqidisrc_init, "0.0.1", "GPL", "QIDIStudio", "QIDIStudio", "https://github.com/qiditech/QIDIStudio");
}

#else

#ifndef PACKAGE
#define PACKAGE "qidisrc"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, qidisrc, "QIDI Tech source", gstqidisrc_init, "0.0.1", "GPL", "QIDIStudio", "https://github.com/qiditech/QIDIStudio")

#endif
