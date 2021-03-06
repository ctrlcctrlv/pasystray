/***
  This file is part of PaSystray

  Copyright (C) 2011, 2012 Christoph Gysin

  PaSystray is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PaSystray is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PaSystray; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include "pulseaudio.h"
#include "pulseaudio_info.h"
#include "systray.h"
#include "notify.h"

#include <pulse/glib-mainloop.h>

pa_context* context = NULL;

static pa_glib_mainloop* m = NULL;
static pa_proplist* context_proplist = NULL;
static char* server = NULL;

void pulseaudio_init(menu_infos_t* mis)
{
    if(!(m = pa_glib_mainloop_new(g_main_context_default())))
        pulseaudio_quit("pa_glib_mainloop_new() failed.");

    pulseaudio_prepare_context();
    pa_context_set_state_callback(context, pulseaudio_context_state_cb, mis);

    pulseaudio_connect();
}

void pulseaudio_destroy()
{
    pulseaudio_quit(NULL);
}

void pulseaudio_prepare_context()
{
    pa_mainloop_api* mainloop_api = pa_glib_mainloop_get_api(m);

    context_proplist = pa_proplist_new();
    pa_proplist_sets(context_proplist, PA_PROP_APPLICATION_NAME, "PulseAudio systray");

    context = pa_context_new_with_proplist(mainloop_api, NULL, context_proplist);
    if(!context)
        pulseaudio_quit("pa_context_new() failed.");
}

void pulseaudio_connect()
{
    if(pa_context_connect(context, server, 0, NULL) < 0)
    {
        g_message("pa_context_connect() failed: ");
        pulseaudio_quit(pa_strerror(pa_context_errno(context)));
    }
}

void pulseaudio_context_state_cb(pa_context* c, void* userdata)
{
    menu_infos_t* mis = userdata;

    switch(pa_context_get_state(c))
    {
        case PA_CONTEXT_UNCONNECTED:
            g_message("PulseAudio context unconnected!");
            break;

        case PA_CONTEXT_READY:
        {
            char* tooltip = context_info_str(context);
            char* escaped = g_markup_escape_text(tooltip, -1);
            char* markup = g_strdup_printf(
                    "<span font_family=\"monospace\" font_size=\"x-small\">%s</span>", escaped);
            gtk_status_icon_set_tooltip_markup(mis->icon, markup);
            g_free(escaped);
            g_free(tooltip);
            g_free(markup);

            pa_context_set_subscribe_callback(context, pulseaudio_event_cb, mis);
            pa_operation_unref(pa_context_subscribe(context,
                        PA_SUBSCRIPTION_MASK_SINK |
                        PA_SUBSCRIPTION_MASK_SOURCE |
                        PA_SUBSCRIPTION_MASK_SINK_INPUT |
                        PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT |
                        PA_SUBSCRIPTION_MASK_SERVER,
                        pulseaudio_subscribed_cb, mis));
            break;
        }

        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            g_message("PulseAudio terminated!");
            menu_infos_clear(mis);
            pa_context_unref(context);
            pulseaudio_prepare_context();
            g_message("reconnecting...");
            pulseaudio_connect();
            break;

        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
    }
}

void pulseaudio_subscribed_cb(pa_context* c, int success, void* userdata)
{
    menu_infos_t* mis = userdata;
    pa_operation_unref(pa_context_get_server_info(context,
                pulseaudio_server_init_cb, &mis->menu_info[MENU_SERVER]));
    pa_operation_unref(pa_context_get_sink_info_list(context,
                pulseaudio_sink_init_cb, &mis->menu_info[MENU_SINK]));
    pa_operation_unref(pa_context_get_source_info_list(context,
                pulseaudio_source_init_cb, &mis->menu_info[MENU_SOURCE]));
    pa_operation_unref(pa_context_get_sink_input_info_list(context,
                pulseaudio_sink_input_init_cb, &mis->menu_info[MENU_INPUT]));
    pa_operation_unref(pa_context_get_source_output_info_list(context,
                pulseaudio_source_output_init_cb, &mis->menu_info[MENU_OUTPUT]));
}

void pulseaudio_event_cb(pa_context* c, pa_subscription_event_type_t t, uint32_t index, void* userdata)
{
#ifdef DEBUG
    pulseaudio_print_event(t, index);
#endif

    menu_infos_t* mis = userdata;

    pa_subscription_event_type_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    pa_subscription_event_type_t facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    switch(type)
    {
        case PA_SUBSCRIPTION_EVENT_NEW:
            pulseaudio_event_new(facility, index, mis);
            break;
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            pulseaudio_event_change(facility, index, mis);
            break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            pulseaudio_event_remove(facility, index, mis);
            break;
        default:
            break;
    }
}

void pulseaudio_event_new(pa_subscription_event_type_t facility, uint32_t index, menu_infos_t* mis)
{
    switch(facility)
    {
        case PA_SUBSCRIPTION_EVENT_SERVER:
            pa_operation_unref(pa_context_get_server_info(context,
                        pulseaudio_server_add_cb, &mis->menu_info[MENU_SERVER]));
            break;
        case PA_SUBSCRIPTION_EVENT_SINK:
            pa_operation_unref(pa_context_get_sink_info_by_index(context, index,
                        pulseaudio_sink_add_cb, &mis->menu_info[MENU_SINK]));
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE:
            pa_operation_unref(pa_context_get_source_info_by_index(context, index,
                        pulseaudio_source_add_cb, &mis->menu_info[MENU_SOURCE]));
            break;
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
            pa_operation_unref(pa_context_get_sink_input_info(context, index,
                        pulseaudio_sink_input_add_cb, &mis->menu_info[MENU_INPUT]));
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
            pa_operation_unref(pa_context_get_source_output_info(context, index,
                        pulseaudio_source_output_add_cb, &mis->menu_info[MENU_OUTPUT]));
            break;
        default:
            break;
    }
}

void pulseaudio_event_change(pa_subscription_event_type_t facility, uint32_t index, menu_infos_t* mis)
{
    switch(facility)
    {
        case PA_SUBSCRIPTION_EVENT_SERVER:
            pa_operation_unref(pa_context_get_server_info(context,
                        pulseaudio_server_change_cb, &mis->menu_info[MENU_SERVER]));
            break;
        case PA_SUBSCRIPTION_EVENT_SINK:
            pa_operation_unref(pa_context_get_sink_info_by_index(context, index,
                        pulseaudio_sink_change_cb, &mis->menu_info[MENU_SINK]));
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE:
            pa_operation_unref(pa_context_get_source_info_by_index(context, index,
                        pulseaudio_source_change_cb, &mis->menu_info[MENU_SOURCE]));
            break;
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
            pa_operation_unref(pa_context_get_sink_input_info(context, index,
                        pulseaudio_sink_input_change_cb, &mis->menu_info[MENU_INPUT]));
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
            pa_operation_unref(pa_context_get_source_output_info(context, index,
                        pulseaudio_source_output_change_cb, &mis->menu_info[MENU_OUTPUT]));
            break;
        default:
            break;
    }
}

void pulseaudio_event_remove(pa_subscription_event_type_t facility, uint32_t index, menu_infos_t* mis)
{
    switch(facility)
    {
        case PA_SUBSCRIPTION_EVENT_SERVER:
            menu_info_item_remove(&mis->menu_info[MENU_SERVER], index);
            break;
        case PA_SUBSCRIPTION_EVENT_SINK:
            menu_info_item_remove(&mis->menu_info[MENU_SINK], index);
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE:
            menu_info_item_remove(&mis->menu_info[MENU_SOURCE], index);
            break;
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
            menu_info_item_remove(&mis->menu_info[MENU_INPUT], index);
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
            menu_info_item_remove(&mis->menu_info[MENU_OUTPUT], index);
            break;
        default:
            break;
    }
}

#ifdef DEBUG
void pulseaudio_print_event(pa_subscription_event_type_t t, uint32_t index)
{
    pa_subscription_event_type_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    pa_subscription_event_type_t facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    g_message("event %s on %s (%u)",
        (type == PA_SUBSCRIPTION_EVENT_NEW) ? "new" :
        (type == PA_SUBSCRIPTION_EVENT_CHANGE) ? "change" :
        (type == PA_SUBSCRIPTION_EVENT_REMOVE) ? "remove" :
        "unknown",
        (facility == PA_SUBSCRIPTION_EVENT_SINK) ? "sink" :
        (facility == PA_SUBSCRIPTION_EVENT_SOURCE) ? "source" :
        (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) ? "sink-input" :
        (facility == PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT) ? "source-output" :
        (facility == PA_SUBSCRIPTION_EVENT_SERVER) ? "server" :
        "unknown",
        index);
}
#endif

void pulseaudio_server_init_cb(pa_context* c, const pa_server_info* i, void* userdata)
{
    pulseaudio_server_add(i, userdata, FALSE);
}

void pulseaudio_server_add_cb(pa_context* c, const pa_server_info* i, void* userdata)
{
    pulseaudio_server_add(i, userdata, TRUE);
}

void pulseaudio_server_change_cb(pa_context* c, const pa_server_info* i, void* userdata)
{
    pulseaudio_server_add(i, userdata, FALSE);
}

void pulseaudio_server_add(const pa_server_info* i, void* userdata, gboolean is_new)
{
    menu_info_t* mi = userdata;
    char* tooltip = server_info_str(i);
    menu_info_item_update(mi, 0, "localhost", i->host_name, NULL, 0, tooltip, NULL, NULL);
    g_free(tooltip);

    /* set default sink/source */
    pulseaudio_change_default_item(&mi->menu_infos->menu_info[MENU_SINK], i->default_sink_name);
    pulseaudio_change_default_item(&mi->menu_infos->menu_info[MENU_SOURCE], i->default_source_name);
}

