#include "diag.h"

#define LOG_SIZE 40
#define LOG_MSG_LEN 96

struct LogEntry
{
  unsigned long ms;
  char msg[LOG_MSG_LEN];
};
static LogEntry entries[LOG_SIZE];
static int head = 0;  // next slot to write
static int count = 0;

void logEvent(const char *fmt, ...)
{
  LogEntry &e = entries[head];
  va_list args;
  va_start(args, fmt);
  vsnprintf(e.msg, sizeof(e.msg), fmt, args);
  va_end(args);
  e.ms = millis();
  head = (head + 1) % LOG_SIZE;
  if (count < LOG_SIZE)
    count++;
  Serial.println(e.msg);
}

void logToJson(JsonArray out)
{
  for (int i = 0; i < count; i++)
  {
    const LogEntry &e = entries[(head - 1 - i + LOG_SIZE) % LOG_SIZE];
    JsonObject o = out.add<JsonObject>();
    o["t"] = e.ms / 1000;
    o["msg"] = e.msg;
  }
}
