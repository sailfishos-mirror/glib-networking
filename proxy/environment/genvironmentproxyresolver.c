/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * GIO - GLib Input, Output and Streaming Library
 *
 * Copyright 2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "genvironmentproxyresolver.h"

#include <glib.h>
#include <glib/gi18n-lib.h>

struct _GEnvironmentProxyResolver {
  GObject parent_instance;
};

static void g_environment_proxy_resolver_iface_init (GProxyResolverInterface *iface);

#ifdef GENVIRONMENTPROXY_MODULE
static void
g_environment_proxy_resolver_class_finalize (GEnvironmentProxyResolverClass *klass)
{
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (GEnvironmentProxyResolver,
                                g_environment_proxy_resolver,
                                G_TYPE_OBJECT, 0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (G_TYPE_PROXY_RESOLVER,
                                                               g_environment_proxy_resolver_iface_init))
#else
G_DEFINE_TYPE_EXTENDED (GEnvironmentProxyResolver,
                        g_environment_proxy_resolver,
                        G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_PROXY_RESOLVER,
                                               g_environment_proxy_resolver_iface_init))
#endif

static void
g_environment_proxy_resolver_init (GEnvironmentProxyResolver *resolver)
{
}

static gboolean
g_environment_proxy_resolver_is_supported (GProxyResolver *object)
{
  return g_getenv ("ftp_proxy") || g_getenv ("FTP_PROXY") ||
         g_getenv ("https_proxy") || g_getenv ("HTTPS_PROXY") ||
         g_getenv ("http_proxy") || g_getenv ("HTTP_PROXY") ||
         g_getenv ("no_proxy") || g_getenv ("NO_PROXY");
}

static gchar **
proxy_results (const gchar *uri)
{
  gchar **proxies = g_new0 (gchar *, 2);
  proxies[0] = g_strdup (uri);
  return proxies;
}

static gboolean
is_uri_ignored (const gchar *uri,
                const gchar *ignore_pattern)
{
  GUri *guri = NULL;
  GUri *ignore_guri = NULL;
  const char *host;
  const char *ignore_host;
  int port;
  int ignore_port;
  gboolean ignored = FALSE;
  GError *error = NULL;

  guri = g_uri_parse (uri, G_URI_FLAGS_NONE, &error);
  if (!guri)
    {
      g_warning ("Failed to parse URI %s: %s", uri, error->message);
      g_error_free (error);
      goto out;
    }
  host = g_uri_get_host (guri);
  if (!host)
    goto out;
  port = g_uri_get_port (guri);

  ignore_guri = g_uri_parse (ignore_pattern, G_URI_FLAGS_NONE, &error);
  if (!ignore_guri)
    {
      g_warning ("Failed to parse URI %s: %s", ignore_pattern, error->message);
      g_error_free (error);
      goto out;
    }
  ignore_host = g_uri_get_host (ignore_guri);
  if (!ignore_host)
    goto out;
  ignore_port = g_uri_get_port (ignore_guri);

  /* libproxy's no_proxy config is implement in its ignore_domain.cpp,
   * ignore_hostname.cpp, and ignore_ip.cpp. It's smarter than ours. I've just
   * copied the basic domain checks from libproxy's ignore_domain.cpp. (Note the
   * glob check is *really* strict.)
   */

	/* Hostname match (domain.com or domain.com:80) */
	if (strcmp (host, ignore_host) == 0)
		return (ignore_port == 0 || port == ignore_port);

	/* Endswith (.domain.com or .domain.com:80) */
	if (ignore_host[0] == '.' && g_str_has_suffix (host, ignore_host))
		return (ignore_port == 0 || port == ignore_port);

	/* Glob (*.domain.com or *.domain.com:80). */
	if (ignore_host[0] == '*' && g_str_has_suffix (host, ignore_host + 1))
		return (ignore_port == 0 || port == ignore_port);

out:
  if (guri)
    g_uri_unref (guri);
  if (ignore_guri)
    g_uri_unref (ignore_guri);
  return ignored;
}

