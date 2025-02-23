/* Copyright (C) 2022 Basler AG
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *     1. Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *     2. Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *     3. Neither the name of the copyright holder nor the names of
 *        its contributors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpylondebug.h"
#include "gstpylonfeaturewalker.h"
#include "gstpylonobject.h"
#include "gstpylonparamspecs.h"

#include <utility>

typedef struct _GstPylonObjectPrivate GstPylonObjectPrivate;
struct _GstPylonObjectPrivate {
  std::shared_ptr<Pylon::CBaslerUniversalInstantCamera> camera;
  GenApi::INodeMap* nodemap;
};

typedef struct _GstPylonObjectDeviceMembers GstPylonObjectDeviceMembers;
struct _GstPylonObjectDeviceMembers {
  const gchar* device_name;
  GenApi::INodeMap& nodemap;
};

/************************************************************
 * Start of GObject definition
 ***********************************************************/

static void gst_pylon_object_init(GstPylonObject* self);
static void gst_pylon_object_class_init(
    GstPylonObjectClass* klass, GstPylonObjectDeviceMembers* device_members);
static gpointer gst_pylon_object_parent_class = NULL;
static gint GstPylonObject_private_offset;
static void gst_pylon_object_class_intern_init(
    gpointer klass, GstPylonObjectDeviceMembers* device_members) {
  gst_pylon_object_parent_class = g_type_class_peek_parent(klass);
  if (GstPylonObject_private_offset != 0)
    g_type_class_adjust_private_offset(klass, &GstPylonObject_private_offset);
  gst_pylon_object_class_init((GstPylonObjectClass*)klass, device_members);
}
static inline gpointer gst_pylon_object_get_instance_private(
    GstPylonObject* self) {
  return (G_STRUCT_MEMBER_P(self, GstPylonObject_private_offset));
}

