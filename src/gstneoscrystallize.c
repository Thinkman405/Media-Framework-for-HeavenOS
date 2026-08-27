/*
 * gstneoscrystallize.c — a real GStreamer sink element bridging video into
 * NEOS's volumetric time-crystal crystallisation, via HeavenOS's media_ffi
 * C ABI (media_ffi_crystallise_video and friends).
 *
 * Design: CONTEXT.md. GstBaseSink, not a transform — crystallise_video is
 * inherently a batch operation (it needs the whole frame sequence's energy
 * time series before any FFT/quantisation work can start), so there is no
 * streaming shape to preserve here, and no "video out" to produce either.
 * Frames accumulate in render(); the real media_ffi call happens once, at
 * EOS (caught via the event() vfunc, not stop() — stop() alone can't tell
 * "the stream finished cleanly" from "the pipeline was torn down early").
 */

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>
#include <stdio.h>

#include "media_ffi.h"

GST_DEBUG_CATEGORY_STATIC (gst_neoscrystallize_debug);
#define GST_CAT_DEFAULT gst_neoscrystallize_debug

#define GST_TYPE_NEOSCRYSTALLIZE (gst_neoscrystallize_get_type ())
G_DECLARE_FINAL_TYPE (GstNeosCrystallize, gst_neoscrystallize, GST, NEOSCRYSTALLIZE, GstBaseSink)

struct _GstNeosCrystallize
{
  GstBaseSink parent;

  /* properties */
  guint tau;
  gchar *output_path;
  gdouble scale;

  /* real result, set at EOS */
  guint64 node_count;
  gdouble input_energy;
  gdouble fundamental_hz;
  gboolean energy_conserving;

  /* negotiated caps */
  gint width;
  gint height;
  gdouble frame_rate;

  /* accumulated raw GRAY8 bytes, frame-major then row-major — exactly the
   * layout media_ffi_crystallise_video expects once cast to f64 */
  GByteArray *frames;
};

G_DEFINE_TYPE (GstNeosCrystallize, gst_neoscrystallize, GST_TYPE_BASE_SINK)

enum
{
  PROP_0,
  PROP_TAU,
  PROP_SCALE,
  PROP_OUTPUT_PATH,
  PROP_NODE_COUNT,
  PROP_INPUT_ENERGY,
  PROP_FUNDAMENTAL_HZ,
  PROP_ENERGY_CONSERVING,
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)GRAY8"));

static void gst_neoscrystallize_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_neoscrystallize_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_neoscrystallize_finalize (GObject * object);

static gboolean gst_neoscrystallize_start (GstBaseSink * sink);
static gboolean gst_neoscrystallize_stop (GstBaseSink * sink);
static gboolean gst_neoscrystallize_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static GstFlowReturn gst_neoscrystallize_render (GstBaseSink * sink,
    GstBuffer * buffer);
static gboolean gst_neoscrystallize_event (GstBaseSink * sink,
    GstEvent * event);
static void gst_neoscrystallize_finish (GstNeosCrystallize * self);

