#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "app.h"
#include "settings.h"
#include "rfid.h"
#include "diag.h"
#include "web_pages.h"
#include "web_assets.h"
#include "opentag3d.h"
#include "spoolman.h"
#include "moonraker.h"

static WebServer server(80);
static DNSServer dns;
static bool dnsRunning = false;
static bool uploadAuthorized = false;

// Settings and maintenance need the admin password when one is set. Status
// and tag reads stay open so the diagnostics page works for troubleshooting.
static bool authOk()
{
  if (cfg.adminPass[0] == '\0' || server.authenticate("admin", cfg.adminPass))
    return true;
  server.requestAuthentication(BASIC_AUTH, "StorOn");
  return false;
}

static void sendJson(int code, JsonDocument &doc)
{
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

// Stream adapter: whatever is written goes out as chunked HTTP content, in
// 4 KB chunks (small chunks make a large reply crawl). Call flush() at the end.
class ChunkOut : public Stream
{
  char buf[4096];
  size_t used = 0;

public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *data, size_t n) override
  {
    for (size_t done = 0; done < n;)
    {
      size_t take = min(n - done, sizeof(buf) - used);
      memcpy(buf + used, data + done, take);
      used += take;
      done += take;
      if (used == sizeof(buf))
        flush();
    }
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override
  {
    if (used)
      server.sendContent(buf, used);
    used = 0;
  }
};

static void sendError(int code, const String &msg)
{
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(code, doc);
}

void setupWeb()
{
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", STATUS_HTML); });
  server.on("/style.css", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "max-age=3600");
    server.send_P(200, "text/css", STYLE_CSS);
  });
  // Branding images, cached by the browser for a day
  struct Asset
  {
    const char *path;
    const uint8_t *data;
    size_t len;
  };
  static const Asset ASSETS[] = {{"/logo.png", LOGO_PNG, LOGO_PNG_LEN},
                                 {"/favicon.png", FAVICON_PNG, FAVICON_PNG_LEN},
                                 {"/apple-touch-icon.png", ICON180_PNG, ICON180_PNG_LEN}};
  for (const Asset &a : ASSETS)
    server.on(a.path, HTTP_GET, [&a]() {
      server.sendHeader("Cache-Control", "max-age=86400");
      server.send_P(200, "image/png", (const char *)a.data, a.len);
    });

  server.on("/readers", HTTP_GET, []() { server.send_P(200, "text/html", READERS_HTML); });

  server.on("/settings", HTTP_GET, []() {
    if (authOk())
      server.send_P(200, "text/html", SETTINGS_HTML);
  });

  server.on("/api/status", HTTP_GET, []() {
    JsonDocument doc;
    appStatusJson(doc);
    sendJson(200, doc);
  });

  server.on("/api/lane/dump", HTTP_GET, []() {
    JsonDocument doc;
    rfidDumpJson(server.arg("lane").toInt() - 1, doc);
    sendJson(doc["error"].isNull() ? 200 : 404, doc);
  });

  server.on("/api/lane/diag", HTTP_GET, []() {
    JsonDocument doc;
    rfidDiagJson(server.arg("lane").toInt() - 1, doc, server.hasArg("drive") ? server.arg("drive").toInt() : -1);
    sendJson(doc["error"].isNull() ? 200 : 404, doc);
  });

  server.on("/api/ot3d/fields", HTTP_GET, []() {
    JsonDocument doc;
    ot3dFieldsJson(doc.to<JsonArray>());
    sendJson(200, doc);
  });

  // Pre-fill the tag writer from a Spoolman spool
  server.on("/api/spoolman/spool", HTTP_GET, []() {
    JsonDocument doc;
    String err;
    if (!spoolmanFetch(server.arg("id").toInt(), doc, err))
    {
      sendError(400, err);
      return;
    }
    doc["ok"] = true;
    sendJson(200, doc);
  });

  // Write OpenTag3D data to the sticker on a lane.
  // Body: {"fields": {...}, "spoolman_id": N (optional, saves the tag ID there)}
  server.on("/api/lane/write", HTTP_POST, []() {
    if (!authOk())
      return;
    JsonDocument body;
    if (deserializeJson(body, server.arg("plain")) || !body["fields"].is<JsonObjectConst>())
    {
      sendError(400, "Invalid request");
      return;
    }
    JsonDocument res;
    int lane = server.arg("lane").toInt() - 1;
    if (!rfidWriteTag(lane, body["fields"].as<JsonObjectConst>(), res))
    {
      res["ok"] = false;
      sendJson(400, res);
      return;
    }
    res["ok"] = true;
    int spoolId = body["spoolman_id"] | 0;
    if (spoolId > 0)
    {
      String err;
      if (spoolmanSetTag(spoolId, res["uid"], err))
        res["spoolman"] = String("Tag ID saved to Spoolman spool #") + spoolId;
      else
        res["spoolman_error"] = err;
    }
    sendJson(200, res);
  });

  // Link the tag in a lane to a Spoolman spool. Body: {"spool_id": N}
  server.on("/api/lane/link", HTTP_POST, []() {
    if (!authOk())
      return;
    JsonDocument body;
    int spoolId = deserializeJson(body, server.arg("plain")) ? 0 : (body["spool_id"] | 0);
    if (spoolId <= 0)
    {
      sendError(400, "Pick a Spoolman spool");
      return;
    }
    String err;
    if (!spoolSyncLink(server.arg("lane").toInt() - 1, spoolId, body["move"] | false,
                       body["archive_old"] | false, err))
    {
      sendError(400, err);
      return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Create a Spoolman spool for the lane's tag. Optional JSON body: see
  // spoolCreateForLane. ?dry_run=1 returns the plan without creating anything.
  server.on("/api/lane/create", HTTP_POST, []() {
    if (!authOk())
      return;
    JsonDocument opts, res;
    if (server.hasArg("plain") && server.arg("plain").length())
      deserializeJson(opts, server.arg("plain"));
    String err;
    bool dry = server.arg("dry_run") == "1";
    if (!spoolCreateForLane(server.arg("lane").toInt() - 1, dry, opts.as<JsonObjectConst>(), res, err))
    {
      sendError(400, err);
      return;
    }
    res["ok"] = true;
    sendJson(200, res);
  });

  // Relay to Spoolman for the spool dialog (Spoolman sends no CORS headers,
  // so the browser can't call it directly). GET ?path=/api/v1/... reads;
  // POST ?path=...&method=POST|PATCH writes the request body (needs admin).
  server.on("/api/spoolman/relay", HTTP_ANY, []() {
    String method = server.method() == HTTP_GET ? "GET" : server.arg("method");
    if (method != "GET" && method != "POST" && method != "PATCH")
    {
      sendError(400, "Unsupported method");
      return;
    }
    if (method != "GET" && !authOk())
      return;
    if (!cfg.spoolmanUrl[0])
    {
      sendError(400, "Set the Spoolman URL in Settings first");
      return;
    }
    String path = server.arg("path");
    bool okPath = path.startsWith("/api/v1/") && path.indexOf("..") < 0;
    for (char c : path)
      okPath = okPath && (isalnum((unsigned char)c) || strchr("/_-.?=&,%", c));
    if (!okPath)
    {
      sendError(400, "Bad Spoolman path");
      return;
    }
    HTTPClient http;
    http.setTimeout(8000);
    // HTTP/1.0 drops HTTPClient's built-in "Accept-Encoding: identity", so
    // Spoolman can gzip the reply (the 3.5 MB SpoolmanDB catalog is 150 KB
    // gzipped). It's passed through still compressed; the browser unpacks it.
    http.useHTTP10(true);
    http.begin(String(cfg.spoolmanUrl) + path);
    http.addHeader("Accept-Encoding", "gzip");
    const char *keep[] = {"Content-Encoding"};
    http.collectHeaders(keep, 1);
    String body = method == "GET" ? String() : server.arg("plain");
    if (body.length())
      http.addHeader("Content-Type", "application/json");
    int code = http.sendRequest(method.c_str(), body);
    if (code <= 0)
    {
      sendError(502, String("Spoolman unreachable (") + http.errorToString(code) + ")");
      http.end();
      return;
    }
    // Stream the reply through in chunks: spool lists can be large
    if (http.header("Content-Encoding") == "gzip")
      server.sendHeader("Content-Encoding", "gzip");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(code, "application/json", "");
    ChunkOut out;
    http.writeToStream(&out);
    out.flush();
    server.sendContent("");
    http.end();
  });

  server.on("/api/spoolman/refresh", HTTP_POST, []() {
    spoolSyncRefreshAll();
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // Lane tool buttons: ?lane=N&action=load|unload|mark_loaded|mark_unloaded
  server.on("/api/lane/tool", HTTP_POST, []() {
    if (!authOk())
      return;
    String msg;
    bool ok = laneToolAction(server.arg("lane").toInt() - 1, server.arg("action"), msg);
    JsonDocument doc;
    doc["ok"] = ok;
    doc[ok ? "message" : "error"] = msg;
    sendJson(ok ? 200 : 400, doc);
  });

  server.on("/api/lane/clear", HTTP_POST, []() {
    if (!authOk())
      return;
    rfidClearLane(server.arg("lane").toInt() - 1);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/settings", HTTP_GET, []() {
    if (!authOk())
      return;
    JsonDocument doc;
    settingsToJson(doc);
    sendJson(200, doc);
  });

  server.on("/api/settings", HTTP_POST, []() {
    if (!authOk())
      return;
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")))
    {
      sendError(400, "Invalid JSON");
      return;
    }
    String err;
    if (!applySettings(doc, err))
    {
      sendError(400, err);
      return;
    }
    logEvent("Settings saved from web UI, rebooting");
    JsonDocument res;
    res["ok"] = true;
    String host = String(cfg.deviceId);
    host.replace('_', '-');
    res["hint"] = "If WiFi changed, reconnect at http://" + host + ".local/";
    sendJson(200, res);
    scheduleReboot(1500);
  });

  server.on("/api/reboot", HTTP_POST, []() {
    if (!authOk())
      return;
    server.send(200, "application/json", "{\"ok\":true}");
    logEvent("Reboot requested from web UI");
    scheduleReboot();
  });

  server.on("/api/factory-reset", HTTP_POST, []() {
    if (!authOk())
      return;
    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    factoryReset();
  });

  // Firmware upload (OTA). The default_16MB partition table has two app slots.
  server.on(
      "/update", HTTP_POST,
      []() {
        if (!uploadAuthorized)
          return; // auth response already sent
        if (Update.hasError())
          sendError(500, Update.errorString());
        else
        {
          server.send(200, "application/json", "{\"ok\":true}");
          logEvent("Firmware updated from web UI, rebooting");
          scheduleReboot();
        }
      },
      []() {
        HTTPUpload &up = server.upload();
        if (up.status == UPLOAD_FILE_START)
        {
          uploadAuthorized = authOk();
          if (!uploadAuthorized)
            return;
          logEvent("Firmware upload started: %s", up.filename.c_str());
          Update.begin(UPDATE_SIZE_UNKNOWN);
        }
        else if (!uploadAuthorized)
          return;
        else if (up.status == UPLOAD_FILE_WRITE)
          Update.write(up.buf, up.currentSize);
        else if (up.status == UPLOAD_FILE_END)
          Update.end(true);
        else if (up.status == UPLOAD_FILE_ABORTED)
          Update.abort();
      });

  // In setup-hotspot mode every unknown URL (phone captive-portal checks
  // included) lands on the settings page.
  server.onNotFound([]() {
    if (setupApActive())
    {
      server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/settings");
      server.send(302, "text/plain", "");
      return;
    }
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
}

void updateWeb()
{
  // Captive-portal DNS only while the setup hotspot is up
  if (setupApActive() && !dnsRunning)
  {
    dns.start(53, "*", WiFi.softAPIP());
    dnsRunning = true;
  }
  else if (!setupApActive() && dnsRunning)
  {
    dns.stop();
    dnsRunning = false;
  }
  if (dnsRunning)
    dns.processNextRequest();
  server.handleClient();
}