GType gst_pylon_object_register(const Pylon::String_t& device_name,
                                GenApi::INodeMap& exemplar) {
  GstPylonObjectDeviceMembers* device_members = new GstPylonObjectDeviceMembers(
      {g_strdup(device_name.c_str()), exemplar});

  GTypeInfo typeinfo = {
      sizeof(GstPylonObjectClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_pylon_object_class_intern_init,
      NULL,
      device_members,
      sizeof(GstPylonObject),
      0,
      (GInstanceInitFunc)gst_pylon_object_init,
  };

  /* Convert camera name to a valid string */
  gchar* type_name = gst_pylon_param_spec_sanitize_name(device_name.c_str());

  GType type = g_type_from_name(type_name);
  if (!type) {
    type = g_type_register_static(GST_TYPE_OBJECT, type_name, &typeinfo,
                                  static_cast<GTypeFlags>(0));
  }

  g_free(type_name);

  GstPylonObject_private_offset =
      g_type_add_instance_private(type, sizeof(GstPylonObjectPrivate));

  return type;
}

/************************************************************
 * End of GObject definition
 ***********************************************************/

/* prototypes */
static void gst_pylon_object_install_properties(GstPylonObjectClass* klass,
                                                GenApi::INodeMap& nodemap,
                                                const gchar* device_fullname);
template <typename F, typename P>
static void gst_pylon_object_set_pylon_property(GenApi::INodeMap& nodemap,
                                                F get_value,
                                                const GValue* value,
                                                const gchar* name);
static void gst_pylon_object_set_enum_property(GenApi::INodeMap& nodemap,
                                               const GValue* value,
                                               const gchar* name);
static void gst_pylon_object_classify_selector(GenApi::INodeMap& nodemap,
                                               const gchar* selector_name,
                                               guint64& selector_value);
template <typename F, typename P>
static void gst_pylon_object_set_pylon_selector(
    GenApi::INodeMap& nodemap, F get_value, const GValue* value,
    const gchar* feature, const gchar* selector, guint64& selector_value);
static void gst_pylon_object_set_enum_selector(GenApi::INodeMap& nodemap,
                                               const GValue* value,
                                               const gchar* feature_name,
                                               const gchar* selector_name,
                                               guint64& selector_value);
template <typename T, typename P>
static T gst_pylon_object_get_pylon_property(GenApi::INodeMap& nodemap,
                                             const gchar* name);
static gint gst_pylon_object_get_enum_property(GenApi::INodeMap& nodemap,
                                               const gchar* name);
template <typename F, typename P>
static void gst_pylon_object_classify_feature(
    GParamSpec* pspec, GstPylonObjectPrivate* priv,
    GstPylonParamSpecSelectorData* selector_data, F get_value,
    const GValue* value);
static void gst_pylon_object_set_property(GObject* object, guint property_id,
                                          const GValue* value,
                                          GParamSpec* pspec);
static void gst_pylon_object_get_property(GObject* object, guint property_id,
                                          GValue* value, GParamSpec* pspec);
static void gst_pylon_object_finalize(GObject* self);

static void gst_pylon_object_install_properties(GstPylonObjectClass* klass,
                                                GenApi::INodeMap& nodemap,
                                                const gchar* device_name) {
  g_return_if_fail(klass);

  GObjectClass* oclass = G_OBJECT_CLASS(klass);

  GstPylonFeatureWalker::install_properties(oclass, nodemap, device_name);
}

static void gst_pylon_object_class_init(
    GstPylonObjectClass* klass, GstPylonObjectDeviceMembers* device_members) {
  GObjectClass* oclass = G_OBJECT_CLASS(klass);

  oclass->set_property = gst_pylon_object_set_property;
  oclass->get_property = gst_pylon_object_get_property;
  oclass->finalize = gst_pylon_object_finalize;

  gst_pylon_object_install_properties(klass, device_members->nodemap,
                                      device_members->device_name);

  delete (device_members);
}

static void gst_pylon_object_init(GstPylonObject* self) {}

template <typename F, typename P>
static void gst_pylon_object_set_pylon_property(GenApi::INodeMap& nodemap,
                                                F get_value,
                                                const GValue* value,
                                                const gchar* name) {
  P param(nodemap, name);
  param.SetValue(get_value(value));
}

static void gst_pylon_object_set_enum_property(GenApi::INodeMap& nodemap,
                                               const GValue* value,
                                               const gchar* name) {
  Pylon::CEnumParameter param(nodemap, name);
  param.SetIntValue(g_value_get_enum(value));
}

static void gst_pylon_object_classify_selector(GenApi::INodeMap& nodemap,
                                               const gchar* selector_name,
                                               guint64& selector_value) {
  gint selector_type =
      nodemap.GetNode(selector_name)->GetPrincipalInterfaceType();
  switch (selector_type) {
    case GenApi::intfIEnumeration:
      Pylon::CEnumParameter(nodemap, selector_name).SetIntValue(selector_value);
      break;
    case GenApi::intfIInteger:
      Pylon::CIntegerParameter(nodemap, selector_name).SetValue(selector_value);
      break;
    default:
      std::string error_msg = "Selector \"" + std::string(selector_name) +
                              "\"" + " is of invalid type " +
                              std::to_string(selector_type);
      g_warning("%s", error_msg.c_str());
      throw Pylon::GenericException(error_msg.c_str(), __FILE__, __LINE__);
  }
}

template <typename F, typename P>
static void gst_pylon_object_set_pylon_selector(GenApi::INodeMap& nodemap,
                                                F get_value,
                                                const GValue* value,
                                                const gchar* feature_name,
                                                const gchar* selector_name,
                                                guint64& selector_value) {
  gst_pylon_object_classify_selector(nodemap, selector_name, selector_value);

  gst_pylon_object_set_pylon_property<F, P>(nodemap, get_value, value,
                                            feature_name);
}

static void gst_pylon_object_set_enum_selector(GenApi::INodeMap& nodemap,
                                               const GValue* value,
                                               const gchar* feature_name,
                                               const gchar* selector_name,
                                               guint64& selector_value) {
  gst_pylon_object_classify_selector(nodemap, selector_name, selector_value);

  gst_pylon_object_set_enum_property(nodemap, value, feature_name);
}

template <typename T, typename P>
static T gst_pylon_object_get_pylon_property(GenApi::INodeMap& nodemap,
                                             const gchar* name) {
  P param(nodemap, name);
  return param.GetValue();
}

static gint gst_pylon_object_get_enum_property(GenApi::INodeMap& nodemap,
                                               const gchar* name) {
  Pylon::CEnumParameter param(nodemap, name);
  return param.GetIntValue();
}

template <typename F, typename P>
static void gst_pylon_object_classify_feature(
    GParamSpec* pspec, GstPylonObjectPrivate* priv,
    GstPylonParamSpecSelectorData* selector_data, F get_value,
    const GValue* value) {
  /* The value accepted by the pspec can be a direct feature or a feature that
   * has a selector. */
  if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
    gst_pylon_object_set_pylon_selector<F, P>(
        *priv->nodemap, get_value, value, selector_data->feature,
        selector_data->selector, selector_data->selector_value);
  } else {
    gst_pylon_object_set_pylon_property<F, P>(*priv->nodemap, get_value, value,
                                              pspec->name);
  }
}