void pulseaudio_change_default_item(menu_info_t* mi, const char* new_default)
{
    g_free(mi->default_name);
    mi->default_name = g_strdup(new_default);

    menu_info_item_t* item = menu_info_item_get_by_name(mi, new_default);

    if(item)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item->widget), true);
}

void pulseaudio_sink_init_cb(pa_context* c, const pa_sink_info* i, int is_last, void* userdata)
{
    pulseaudio_sink_add(i, is_last, userdata, FALSE);
}

void pulseaudio_sink_add_cb(pa_context* c, const pa_sink_info* i, int is_last, void* userdata)
{
    pulseaudio_sink_add(i, is_last, userdata, TRUE);
}

void pulseaudio_sink_change_cb(pa_context* c, const pa_sink_info* i, int is_last, void* userdata)
{
    pulseaudio_sink_add(i, is_last, userdata, FALSE);
}

void pulseaudio_sink_add(const pa_sink_info* i, int is_last, void* userdata, gboolean is_new)
{
    if(is_last < 0)
    {
        g_message("Failed to get sink information: %s",
                pa_strerror(pa_context_errno(context)));
        return;
    }

    if(is_last)
        return;

    if(is_new)
    {
        gchar* msg = g_strdup_printf("new sink \"%s\"", i->description);
        notify(msg, i->name, NULL);
        g_free(msg);
    }

    menu_info_t* mi = userdata;
    char* tooltip = sink_info_str(i);
    menu_info_item_update(mi, i->index, i->name, i->description, &i->volume,
            i->mute, tooltip, NULL, NULL);
    g_free(tooltip);
}