static gchar **
g_environment_proxy_resolver_lookup (GProxyResolver  *resolver,
                                     const gchar     *uri,
                                     GCancellable    *cancellable,
                                     GError         **error)
{
  gchar *scheme = g_uri_parse_scheme (uri);
  gchar **no_proxy = NULL;

  if (g_getenv ("no_proxy"))
    no_proxy = g_strsplit (g_getenv ("no_proxy"), ",", -1);
  else if (g_getenv ("NO_PROXY"))
    no_proxy = g_strsplit (g_getenv ("NO_PROXY"), ",", -1);

  if (no_proxy)
    {
      int len = g_strv_length (no_proxy);
      gboolean ignored = FALSE;

      for (int i = 0; i < len; i++)
        {
          if (is_uri_ignored (uri, no_proxy[i]))
            {
              ignored = TRUE;
              break;
            }
        }
      g_strfreev (no_proxy);

      if (ignored)
        return proxy_results ("direct://");
    }

  /* The order of precedence of the environment variables is copied from
   * libproxy's config_envvar.cpp. We match it carefully. Note that the
   * HTTP proxy is used for all schemes, not just HTTP.
   */
  if (g_ascii_strcasecmp (scheme, "ftp") == 0)
    {
      if (g_getenv ("ftp_proxy"))
        return proxy_results (g_getenv ("ftp_proxy"));
      if (g_getenv ("FTP_proxy"))
        return proxy_results (g_getenv ("FTP_PROXY"));
    }

  if (g_ascii_strcasecmp (scheme, "https") == 0)
    {
      if (g_getenv ("https_proxy"))
        return proxy_results (g_getenv ("https_proxy"));
      if (g_getenv ("HTTPS_proxy"))
        return proxy_results (g_getenv ("HTTPS_PROXY"));
    }

  if (g_getenv ("http_proxy"))
    return proxy_results (g_getenv ("http_proxy"));

  if (g_getenv ("HTTP_PROXY"))
    return proxy_results (g_getenv ("HTTP_PROXY"));

  return g_new0 (gchar *, 1);
}

static void
g_environment_proxy_resolver_lookup_async (GProxyResolver      *resolver,
                                           const gchar         *uri,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  GTask *task;
  gchar **proxies;

  proxies = g_environment_proxy_resolver_lookup (resolver, uri, cancellable, NULL);

  task = g_task_new (resolver, cancellable, callback, user_data);
  g_task_set_source_tag (task, g_environment_proxy_resolver_lookup_async);
  g_task_set_name (task, "[glib-networking] g_environment_proxy_resolver_lookup_async");
  g_task_set_task_data (task, g_strdup (uri), g_free);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_return_pointer (task, proxies, (GDestroyNotify)g_strfreev);
  g_object_unref (task);
}

static gchar **
g_environment_proxy_resolver_lookup_finish (GProxyResolver     *resolver,
                                            GAsyncResult       *result,
                                            GError            **error)
{
  g_return_val_if_fail (g_task_is_valid (result, resolver), NULL);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == g_environment_proxy_resolver_lookup_async, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
g_environment_proxy_resolver_class_init (GEnvironmentProxyResolverClass *resolver_class)
{
}

static void
g_environment_proxy_resolver_iface_init (GProxyResolverInterface *iface)
{
  iface->is_supported = g_environment_proxy_resolver_is_supported;
  iface->lookup = g_environment_proxy_resolver_lookup;
  iface->lookup_async = g_environment_proxy_resolver_lookup_async;
  iface->lookup_finish = g_environment_proxy_resolver_lookup_finish;
}

#ifdef GENVIRONMENTPROXY_MODULE
void
g_environment_proxy_resolver_register (GIOModule *module)
{
  g_environment_proxy_resolver_register_type (G_TYPE_MODULE (module));
  if (!module)
    g_io_extension_point_register (G_PROXY_RESOLVER_EXTENSION_POINT_NAME);
  g_io_extension_point_implement (G_PROXY_RESOLVER_EXTENSION_POINT_NAME,
                                  g_environment_proxy_resolver_get_type(),
                                  "environment",
                                  100);
}
#endif