static void gst_pylon_object_set_property(GObject* object, guint property_id,
                                          const GValue* value,
                                          GParamSpec* pspec) {
  GstPylonObject* self = (GstPylonObject*)object;
  GstPylonObjectPrivate* priv =
      (GstPylonObjectPrivate*)gst_pylon_object_get_instance_private(self);
  GType value_type = g_type_fundamental(G_VALUE_TYPE(value));
  GstPylonParamSpecSelectorData* selector_data = NULL;

  if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
    selector_data = gst_pylon_param_spec_selector_get_data(pspec);
  }

  try {
    switch (value_type) {
      case G_TYPE_INT64:
        typedef gint64 (*GGetInt64)(const GValue*);
        gst_pylon_object_classify_feature<GGetInt64, Pylon::CIntegerParameter>(
            pspec, priv, selector_data, g_value_get_int64, value);
        break;
      case G_TYPE_BOOLEAN:
        typedef gboolean (*GGetBool)(const GValue*);
        gst_pylon_object_classify_feature<GGetBool, Pylon::CBooleanParameter>(
            pspec, priv, selector_data, g_value_get_boolean, value);
        break;
      case G_TYPE_FLOAT:
        typedef gfloat (*GGetFloat)(const GValue*);
        gst_pylon_object_classify_feature<GGetFloat, Pylon::CFloatParameter>(
            pspec, priv, selector_data, g_value_get_float, value);
        break;
      case G_TYPE_STRING:
        typedef const gchar* (*GGetString)(const GValue*);
        gst_pylon_object_classify_feature<GGetString, Pylon::CStringParameter>(
            pspec, priv, selector_data, g_value_get_string, value);
        break;
      case G_TYPE_ENUM:
        if (GST_PYLON_PARAM_FLAG_IS_SET(pspec, GST_PYLON_PARAM_IS_SELECTOR)) {
          gst_pylon_object_set_enum_selector(
              *priv->nodemap, value, selector_data->feature,
              selector_data->selector, selector_data->selector_value);
        } else {
          gst_pylon_object_set_enum_property(*priv->nodemap, value,
                                             pspec->name);
        }
        break;
      default:
        g_warning("Unsupported GType: %s", g_type_name(pspec->value_type));
        std::string msg =
            "Unsupported GType: " + std::string(g_type_name(pspec->value_type));
        throw Pylon::GenericException(msg.c_str(), __FILE__, __LINE__);
    }
  } catch (const Pylon::GenericException& e) {
    GST_ERROR("Unable to set pylon property \"%s\" on \"%s\": %s", pspec->name,
              priv->camera->GetDeviceInfo().GetFriendlyName().c_str(),
              e.GetDescription());
  }
}

static void gst_pylon_object_get_property(GObject* object, guint property_id,
                                          GValue* value, GParamSpec* pspec) {
  GstPylonObject* self = (GstPylonObject*)object;
  GstPylonObjectPrivate* priv =
      (GstPylonObjectPrivate*)gst_pylon_object_get_instance_private(self);

  try {
    switch (g_type_fundamental(pspec->value_type)) {
      case G_TYPE_INT64:
        g_value_set_int64(value, gst_pylon_object_get_pylon_property<
                                     gint64, Pylon::CIntegerParameter>(
                                     *priv->nodemap, pspec->name));
        break;
      case G_TYPE_BOOLEAN:
        g_value_set_boolean(value, gst_pylon_object_get_pylon_property<
                                       gboolean, Pylon::CBooleanParameter>(
                                       *priv->nodemap, pspec->name));
        break;
      case G_TYPE_FLOAT:
        g_value_set_float(
            value,
            gst_pylon_object_get_pylon_property<gfloat, Pylon::CFloatParameter>(
                *priv->nodemap, pspec->name));
        break;
      case G_TYPE_STRING:
        g_value_set_string(
            value, gst_pylon_object_get_pylon_property<GenICam::gcstring,
                                                       Pylon::CStringParameter>(
                       *priv->nodemap, pspec->name)
                       .c_str());
        break;
      case G_TYPE_ENUM:
        g_value_set_enum(value, gst_pylon_object_get_enum_property(
                                    *priv->nodemap, pspec->name));
        break;
      default:
        g_warning("Unsupported GType: %s", g_type_name(pspec->value_type));
        std::string msg =
            "Unsupported GType: " + std::string(g_type_name(pspec->value_type));
        throw Pylon::GenericException(msg.c_str(), __FILE__, __LINE__);
    }
  } catch (const Pylon::GenericException& e) {
    GST_ERROR("Unable to get pylon property \"%s\" on \"%s\": %s", pspec->name,
              priv->camera->GetDeviceInfo().GetFriendlyName().c_str(),
              e.GetDescription());
  }
}

GObject* gst_pylon_object_new(
    std::shared_ptr<Pylon::CBaslerUniversalInstantCamera> camera,
    const Pylon::String_t& device_name, GenApi::INodeMap* nodemap) {
  gchar* type_name = gst_pylon_param_spec_sanitize_name(device_name.c_str());

  GType type = g_type_from_name(type_name);
  GObject* obj = G_OBJECT(g_object_new(type, "name", type_name, NULL));
  GstPylonObject* self = (GstPylonObject*)obj;
  GstPylonObjectPrivate* priv =
      (GstPylonObjectPrivate*)gst_pylon_object_get_instance_private(self);
  g_free(type_name);

  priv->camera = std::move(camera);
  priv->nodemap = nodemap;

  return obj;
}

static void gst_pylon_object_finalize(GObject* object) {
  GstPylonObject* self = (GstPylonObject*)object;
  GstPylonObjectPrivate* priv =
      (GstPylonObjectPrivate*)gst_pylon_object_get_instance_private(self);

  priv->camera = NULL;

  G_OBJECT_CLASS(gst_pylon_object_parent_class)->finalize(object);
}