void pulseaudio_source_init_cb(pa_context* c, const pa_source_info* i, int is_last, void* userdata)
{
    pulseaudio_source_add(i, is_last, userdata, FALSE);
}

void pulseaudio_source_add_cb(pa_context* c, const pa_source_info* i, int is_last, void* userdata)
{
    pulseaudio_source_add(i, is_last, userdata, TRUE);
}

void pulseaudio_source_change_cb(pa_context* c, const pa_source_info* i, int is_last, void* userdata)
{
    pulseaudio_source_add(i, is_last, userdata, FALSE);
}

void pulseaudio_source_add(const pa_source_info* i, int is_last, void* userdata, gboolean is_new)
{
    if(is_last < 0)
    {
        g_message("Failed to get source information: %s", pa_strerror(pa_context_errno(context)));
        return;
    }

    if(is_last)
        return;

    const char* class = pa_proplist_gets(i->proplist, PA_PROP_DEVICE_CLASS);

    // ignore monitors
    if(class && g_str_equal(class, "monitor"))
        return;

    if(is_new)
    {
        gchar* msg = g_strdup_printf("new source \"%s\"", i->description);
        notify(msg, i->name, NULL);
        g_free(msg);
    }

    menu_info_t* mi = userdata;
    char* tooltip = source_info_str(i);
    menu_info_item_update(mi, i->index, i->name, i->description, &i->volume,
            i->mute, tooltip, NULL, NULL);
    g_free(tooltip);
}

