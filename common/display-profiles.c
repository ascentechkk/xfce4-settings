/*
 *  Copyright (c) 2019 Simon Steinbeiß <simon@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <glib.h>
#include <display-profiles.h>

gint get_size (gchar **i);

gint
get_size (gchar **i) {
    gint num = 0;
    while (*i != NULL) {
        num++;
        i++;
    }
    return num;
}

gboolean
display_settings_profile_name_exists (XfconfChannel *channel, const gchar *new_profile_name)
{
    GHashTable *properties;
    GList *channel_contents, *current;

    properties = xfconf_channel_get_properties (channel, NULL);
    channel_contents = g_hash_table_get_keys (properties);

    /* get all profiles */
    current = g_list_first (channel_contents);
    while (current)
    {
        gchar **current_elements = g_strsplit (current->data, "/", -1);
        gchar *old_profile_name;

        if (get_size (current_elements) != 2)
        {
            g_strfreev (current_elements);
            current = g_list_next (current);
            continue;
        }

        old_profile_name = xfconf_channel_get_string (channel, current->data, NULL);
        if (g_strcmp0 (new_profile_name, old_profile_name) == 0)
        {
            g_free (old_profile_name);
            return FALSE;
        }
        g_free (old_profile_name);

        current = g_list_next (current);
    }
    g_list_free (channel_contents);
    g_hash_table_destroy (properties);
    return TRUE;
}

void
rl_display_infos_free (GArray *display_infos)
{
    for (int i = 0; i < display_infos->len; i++)
    {
        RLDisplayInfo *item = g_array_index (display_infos, RLDisplayInfo*, i);
        g_free (item->edid);
        g_free (item->rlmodel);
        g_slice_free (RLDisplayInfo, item);
    }
    g_array_free (display_infos, TRUE);
}

static gboolean
rl_match_profile(RLDisplayInfo *display_info, gchar *edid, gchar *rlmodel, gboolean rlmodel_enabled)
{
    if (rlmodel_enabled && rlmodel && strlen (rlmodel) > 0)
        return g_strcmp0 (display_info->rlmodel, rlmodel) == 0;

    if (edid)
        return g_strcmp0 (display_info->edid, edid) == 0;

    return FALSE;
}

GList*
display_settings_get_profiles (GArray *display_infos, XfconfChannel *channel)
{
    GHashTable *properties;
    GList      *channel_contents;
    GList      *profiles = NULL;
    GList      *current;
    guint       m;
    guint       noutput;
    gboolean    rlmodel_enabled;

    properties = xfconf_channel_get_properties (channel, NULL);
    channel_contents = g_hash_table_get_keys (properties);
    noutput = display_infos->len;
    rlmodel_enabled = xfconf_channel_get_bool (channel, "/RLModelEnabled", FALSE);

    /* get all profiles */
    current = g_list_first (channel_contents);
    while (current)
    {
        GHashTable     *props;
        gchar          *property_profile;
        GHashTableIter  iter;
        gpointer        key, value;
        guint           profile_match = 0;
        guint           monitors = 0;
        gchar         **current_elements = g_strsplit (current->data, "/", -1);
        gchar          *profile_name;
        RLDisplayInfo  *display_info;

        /* Only process the profiles and skip all other xfconf properties */
        /* If xfconf ever supports just getting the first-level children of a property
           we could replace this */
        if (get_size (current_elements) != 2)
        {
            g_strfreev (current_elements);
            current = g_list_next (current);
            continue;
        }

        profile_name = g_strdup_printf ("%s", *(current_elements + 1));
        g_strfreev (current_elements);

        /* Walk through the profile and check if every EDID referenced there is also currently available */
        property_profile = g_strdup_printf ("/%s", profile_name);
        props = xfconf_channel_get_properties (channel, property_profile);
        g_hash_table_iter_init (&iter, props);

        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            gchar *property;
            gchar *current_edid;
            gchar *current_rlmodel;

            gchar ** property_elements = g_strsplit (key, "/", -1);
            if (get_size (property_elements) == 3) {
                monitors++;

                property = g_strdup_printf ("%s/EDID", (gchar*)key);
                current_edid = xfconf_channel_get_string (channel, property, NULL);

                property = g_strdup_printf ("%s/RLMODEL", (gchar*)key);
                current_rlmodel = xfconf_channel_get_string (channel, property, NULL);

                if (current_edid || current_rlmodel) {

                    for (m = 0; m < noutput; ++m)
                    {
                        display_info = g_array_index (display_infos, RLDisplayInfo*, m);
                        if (rl_match_profile (display_info, current_edid, current_rlmodel, rlmodel_enabled))
                        {
                            profile_match ++;
                        }
                    }
                }
                g_free (property);
                g_free (current_edid);
            }

            g_strfreev (property_elements);
        }
        g_free (property_profile);
        g_hash_table_destroy (props);

        /* filter the content of the combobox to only matching profiles and exclude "Notify", "Default" and "Schemes" */
        if (!g_list_find_custom (profiles, profile_name, (GCompareFunc) strcmp) &&
            strcmp (profile_name, "Notify") &&
            strcmp (profile_name, "Default") &&
            strcmp (profile_name, "Schemes") &&
            profile_match == monitors &&
            noutput == profile_match)
        {
            profiles = g_list_prepend (profiles, g_strdup (profile_name));
        }
        /* else don't add the profile to the list */
        current = g_list_next (current);
        g_free (profile_name);
    }

    g_list_free (channel_contents);
    g_hash_table_destroy (properties);

    return profiles;
}
