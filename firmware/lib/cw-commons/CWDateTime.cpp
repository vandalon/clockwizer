#include "CWDateTime.h"
#include "TelnetStream.h"
#include <sys/time.h>

void CWDateTime::begin(const char *timeZone, bool use24format, const char *ntpServer = "time.cloudflare.com", const char *posixTZ = "")
{
  Serial.printf("[Time] NTP Server: %s, Timezone: %s\n", ntpServer, timeZone);

  const char *rule = posixTZ;
  if (strlen(rule) <= 1) {
    // No POSIX rule given: look the timezone name up in the built-in table
    rule = cwt::posixForZone(timeZone);
    if (!rule) {
      Serial.printf("[Time] Unknown timezone '%s', using UTC. Set a POSIX rule instead\n", timeZone);
      rule = "UTC0";
    }
  }
  Serial.printf("[Time] Rule: %s\n", rule);

  this->use24hFormat = use24format;
  cwt::begin(ntpServer, rule);
}

struct tm CWDateTime::localTm() const
{
  time_t t = cwt::now();
  struct tm tm;
  localtime_r(&t, &tm);
  return tm;
}

String CWDateTime::getFormattedTime()
{
  return getFormattedTime("%a, %d-%b-%Y %H:%M:%S");
}

String CWDateTime::getFormattedTime(const char *format)
{
  struct tm tm = localTm();
  char buffer[64];
  strftime(buffer, sizeof(buffer), format, &tm);
  return String(buffer);
}

char *CWDateTime::getHour(const char *format)
{
  static char buffer[3] = {'\0'};
  snprintf(buffer, sizeof(buffer), "%02d", getHour());
  return buffer;
}

char *CWDateTime::getMinute(const char *format)
{
  static char buffer[3] = {'\0'};
  snprintf(buffer, sizeof(buffer), "%02d", getMinute());
  return buffer;
}

int CWDateTime::getHour()
{
  int h = localTm().tm_hour;
  if (use24hFormat) return h;
  return h % 12 == 0 ? 12 : h % 12;
}

int CWDateTime::getHour24()
{
  return localTm().tm_hour;
}

int CWDateTime::getMinute()
{
  return localTm().tm_min;
}

int CWDateTime::getSecond()
{
  return localTm().tm_sec;
}

int CWDateTime::getDay()
{
  return localTm().tm_mday;
}

int CWDateTime::getMonth()
{
  return localTm().tm_mon + 1;
}

int CWDateTime::getYear()
{
  return localTm().tm_year + 1900;
}

int CWDateTime::getWeekday()
{
  return localTm().tm_wday;  // 0 = Sunday
}

long CWDateTime::getMilliseconds()
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_usec / 1000;
}

time_t CWDateTime::localNow()
{
  return utcToLocal(cwt::now());
}

time_t CWDateTime::utcToLocal(time_t utc)
{
  struct tm tm;
  localtime_r(&utc, &tm);
  return cwt::makeTime(tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
}

bool CWDateTime::isAM()
{
  return localTm().tm_hour < 12;
}

bool CWDateTime::is24hFormat()
{
  return this->use24hFormat;
}