static void
gst_neoscrystallize_class_init (GstNeosCrystallizeClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_neoscrystallize_set_property;
  gobject_class->get_property = gst_neoscrystallize_get_property;
  gobject_class->finalize = gst_neoscrystallize_finalize;

  g_object_class_install_property (gobject_class, PROP_TAU,
      g_param_spec_uint ("tau", "Tau",
          "Takens embedding delay, passed straight to media_ffi_crystallise_video",
          1, G_MAXUINT, 3, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SCALE,
      g_param_spec_double ("scale", "Scale",
          "Multiplied into every raw 0-255 pixel byte before crystallisation. "
          "media_ffi_crystallise_video applies no rescaling of its own (per "
          "_mkb/timecrystal.md 5.3, that is the caller's responsibility) - "
          "real 8-bit video overflows the Howard-Comma quantisable ceiling "
          "long before a video's worth of frames finishes without this. "
          "The default matches the scale this workspace's own real video "
          "fixtures already use.",
          0.0, G_MAXDOUBLE, 3.0e-9, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_PATH,
      g_param_spec_string ("output-path", "Output path",
          "If set, write a plain-text summary here when crystallisation completes",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NODE_COUNT,
      g_param_spec_uint64 ("node-count", "Node count",
          "Phase-space node count from the last crystallisation, set after EOS",
          0, G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INPUT_ENERGY,
      g_param_spec_double ("input-energy", "Input energy",
          "Real input energy (Joules) from the last crystallisation, set after EOS",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FUNDAMENTAL_HZ,
      g_param_spec_double ("fundamental-hz", "Fundamental (Hz)",
          "Real, Howard-Comma-quantised fundamental frequency, set after EOS",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENERGY_CONSERVING,
      g_param_spec_boolean ("energy-conserving", "Energy conserving",
          "Whether crystallisation conserved energy within the half-quantum floor",
          FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "NEOS Crystallize", "Sink/Video",
      "Crystallises real video through HeavenOS/NEOS's volumetric time-crystal pipeline",
      "HeavenOS <https://github.com/Thinkman405/HeavenOS>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  basesink_class->start = GST_DEBUG_FUNCPTR (gst_neoscrystallize_start);
  basesink_class->stop = GST_DEBUG_FUNCPTR (gst_neoscrystallize_stop);
  basesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_neoscrystallize_set_caps);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_neoscrystallize_render);
  basesink_class->event = GST_DEBUG_FUNCPTR (gst_neoscrystallize_event);
}

static void
gst_neoscrystallize_init (GstNeosCrystallize * self)
{
  self->tau = 3;
  self->scale = 3.0e-9;
  self->output_path = NULL;
  self->node_count = 0;
  self->input_energy = 0.0;
  self->fundamental_hz = 0.0;
  self->energy_conserving = FALSE;
  self->width = 0;
  self->height = 0;
  self->frame_rate = 0.0;
  self->frames = g_byte_array_new ();
}

static void
gst_neoscrystallize_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNeosCrystallize *self = GST_NEOSCRYSTALLIZE (object);

  switch (prop_id) {
    case PROP_TAU:
      self->tau = g_value_get_uint (value);
      break;
    case PROP_SCALE:
      self->scale = g_value_get_double (value);
      break;
    case PROP_OUTPUT_PATH:
      g_free (self->output_path);
      self->output_path = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_neoscrystallize_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNeosCrystallize *self = GST_NEOSCRYSTALLIZE (object);

  switch (prop_id) {
    case PROP_TAU:
      g_value_set_uint (value, self->tau);
      break;
    case PROP_SCALE:
      g_value_set_double (value, self->scale);
      break;
    case PROP_OUTPUT_PATH:
      g_value_set_string (value, self->output_path);
      break;
    case PROP_NODE_COUNT:
      g_value_set_uint64 (value, self->node_count);
      break;
    case PROP_INPUT_ENERGY:
      g_value_set_double (value, self->input_energy);
      break;
    case PROP_FUNDAMENTAL_HZ:
      g_value_set_double (value, self->fundamental_hz);
      break;
    case PROP_ENERGY_CONSERVING:
      g_value_set_boolean (value, self->energy_conserving);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_neoscrystallize_finalize (GObject * object)
{
  GstNeosCrystallize *self = GST_NEOSCRYSTALLIZE (object);

  g_free (self->output_path);
  g_byte_array_free (self->frames, TRUE);

  G_OBJECT_CLASS (gst_neoscrystallize_parent_class)->finalize (object);
}

static gboolean
gst_neoscrystallize_start (GstBaseSink * sink)
{
  GstNeosCrystallize *self = GST_NEOSCRYSTALLIZE (sink);

  g_byte_array_set_size (self->frames, 0);
  self->width = 0;
  self->height = 0;
  self->frame_rate = 0.0;

  return TRUE;
}

static gboolean
gst_neoscrystallize_stop (GstBaseSink * sink)
{
  return TRUE;
}

static gboolean
gst_neoscrystallize_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstNeosCrystallize *self = GST_NEOSCRYSTALLIZE (sink);
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "failed to parse negotiated caps");
    return FALSE;
  }

  /* The pad template already restricts this to GRAY8, but a real element
   * checks explicitly rather than trusting negotiation alone. */
  if (GST_VIDEO_INFO_FORMAT (&info) != GST_VIDEO_FORMAT_GRAY8) {
    GST_ERROR_OBJECT (self, "only GRAY8 is supported");
    return FALSE;
  }

  self->width = GST_VIDEO_INFO_WIDTH (&info);
  self->height = GST_VIDEO_INFO_HEIGHT (&info);

  if (GST_VIDEO_INFO_FPS_D (&info) > 0) {
    self->frame_rate = (gdouble) GST_VIDEO_INFO_FPS_N (&info) /
        (gdouble) GST_VIDEO_INFO_FPS_D (&info);
  } else {
    self->frame_rate = 0.0;
  }

  GST_INFO_OBJECT (self, "negotiated %dx%d @ %.4f fps", self->width,
      self->height, self->frame_rate);

  return TRUE;
}

static GstFlowReturn
gst_neoscrystallize_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstNeosCrystallize *self = GST_NEOSCRYSTALLIZE (sink);
  GstMapInfo map;

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "failed to map an incoming buffer");
    return GST_FLOW_ERROR;
  }

  g_byte_array_append (self->frames, map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  return GST_FLOW_OK;
}

static gboolean
gst_neoscrystallize_event (GstBaseSink * sink, GstEvent * event)
{
  GstNeosCrystallize *self = GST_NEOSCRYSTALLIZE (sink);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    gst_neoscrystallize_finish (self);
  }

  return GST_BASE_SINK_CLASS (gst_neoscrystallize_parent_class)->event (sink,
      event);
}

static void
gst_neoscrystallize_finish (GstNeosCrystallize * self)
{
  size_t per_frame, frame_count, total, i;
  double *buf;
  MediaFfiVideoResult *result;

  if (self->width <= 0 || self->height <= 0) {
    GST_WARNING_OBJECT (self, "no caps negotiated, nothing to crystallise");
    return;
  }

  per_frame = (size_t) self->width * (size_t) self->height;
  if (per_frame == 0 || self->frames->len < per_frame) {
    GST_WARNING_OBJECT (self,
        "fewer than one full frame accumulated, nothing to crystallise");
    return;
  }

  frame_count = self->frames->len / per_frame;
  total = frame_count * per_frame;

  /* media_ffi_crystallise_video applies no rescaling of its own (byte as
   * f64, range 0-255, per its documented contract) - real 8-bit video
   * overflows the quantisable ceiling long before a video's worth of
   * frames finishes without this, confirmed directly by running a real
   * pipeline through this element before `scale` existed. */
  buf = g_new (double, total);
  for (i = 0; i < total; i++) {
    buf[i] = (double) self->frames->data[i] * self->scale;
  }

  result = media_ffi_crystallise_video (buf, frame_count,
      (size_t) self->width, (size_t) self->height, self->frame_rate,
      (size_t) self->tau);

  g_free (buf);

  if (media_ffi_video_result_is_ok (result) == 1) {
    GstStructure *s;

    self->node_count = (guint64) media_ffi_video_result_node_count (result);
    self->input_energy = media_ffi_video_result_input_energy (result);
    self->fundamental_hz = media_ffi_video_result_fundamental_hz (result);
    self->energy_conserving =
        media_ffi_video_result_is_energy_conserving (result) == 1;

    GST_INFO_OBJECT (self,
        "crystallised %" G_GUINT64_FORMAT
        " node(s), energy %.6e J, fundamental %.4f Hz, conserving: %d",
        self->node_count, self->input_energy, self->fundamental_hz,
        self->energy_conserving);

    if (self->output_path != NULL) {
      FILE *f = fopen (self->output_path, "w");
      if (f != NULL) {
        fprintf (f, "nodes: %" G_GUINT64_FORMAT "\n", self->node_count);
        fprintf (f, "input_energy_joules: %.10e\n", self->input_energy);
        fprintf (f, "fundamental_hz: %.10f\n", self->fundamental_hz);
        fprintf (f, "energy_conserving: %s\n",
            self->energy_conserving ? "true" : "false");
        fclose (f);
      } else {
        GST_ERROR_OBJECT (self, "failed to open output-path '%s'",
            self->output_path);
      }
    }

    s = gst_structure_new ("neoscrystallize-result",
        "node-count", G_TYPE_UINT64, self->node_count,
        "input-energy", G_TYPE_DOUBLE, self->input_energy,
        "fundamental-hz", G_TYPE_DOUBLE, self->fundamental_hz,
        "energy-conserving", G_TYPE_BOOLEAN, self->energy_conserving, NULL);
    gst_element_post_message (GST_ELEMENT (self),
        gst_message_new_application (GST_OBJECT (self), s));
  } else {
    const char *msg = media_ffi_video_result_error_message (result);
    GST_ELEMENT_ERROR (self, STREAM, FAILED,
        ("NEOS crystallisation failed"),
        ("%s", msg != NULL ? msg : "unknown error"));
  }

  media_ffi_video_result_free (result);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_neoscrystallize_debug, "neoscrystallize", 0,
      "NEOS video crystallisation sink");
  return gst_element_register (plugin, "neoscrystallize", GST_RANK_NONE,
      GST_TYPE_NEOSCRYSTALLIZE);
}

#ifndef PACKAGE
#define PACKAGE "neoscrystallize"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    neoscrystallize,
    "NEOS/HeavenOS video crystallisation sink",
    plugin_init,
    "0.1.0", "Proprietary", "HeavenOS",
    "https://github.com/Thinkman405/Media-Framework-for-HeavenOS")
