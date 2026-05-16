#include <libsoup-3.0/libsoup/soup.h>
#include <rest-1.0/rest/rest.h>
#include <glib.h>
#include <glib-object.h>

#define YOUTUBE_API_BASE_URL "https://www.googleapis.com/youtube/v3/"
#define YOUTUBE_API_AUTH_URL "https://accounts.google.com/o/oauth2/v2/auth"
#define YOUTUBE_API_TOKEN_URL "https://oauth2.googleapis.com/token"
#define YOUTUBE_API_SCOPE "https://www.googleapis.com/auth/youtube.force-ssl"
#define LOOPBACK_REDIRECT_URL "http://127.0.0.1:43215"

static
void on_token_refresh(GObject* source_object, GAsyncResult* result, gpointer data)
{
    GError* error = NULL;
    RestOAuth2Proxy* proxy = REST_OAUTH2_PROXY(source_object);
    GMainLoop* main_loop = (GMainLoop*)data;

    // TODO: patch that adds error checking for OAuth JSON responses
    // TODO: patch that adds client_secret to refresh_token request
    // TODO: patch that fixes ownership of getter
    // TODO: patch that uses crypto-grade randomness for PKCE
    gboolean success = rest_oauth2_proxy_refresh_access_token_finish(proxy, result, &error);
    if(error || !success) {
        g_printerr("Error: %s\n", error->message);
        g_main_loop_quit(main_loop);
    } else {
        g_printerr("New access token: %s\n", rest_oauth2_proxy_get_access_token(proxy));
        g_printerr("New refresh token: %s\n", rest_oauth2_proxy_get_refresh_token(proxy));
    }
}

int main(void)
{
    char** env = g_get_environ();
    const char* client_id = g_environ_getenv(env, "YT_CLIENT_ID");
    const char* client_secret = g_environ_getenv(env, "YT_CLIENT_SECRET");
    const char* access_token = g_environ_getenv(env, "YT_ACCESS_TOKEN");
    const char* refresh_token = g_environ_getenv(env, "YT_REFRESH_TOKEN");
    const char* expiration = g_environ_getenv(env, "YT_EXPIRATION");

    GMainLoop* main_loop = g_main_loop_new(g_main_context_default(), /*is_running=*/FALSE);
    RestOAuth2Proxy* proxy = rest_oauth2_proxy_new(
        YOUTUBE_API_AUTH_URL,
        YOUTUBE_API_TOKEN_URL,
        LOOPBACK_REDIRECT_URL,
        client_id,
        client_secret,
        YOUTUBE_API_BASE_URL
    );
    SoupLogger* logger = soup_logger_new(SOUP_LOGGER_LOG_BODY);
    rest_proxy_add_soup_feature(REST_PROXY(proxy), SOUP_SESSION_FEATURE(logger));
    rest_oauth2_proxy_set_access_token(proxy, access_token);
    rest_oauth2_proxy_set_refresh_token(proxy, refresh_token);
    rest_oauth2_proxy_refresh_access_token_async(proxy, NULL, on_token_refresh, main_loop);

    g_main_loop_run(main_loop);
    g_main_loop_unref(main_loop);

    g_strfreev(env);
    return 0;
}