void pulseaudio_sink_input_init_cb(pa_context* c, const pa_sink_input_info* i, int is_last, void* userdata)
{
    pulseaudio_sink_input_add(i, is_last, userdata, FALSE);
}

void pulseaudio_sink_input_add_cb(pa_context* c, const pa_sink_input_info* i, int is_last, void* userdata)
{
    pulseaudio_sink_input_add(i, is_last, userdata, TRUE);
}

void pulseaudio_sink_input_change_cb(pa_context* c, const pa_sink_input_info* i, int is_last, void* userdata)
{
    pulseaudio_sink_input_add(i, is_last, userdata, FALSE);
}

void pulseaudio_sink_input_add(const pa_sink_input_info* i, int is_last, void* userdata, gboolean is_new)
{
    if(is_last < 0)
    {
        g_message("Failed to get sink information: %s", pa_strerror(pa_context_errno(context)));
        return;
    }

    if(is_last)
        return;

    // ignore filters
    const char* media_role = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_ROLE);
    if(media_role && g_str_equal(media_role, "filter"))
        return;

    const char* app_name = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    const char* app_icon = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_ICON_NAME);

    menu_info_t* mi = userdata;
    char* tooltip = input_info_str(i);
    menu_info_item_update(mi, i->index, NULL, app_name ? app_name : i->name,
            &i->volume, i->mute, tooltip, app_icon, NULL);
    g_free(tooltip);
}

void pulseaudio_source_output_init_cb(pa_context* c, const pa_source_output_info* i, int is_last, void* userdata)
{
    pulseaudio_source_output_add(i, is_last, userdata, FALSE);
}

void pulseaudio_source_output_add_cb(pa_context* c, const pa_source_output_info* i, int is_last, void* userdata)
{
    pulseaudio_source_output_add(i, is_last, userdata, TRUE);
}

void pulseaudio_source_output_change_cb(pa_context* c, const pa_source_output_info* i, int is_last, void* userdata)
{
    pulseaudio_source_output_add(i, is_last, userdata, FALSE);
}

void pulseaudio_source_output_add(const pa_source_output_info* i, int is_last, void* userdata, gboolean is_new)
{
    if(is_last < 0)
    {
        g_message("Failed to get source information: %s", pa_strerror(pa_context_errno(context)));
        return;
    }

    if(is_last)
        return;

    // ignore peak detects
    const char* media_name = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_NAME);
    if(media_name && g_str_equal(media_name, "Peak detect"))
        return;

    const char* app_name = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    const char* app_icon = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_ICON_NAME);

    menu_info_t* mi = userdata;
    char* tooltip = output_info_str(i);
    menu_info_item_update(mi, i->index, NULL, app_name ? app_name : i->name,
            &i->volume, i->mute, tooltip, app_icon, NULL);
    g_free(tooltip);
}

void pulseaudio_quit(const char* msg)
{
    if(msg)
        g_message("%s", msg);

    if(context)
        pa_context_unref(context);

    if(m)
    {
        pa_signal_done();
        pa_glib_mainloop_free(m);
    }
}
